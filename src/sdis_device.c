/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "sdis.h"
#include "sdis_device_c.h"
#include "sdis_log.h"

#include <rsys/cstr.h>
#include <rsys/logger.h>
#include <rsys/mem_allocator.h>
#include <rsys/mutex.h>

#include <star/s2d.h>
#include <star/s3d.h>
#include <star/ssp.h>
#include <star/swf.h>

#include <omp.h>

#ifdef SDIS_ENABLE_MPI
  #include <mpi.h>
#endif

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
#ifdef SDIS_ENABLE_MPI

static const char*
mpi_error_string(struct sdis_device* dev, const int mpi_err)
{
  int res_mpi = MPI_SUCCESS;
  int len;
  ASSERT(dev);

  res_mpi = MPI_Error_string(mpi_err, str_get(&dev->mpi_err_str), &len);
  return res_mpi == MPI_SUCCESS
    ? str_get(&dev->mpi_err_str) : "Invalid MPI error";
}

static const char*
mpi_thread_support_string(const int val)
{
  switch(val) {
    case MPI_THREAD_SINGLE: return "MPI_THREAD_SINGLE";
    case MPI_THREAD_FUNNELED: return "MPI_THREAD_FUNNELED";
    case MPI_THREAD_SERIALIZED: return "MPI_THREAD_SERIALIZED";
    case MPI_THREAD_MULTIPLE: return "MPI_THREAD_MULTIPLE";
    default: FATAL("Unreachable code.\n"); break;
  }
}

static res_T
mpi_print_proc_info(struct sdis_device* dev)
{
  char proc_name[MPI_MAX_PROCESSOR_NAME];
  int proc_name_len;
  char* proc_names = NULL;
  uint32_t* proc_nthreads = NULL;
  uint32_t nthreads = 0;
  int iproc;
  res_T res = RES_OK;
  ASSERT(dev);

  /* On process 0, allocate the arrays to stored gathered data */
  if(dev->mpi_rank == 0) {

    /* Allocate the array to store the per process name */
    proc_names = MEM_CALLOC(dev->allocator, (size_t)dev->mpi_nprocs,
      MPI_MAX_PROCESSOR_NAME*sizeof(*proc_names));
    if(!proc_names) {
      res = RES_MEM_ERR;
      log_err(dev,
        "Could not allocate the temporary memory for MPI process names -- "
        "%s.\n", res_to_cstr(res));
      goto error;
    }

    /* Allocate the array to store the per process #threads */
    proc_nthreads = MEM_CALLOC(dev->allocator, (size_t)dev->mpi_nprocs,
      sizeof(*proc_nthreads));
    if(!proc_nthreads) {
      res = RES_MEM_ERR;
      log_err(dev,
        "Could not allocate the temporary memory for the #threads of the MPI "
        "processes -- %s.\n", res_to_cstr(res));
      goto error;
    }
  }

  /* Gather the process name to the process 0 */
  MPI(Get_processor_name(proc_name, &proc_name_len));
  MPI(Gather(proc_name, MPI_MAX_PROCESSOR_NAME, MPI_CHAR, proc_names,
    MPI_MAX_PROCESSOR_NAME, MPI_CHAR, 0, MPI_COMM_WORLD));

  /* Gather the #threads to process 0*/
  nthreads = (uint32_t)dev->nthreads;
  MPI(Gather(&nthreads, 1, MPI_UINT32_T, proc_nthreads, 1, MPI_UINT32_T, 0,
    MPI_COMM_WORLD));

  if(dev->mpi_rank == 0) {
    FOR_EACH(iproc, 0, dev->mpi_nprocs) {
      log_info(dev, "Process %d -- %s; #threads: %u\n",
        iproc, proc_names + iproc*MPI_MAX_PROCESSOR_NAME, proc_nthreads[iproc]);
    }
  }

exit:
  if(proc_names) MEM_RM(dev->allocator, proc_names);
  if(proc_nthreads) MEM_RM(dev->allocator, proc_nthreads);
  return res;
error:
  goto exit;
}

static res_T
mpi_init(struct sdis_device* dev)
{
  int res_mpi = MPI_SUCCESS;
  int is_init = 0;
  int thread_support = 0;
  res_T res = RES_OK;
  ASSERT(dev);

  #define CALL_MPI(Func, ErrMsg) {                                             \
    res_mpi = MPI_##Func;                                                      \
    if(res_mpi != MPI_SUCCESS) {                                               \
      log_err(dev, ErrMsg" - %s\n", mpi_error_string(dev, res_mpi));           \
      res = RES_UNKNOWN_ERR;                                                   \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  CALL_MPI(Initialized(&is_init),
    "Error querying the MPI init state");

  if(!is_init) {
    log_err(dev,
      "MPI is not initialized. The MPI_Init[_thread] function must be called "
      "priorly to the creation of the Stardis device.\n");
    res = RES_BAD_OP;
    goto error;
  }

  CALL_MPI(Query_thread(&thread_support),
    "Error querying the MPI thread support");

  if(thread_support < MPI_THREAD_SERIALIZED) {
    log_err(dev,
     "The provided MPI implementation does not support serialized API calls "
     "from multiple threads. The thread support is limited to %s.\n",
     mpi_thread_support_string(thread_support));
    res = RES_BAD_OP;
    goto error;
  }

  CALL_MPI(Comm_rank(MPI_COMM_WORLD, &dev->mpi_rank),
    "Error retrieving the MPI rank");
  CALL_MPI(Comm_size(MPI_COMM_WORLD, &dev->mpi_nprocs),
    "Error retrieving the size of the MPI group");

  #undef CALL_MPI

  dev->mpi_mutex = mutex_create();
  if(!dev->mpi_mutex) {
    log_err(dev,
      "Error creating the mutex used to protect the MPI calls.\n");
    res = RES_MEM_ERR;
    goto error;
  }

  mpi_print_proc_info(dev);

exit:
  return res;
error:
  if(dev->mpi_mutex) {
    mutex_destroy(dev->mpi_mutex);
    dev->mpi_mutex = NULL;
  }
  goto exit;
}

#endif /* SDIS_ENABLE_MPI */

static INLINE int
check_sdis_device_create_args(const struct sdis_device_create_args* args)
{
  return args && args->nthreads_hint != 0;
}

static INLINE res_T
setup_logger
  (struct sdis_device* dev,
   const struct sdis_device_create_args* args)
{
  ASSERT(dev && args);
  if(args->logger) {
    dev->logger = args->logger;
  } else {
    setup_log_default(dev);
  }
  return RES_OK;
}

static INLINE res_T
setup_star2d(struct sdis_device* dev)
{
  res_T res = RES_OK;
  ASSERT(dev);
  res = s2d_device_create(dev->logger, dev->allocator, 0, &dev->s2d_dev);
  if(res != RES_OK) {
    log_err(dev,
      "Could not create the Star-2D device for Stardis-Solver -- %s.\n",
      res_to_cstr(res));
    goto error;
  }
exit:
  return res;
error:
  goto exit;
}

static INLINE res_T
setup_star3d(struct sdis_device* dev)
{
  res_T res = RES_OK;
  ASSERT(dev);
  res = s3d_device_create(dev->logger, dev->allocator, 0, &dev->s3d_dev);
  if(res != RES_OK) {
    log_err(dev,
      "Could not create the Star-3D device for Stardis-Solver -- %s.\n",
      res_to_cstr(res));
    goto error;
  }
exit:
  return res;
error:
  goto exit;
}

static INLINE res_T
setup_starwf(struct sdis_device* dev)
{
  struct swf_H_tabulate_args H2d_args = SWF_H2D_TABULATE_ARGS_DEFAULT;
  struct swf_H_tabulate_args H3d_args = SWF_H3D_TABULATE_ARGS_DEFAULT;
  res_T res = RES_OK;
  ASSERT(dev);

  H2d_args.allocator = dev->allocator;
  H3d_args.allocator = dev->allocator;

  res = swf_H2d_tabulate(&H2d_args, &dev->H_2d);
  if(res != RES_OK) {
    log_err(dev, "Unable to tabulate H2d function -- %s.\n",
      res_to_cstr(res));
    goto error;
  }

  res = swf_H3d_tabulate(&H3d_args, &dev->H_3d);
  if(res != RES_OK) {
    log_err(dev, "Unable to tabulate H3d function -- %s.\n",
      res_to_cstr(res));
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static INLINE res_T
setup_mpi(struct sdis_device* dev, const struct sdis_device_create_args* args)
{
  ASSERT(dev && args);
#ifdef SDIS_ENABLE_MPI
  dev->use_mpi = args->use_mpi;
  if(args->use_mpi) {
    const res_T res = mpi_init(dev);
    if(res != RES_OK) return res;
  }
#else
  if(args->use_mpi) {
    log_warn(dev,
      "Stardis-Solver is built without the support of the Message Passing "
      "Interface. MPI cannot be used for parallel computations.\n");
  }
#endif
  return RES_OK;

}

static void
device_release(ref_T* ref)
{
  struct sdis_device* dev;
  ASSERT(ref);
  dev = CONTAINER_OF(ref, struct sdis_device, ref);
  if(dev->s2d_dev) S2D(device_ref_put(dev->s2d_dev));
  if(dev->s3d_dev) S3D(device_ref_put(dev->s3d_dev));
  if(dev->H_2d) SWF(tabulation_ref_put(dev->H_2d));
  if(dev->H_3d) SWF(tabulation_ref_put(dev->H_3d));
  if(dev->logger == &dev->logger__) logger_release(&dev->logger__);
  ASSERT(flist_name_is_empty(&dev->interfaces_names));
  ASSERT(flist_name_is_empty(&dev->media_names));
  ASSERT(flist_name_is_empty(&dev->source_names));
  flist_name_release(&dev->interfaces_names);
  flist_name_release(&dev->media_names);
  flist_name_release(&dev->source_names);
#ifdef SDIS_ENABLE_MPI
  if(dev->mpi_mutex) mutex_destroy(dev->mpi_mutex);
  str_release(&dev->mpi_err_str);
#endif
  MEM_RM(dev->allocator, dev);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_device_create
  (const struct sdis_device_create_args* args,
   struct sdis_device** out_dev)
{
  struct sdis_device* dev = NULL;
  struct mem_allocator* allocator = NULL;
  unsigned nthreads_max = 0;
  res_T res = RES_OK;

  if(!check_sdis_device_create_args(args) || !out_dev) {
    res = RES_BAD_ARG;
    goto error;
  }

  allocator = args->allocator ? args->allocator : &mem_default_allocator;
  dev = MEM_CALLOC(allocator, 1, sizeof(struct sdis_device));
  if(!dev) {
    if(args->verbosity) {
      #define ERR_STR STR(FUNC_NAME)": could not allocate the Stardis device -- %s."
      if(args->logger) {
        logger_print(args->logger, LOG_ERROR, ERR_STR, res_to_cstr(res));
      } else {
        fprintf(stderr, MSG_ERROR_PREFIX ERR_STR, res_to_cstr(res));
      }
      #undef ERR_STR
    }
    res = RES_MEM_ERR;
    goto error;
  }
  nthreads_max = (unsigned)MMAX(omp_get_max_threads(), omp_get_num_procs());
  dev->allocator = allocator;
  dev->no_escape_sequence = args->no_escape_sequence;
  dev->verbose = args->verbosity;
  dev->nthreads = MMIN(args->nthreads_hint, nthreads_max);
  ref_init(&dev->ref);
  flist_name_init(allocator, &dev->interfaces_names);
  flist_name_init(allocator, &dev->media_names);
  flist_name_init(allocator, &dev->source_names);
#ifdef SDIS_ENABLE_MPI
  str_init(allocator, &dev->mpi_err_str);
#endif

  res = setup_logger(dev, args);
  if(res != RES_OK) goto error;
  res = setup_star2d(dev);
  if(res != RES_OK) goto error;
  res = setup_star3d(dev);
  if(res != RES_OK) goto error;
  res = setup_starwf(dev);
  if(res != RES_OK) goto error;
  res = setup_mpi(dev, args);
  if(res != RES_OK) goto error;

  log_info(dev, "Using %lu %s.\n", (unsigned long)dev->nthreads,
    dev->nthreads == 1 ? "thread" : "threads");

exit:
  if(out_dev) *out_dev = dev;
  return res;
error:
  if(dev) { SDIS(device_ref_put(dev)); dev = NULL; }
  goto exit;
}

res_T
sdis_device_ref_get(struct sdis_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_get(&dev->ref);
  return RES_OK;
}

res_T
sdis_device_ref_put(struct sdis_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_put(&dev->ref, device_release);
  return RES_OK;
}

res_T
sdis_device_is_mpi_used(struct sdis_device* dev, int* is_mpi_used)
{
  if(!dev || !is_mpi_used) return RES_BAD_ARG;
#ifndef SDIS_ENABLE_MPI
  *is_mpi_used = 0;
#else
  *is_mpi_used = dev->use_mpi;
#endif
  return RES_OK;
}

res_T
sdis_device_get_mpi_rank(struct sdis_device* dev, int* rank)
{
#ifndef SDIS_ENABLE_MPI
  (void)dev, (void)rank;
  return RES_BAD_OP;
#else
  if(!dev || !rank) return RES_BAD_ARG;
  if(!dev->use_mpi) return RES_BAD_OP;
  ASSERT(dev->mpi_rank >= 0);
  *rank = dev->mpi_rank;
  return RES_OK;
#endif
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
create_rng_from_rng_proxy
  (struct sdis_device* dev,
   const struct ssp_rng_proxy* proxy,
   struct ssp_rng** out_rng)
{
  enum ssp_rng_type rng_type;
  struct ssp_rng* rng = NULL;
  FILE* stream = NULL;
  res_T res = RES_OK;
  ASSERT(dev && proxy && out_rng);

  stream = tmpfile();
  if(!stream) {
    log_err(dev,
      "Could not open a temporary stream to store the RNG state.\n");
    res = RES_IO_ERR;
    goto error;
  }

  SSP(rng_proxy_get_type(proxy, &rng_type));
  res = ssp_rng_create(dev->allocator, rng_type, &rng);
  if(res != RES_OK) {
    log_err(dev, "Could not create the RNG -- %s\n", res_to_cstr(res));
    goto error;
  }

  res = ssp_rng_proxy_write(proxy, stream);
  if(res != RES_OK) {
    log_err(dev, "Could not serialize the RNG state -- %s\n",
      res_to_cstr(res));
    goto error;
  }

  rewind(stream);
  res = ssp_rng_read(rng, stream);
  if(res != RES_OK) {
    log_err(dev, "Could not read the serialized RNG state -- %s\n",
      res_to_cstr(res));
    goto error;
  }

exit:
  if(out_rng) *out_rng = rng;
  if(stream) fclose(stream);
  return res;
error:
  if(rng) {
    SSP(rng_ref_put(rng));
    rng = NULL;
  }
  goto exit;
}
