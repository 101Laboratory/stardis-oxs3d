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

#define _POSIX_C_SOURCE 200112L

#include "sdis.h"
#include "sdis_c.h"
#include "sdis_device_c.h"
#include "sdis_estimator_c.h"
#include "sdis_green.h"
#include "sdis_log.h"
#include "sdis_misc.h"
#include "sdis_scene_c.h"
#ifdef SDIS_ENABLE_MPI
  #include "sdis_mpi.h"
#endif

#include <star/ssp.h>

#include <rsys/cstr.h>
#include <rsys/clock_time.h>
#include <rsys/mem_allocator.h>

#include <errno.h>

/* Number random numbers in a sequence, i.e. number of consecutive random
 * numbers that can be used by a thread */
#define RNG_SEQUENCE_SIZE 100000

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
check_accum_message(const enum mpi_sdis_message msg)
{
  return msg == MPI_SDIS_MSG_ACCUM_TEMP
      || msg == MPI_SDIS_MSG_ACCUM_TIME
      || msg == MPI_SDIS_MSG_ACCUM_FLUX_CONVECTIVE
      || msg == MPI_SDIS_MSG_ACCUM_FLUX_IMPOSED
      || msg == MPI_SDIS_MSG_ACCUM_FLUX_RADIATIVE
      || msg == MPI_SDIS_MSG_ACCUM_FLUX_TOTAL
      || msg == MPI_SDIS_MSG_ACCUM_MEAN_POWER;
}

static res_T
gather_green_functions_no_mpi
  (struct sdis_scene* scn,
   struct ssp_rng_proxy* rng_proxy,
   struct sdis_green_function* per_thread_green[],
   const struct accum* per_thread_acc_time,
   struct sdis_green_function** out_green)
{
  struct sdis_green_function* green = NULL;
  struct accum acc_time = ACCUM_NULL;
  res_T res = RES_OK;
  ASSERT(scn && rng_proxy && per_thread_green && per_thread_acc_time);
  ASSERT(out_green);

  /* Redux the per thread green function into the green function of the 1st
   * thread */
  res = green_function_redux_and_clear
    (per_thread_green[0], per_thread_green+1, scn->dev->nthreads-1);
  if(res != RES_OK) goto error;

  /* Return the green of the 1st thread */
  SDIS(green_function_ref_get(per_thread_green[0]));
  green = per_thread_green[0];

  res = gather_accumulators
    (scn->dev, MPI_SDIS_MSG_ACCUM_TIME, per_thread_acc_time, &acc_time);
  if(res != RES_OK) goto error;

  /* Finalize the estimated green */
  res = green_function_finalize(green, rng_proxy, &acc_time);
  if(res != RES_OK) goto error;

exit:
  *out_green = green;
  return res;
error:
  if(green) { SDIS(green_function_ref_put(green)); green = NULL; }
  goto exit;
}

#ifdef SDIS_ENABLE_MPI
static void
rewind_progress_printing(struct sdis_device* dev)
{
  size_t i;

  if(!dev->use_mpi
  || dev->no_escape_sequence
  || dev->mpi_nprocs == 1)
    return;

  FOR_EACH(i, 0, (size_t)(dev->mpi_nprocs-1)) {
    log_info(dev, "\033[1A\r"); /* Move up */
  }
}

static res_T
send_green_function_to_master_process
  (struct sdis_device* dev,
   struct sdis_green_function* green)
{
  char buf[128];
  FILE* stream = NULL; /* Temp file that stores the serialized green function */
  void* data = NULL; /* Pointer to serialized green function data */
  long sz = 0; /* Size in Bytes of the serialized green function data */
  res_T res = RES_OK;
  ASSERT(dev && green && dev->mpi_rank != 0);

  /* Open a stream to store the serialized green function */
  stream = tmpfile();
  if(!stream) {
    log_err(dev,
      "Could not open the stream used to temporary store the green function "
      "before it is sent to the master process.\n");
    res = RES_IO_ERR;
    goto error;
  }

  /* Write the green function into the stream */
  res = sdis_green_function_write(green, stream);
  if(res != RES_OK) goto error;

  /* Fetch the size of the serialized data */
  sz = ftell(stream);
  if(sz == -1) {
    strerror_r(errno, buf, sizeof(buf));
    log_err(dev,
      "Could not query the size of the serialized green function data to sent "
      "to the master process -- %s.\n", buf);
    res = RES_IO_ERR;
    goto error;
  }
  if(sz > INT_MAX) {
    log_err(dev,
      "The size of the green function data is too large. It must be less than "
      "%d Mebabytes while it is of %ld MegabBytes.\n",
      INT_MAX / (1024*1024), sz/(1024*1024));
    res = RES_MEM_ERR;
    goto error;
  }

  data = MEM_CALLOC(dev->allocator, 1, (size_t)sz);
  if(!data) {
    log_err(dev, "Could not allocate the memory to store the serialized green "
      "function before it is sent to the master process.\n");
    res = RES_MEM_ERR;
    goto error;
  }

  /* Load in memory the serialized data */
  rewind(stream);
  if(fread(data, (size_t)sz, 1, stream) != 1) {
    log_err(dev,
      "Could not read the serialized green function data from the temporary "
      "stream before it is sent to the master process.\n");
    res = RES_IO_ERR;
    goto error;
  }

  /* Send the serialized data to the master process */
  mutex_lock(dev->mpi_mutex);
  MPI(Send(data, (int)sz, MPI_CHAR, 0/*Dst*/, MPI_SDIS_MSG_GREEN_FUNCTION,
    MPI_COMM_WORLD));
  mutex_unlock(dev->mpi_mutex);

exit:
  if(stream) CHK(fclose(stream) == 0);
  if(data) MEM_RM(dev->allocator, data);
  return RES_OK;
error:
  goto exit;
}

static res_T
gather_green_functions_from_non_master_process
  (struct sdis_scene* scn,
   struct sdis_green_function* greens[])
{
  struct sdis_green_function_create_from_stream_args green_args =
    SDIS_GREEN_FUNCTION_CREATE_FROM_STREAM_ARGS_DEFAULT;

  void* data = NULL; /* Pointer to gathered serialized green function data */
  FILE* stream = NULL; /* Temp file that stores the serialized green function */
  int iproc;
  res_T res = RES_OK;
  ASSERT(scn->dev && greens && scn->dev->mpi_rank == 0);

  FOR_EACH(iproc, 1, scn->dev->mpi_nprocs) {
    MPI_Request req;
    MPI_Status status;
    int count;

    /* Waiting for the serialized green function sent by the process `iproc'*/
    mpi_waiting_for_message
      (scn->dev, iproc, MPI_SDIS_MSG_GREEN_FUNCTION, &status);

    /* Fetch the sizeof the green function sent by the process `iproc' */
    mutex_lock(scn->dev->mpi_mutex);
    MPI(Get_count(&status, MPI_CHAR, &count));
    mutex_unlock(scn->dev->mpi_mutex);

    /* Allocate the memory to store the serialized green function sent by the
     * process `iproc' */
    data = MEM_REALLOC(scn->dev->allocator, data, (size_t)count);
    if(!data) {
      log_err(scn->dev,
        "Could not allocate %d bytes to store the serialized green function "
        "sent by the process %d.\n",
        count, iproc);
      res = RES_MEM_ERR;
      goto error;
    }

    /* Asynchronously receive the green function */
    mutex_lock(scn->dev->mpi_mutex);
    MPI(Irecv(data, count, MPI_CHAR, iproc, MPI_SDIS_MSG_GREEN_FUNCTION,
      MPI_COMM_WORLD, &req));
    mutex_unlock(scn->dev->mpi_mutex);
    mpi_waiting_for_request(scn->dev, &req);

    /* Open a stream to store the serialized green function */
    stream = tmpfile();
    if(!stream) {
      log_err(scn->dev,
        "Could not open the stream used to temporary store the green function "
        "sent by the process %d.\n", iproc);
      res = RES_IO_ERR;
      goto error;
    }

    if(fwrite(data, (size_t)count, 1, stream) != 1) {
      log_err(scn->dev,
        "Could not write the green function sent by the process %d into the "
        "temporary stream.\n", iproc);
      res = RES_IO_ERR;
      goto error;
    }

    /* Deserialized the green function of the process `iproc'. Note that the
     * number of green functions to output is #procs - 1. Since we
     * iterate over the indices of non master processes in [1, #procs],
     * the index the green function to deserialized is iproc - 1 */
    rewind(stream);
    green_args.scene = scn;
    green_args.stream = stream;
    res = sdis_green_function_create_from_stream(&green_args, &greens[iproc-1]);
    if(res != RES_OK) {
      log_err(scn->dev,
        "Error deserializing the green function sent by the process %d -- %s.\n",
        iproc, res_to_cstr(res));
      goto error;
    }

    CHK(fclose(stream) == 0);
    stream = NULL;
  }

exit:
  if(data) MEM_RM(scn->dev->allocator, data);
  if(stream) CHK(fclose(stream) == 0);
  return res;
error:
  goto exit;
}
#endif

/*******************************************************************************
 * Exported function
 ******************************************************************************/
res_T
sdis_get_info(struct sdis_info* info)
{
  if(!info) return RES_BAD_ARG;
  *info = SDIS_INFO_NULL;
#ifdef SDIS_ENABLE_MPI
  info->mpi_enabled = 1;
#else
  info->mpi_enabled = 0;
#endif
  return RES_OK;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
create_per_thread_rng
  (struct sdis_device* dev,
   struct ssp_rng* rng_state,
   const enum ssp_rng_type rng_type,
   struct ssp_rng_proxy** out_proxy,
   struct ssp_rng** out_rngs[])
{
  struct ssp_rng_proxy_create2_args proxy_args = SSP_RNG_PROXY_CREATE2_ARGS_NULL;
  struct ssp_rng_proxy* proxy = NULL;
  struct ssp_rng** rngs = NULL;
  size_t i;
  res_T res = RES_OK;
  ASSERT(dev && out_proxy && out_rngs);

  rngs = MEM_CALLOC(dev->allocator, dev->nthreads, sizeof(*rngs));
  if(!rngs) {
    log_err(dev, "Could not allocate the list of per thread RNG.\n");
    res = RES_MEM_ERR;
    goto error;
  }

  /* Create the RNG proxy */
  proxy_args.rng= rng_state;
  proxy_args.type = rng_type;
  proxy_args.nbuckets = dev->nthreads;
#ifdef SDIS_ENABLE_MPI
  if(dev->use_mpi) {
    proxy_args.sequence_size = RNG_SEQUENCE_SIZE;
    proxy_args.sequence_offset = RNG_SEQUENCE_SIZE * (size_t)dev->mpi_rank;
    proxy_args.sequence_pitch = RNG_SEQUENCE_SIZE * (size_t)dev->mpi_nprocs;
  } else
#endif
  {
    proxy_args.sequence_size = RNG_SEQUENCE_SIZE;
    proxy_args.sequence_offset = 0;
    proxy_args.sequence_pitch = RNG_SEQUENCE_SIZE;
  }
  res = ssp_rng_proxy_create2(dev->allocator, &proxy_args, &proxy);
  if(res != RES_OK) goto error;

  /* Query the RNG proxy to create the per thread RNGs */
  FOR_EACH(i, 0, dev->nthreads) {
    res = ssp_rng_proxy_create_rng(proxy, i, &rngs[i]);
    if(res != RES_OK) goto error;
  }

exit:
  *out_rngs = rngs;
  *out_proxy = proxy;
  return res;
error:
  if(rngs) { release_per_thread_rng(dev, rngs); rngs = NULL; }
  if(proxy) { SSP(rng_proxy_ref_put(proxy)); proxy = NULL; }
  goto exit;
}

void
release_per_thread_rng(struct sdis_device* dev, struct ssp_rng* rngs[])
{
  size_t i;
  ASSERT(dev);
  if(!rngs) return;
  FOR_EACH(i, 0, dev->nthreads) { if(rngs[i]) SSP(rng_ref_put(rngs[i])); }
  MEM_RM(dev->allocator, rngs);
}

res_T
create_per_thread_green_function
  (struct sdis_scene* scn,
   const hash256_T signature,
   struct sdis_green_function** out_greens[])
{
  struct sdis_green_function** greens = NULL;
  size_t i;
  res_T res = RES_OK;
  ASSERT(scn && out_greens);

  greens = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*greens));
  if(!greens) {
    log_err(scn->dev,
      "Could not allocate the list of per thread green function.\n");
    res = RES_MEM_ERR;
    goto error;
  }

  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = green_function_create(scn, signature, &greens[i]);
    if(res != RES_OK) goto error;
  }

exit:
  *out_greens = greens;
  return res;
error:
  if(greens) {
    release_per_thread_green_function(scn, greens);
    greens = NULL;
  }
  goto exit;
}

void
release_per_thread_green_function
  (struct sdis_scene* scn,
   struct sdis_green_function* greens[])
{
  size_t i;
  ASSERT(greens);
  FOR_EACH(i, 0, scn->dev->nthreads) {
    if(greens[i]) SDIS(green_function_ref_put(greens[i]));
  }
  MEM_RM(scn->dev->allocator, greens);
}

res_T
alloc_process_progress(struct sdis_device* dev, int32_t** out_progress)
{
  int32_t* progress = NULL;
  size_t nprocs;
  res_T res = RES_OK;
  ASSERT(dev && out_progress);

#ifdef SDIS_ENABLE_MPI
  if(dev->use_mpi) {
    nprocs = (size_t)dev->mpi_nprocs;
  } else
#endif
  {
    nprocs = 1;
  }
  progress = MEM_CALLOC(dev->allocator, nprocs, sizeof(*progress));
  if(!progress) {
    log_err(dev,"Could not allocate the list of per process progress status.\n");
    res = RES_MEM_ERR;
    goto error;
  }

exit:
  *out_progress = progress;
  return res;
error:
  if(progress) { MEM_RM(dev->allocator, progress); progress = NULL; }
  goto exit;
}

void
free_process_progress(struct sdis_device* dev, int32_t progress[])
{
  ASSERT(dev && progress);
  MEM_RM(dev->allocator, progress);
}

size_t
compute_process_index_range
  (const struct sdis_device* dev,
   const size_t nindices,
   size_t range[2])
{
#ifndef SDIS_ENABLE_MPI
  (void)dev;
  range[0] = 0;
  range[1] = nindices; /* Upper bound is _exclusive_ */
#else
  ASSERT(dev);

  if(!dev->use_mpi) {
    range[0] = 0;
    range[1] = nindices;
  } else {
    size_t per_process_indices = 0;
    size_t remaining_indices = 0;

    /* Compute the minimum number of indices on each process */
    per_process_indices = nindices / (size_t)dev->mpi_nprocs;

    range[0] = per_process_indices * (size_t)dev->mpi_rank;
    range[1] = range[0] + per_process_indices; /* Upper bound is _exclusive_ */
    ASSERT(range[0] <= range[1]);

    /* Set the remaining number of indices that are not managed by one process */
    remaining_indices =
      nindices - per_process_indices * (size_t)dev->mpi_nprocs;

    /* Distribute the remaining indices among the processes. Each process whose
     * rank is lower than the number of remaining indices takes an additional
     * index. To ensure continuity of indices per process, subsequent processes
     * shift their initial rank accordingly, i.e. process 1 shifts its indices
     * by 1, process 2 shifts them by 2 and so on until there are no more
     * indices to distribute. From then on, subsequent processes simply shift
     * their index range by the number of remaining indices that have been
     * distributed. */
    if((size_t)dev->mpi_rank < remaining_indices) {
      range[0] += (size_t)dev->mpi_rank;
      range[1] += (size_t)dev->mpi_rank + 1/* Take one more index */;
    } else {
      range[0] += remaining_indices;
      range[1] += remaining_indices;
    }
  }
#endif
  return range[1] - range[0];
}

#ifndef SDIS_ENABLE_MPI
res_T
gather_accumulators
  (struct sdis_device* dev,
   const enum mpi_sdis_message msg,
   const struct accum* per_thread_acc,
   struct accum* acc)
{
  (void)msg;
  ASSERT(dev);
  /* Gather thread accumulators */
  sum_accums(per_thread_acc, dev->nthreads, acc);
  return RES_OK;
}
#endif

#ifdef SDIS_ENABLE_MPI
res_T
gather_accumulators
  (struct sdis_device* dev,
   const enum mpi_sdis_message msg,
   const struct accum* per_thread_acc,
   struct accum* acc)
{
  struct accum* per_proc_acc = NULL;
  size_t nprocs = 0;
  res_T res = RES_OK;
  ASSERT(dev && per_thread_acc && acc && check_accum_message(msg));

  if(!dev->use_mpi) {
    /* Gather thread accumulators */
    sum_accums(per_thread_acc, dev->nthreads, acc);
    goto exit;
  }

  nprocs = dev->mpi_rank == 0 ? (size_t)dev->mpi_nprocs : 1;
  per_proc_acc = MEM_CALLOC(dev->allocator, nprocs, sizeof(struct accum));
  if(!per_proc_acc) { res = RES_MEM_ERR; goto error; }

  /* Gather thread accumulators */
  sum_accums(per_thread_acc, dev->nthreads, &per_proc_acc[0]);

  /* Non master process */
  if(dev->mpi_rank != 0) {

    /* Send the accumulator to the master process */
    mutex_lock(dev->mpi_mutex);
    MPI(Send(&per_proc_acc[0], sizeof(per_proc_acc[0]), MPI_CHAR, 0/*Dst*/,
      msg, MPI_COMM_WORLD));
    mutex_unlock(dev->mpi_mutex);

    *acc = per_proc_acc[0];

  /* Master process */
  } else {
    int iproc;

    /* Gather process accumulators */
    FOR_EACH(iproc, 1, dev->mpi_nprocs) {
      MPI_Request req;

      /* Asynchronously receive the accumulator of `iproc' */
      mutex_lock(dev->mpi_mutex);
      MPI(Irecv(&per_proc_acc[iproc], sizeof(per_proc_acc[iproc]), MPI_CHAR,
        iproc, msg, MPI_COMM_WORLD, &req));
      mutex_unlock(dev->mpi_mutex);
      mpi_waiting_for_request(dev, &req);
    }

    /* Sum the process accumulators */
    sum_accums(per_proc_acc, (size_t)dev->mpi_nprocs, acc);
  }

exit:
  if(per_proc_acc) MEM_RM(dev->allocator, per_proc_acc);
  return res;
error:
  goto exit;
}
#endif /* SDIS_ENABLE_MPI */

#ifndef SDIS_ENABLE_MPI
res_T
gather_accumulators_list
  (struct sdis_device* dev,
   const enum mpi_sdis_message msg,
   const size_t nprobes, /* Total number of probes */
   const size_t process_probes[2], /* Ids of the probes managed by the process */
   struct accum* per_probe_acc) /* List of per probe accumulators */
{
  (void)dev, (void)msg, (void) nprobes;
  (void)process_probes, (void)per_probe_acc;
  return RES_OK;
}
#else
res_T
gather_accumulators_list
  (struct sdis_device* dev,
   const enum mpi_sdis_message msg,
   const size_t nprobes, /* Total number of probes */
   const size_t process_probes[2], /* Range of probes managed by the process */
   struct accum* per_probe_acc) /* List of per probe accumulators */
{
  struct accum_list {
    size_t size;
    /* Simulate a C99 flexible array */
    ALIGN(16) struct accum accums[1/*Dummy element*/];
  }* accum_list = NULL;
  size_t max_per_process_nprobes = 0; /* Maximum #probes per process */
  size_t process_nprobes = 0; /* Number of process probes */
  size_t msg_sz = 0; /* Size in bytes of the message to send */
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(dev);
  ASSERT(process_nprobes == 0 || (process_probes && per_probe_acc));

  /* Without MPI, do nothing since per_probe_acc already has all the
   * accumulators */
  if(!dev->use_mpi) goto exit;

  /* Defines the maximum number of probes managed by a process. In fact, it's
   * the number of probes divided by the number of processes, plus one to manage
   * the remainder of the entire division: the remaining probes are distributed
   * between the processes */
  max_per_process_nprobes = nprobes/(size_t)dev->mpi_nprocs + 1;

  /* Number of probes */
  process_nprobes = process_probes[1] - process_probes[0];

  /* Allocate the array into which the data to be collected is copied */
  msg_sz =
    sizeof(struct accum_list)
  + sizeof(struct accum)*max_per_process_nprobes
  - 1/*Dummy element */;
  if(msg_sz > INT_MAX) {
    log_err(dev, "%s: invalid MPI message size %lu.\n",
      FUNC_NAME, (unsigned long)msg_sz);
    res = RES_BAD_ARG;
    goto error;
  }

  accum_list = MEM_CALLOC(dev->allocator, 1, msg_sz);
  if(!accum_list) {
    log_err(dev,
      "%s: unable to allocate the temporary list of accumulators.\n",
      FUNC_NAME);
    res = RES_MEM_ERR;
    goto error;
  }

  /* Non master process */
  if(dev->mpi_rank != 0) {

    /* Setup the message to be sent */
    accum_list->size = process_nprobes;
    memcpy(accum_list->accums, per_probe_acc,
      sizeof(struct accum)*process_nprobes);

    mutex_lock(dev->mpi_mutex);
    MPI(Send(accum_list, (int)msg_sz, MPI_CHAR, 0/*Dst*/, msg, MPI_COMM_WORLD));
    mutex_unlock(dev->mpi_mutex);

  /* Master process */
  } else {
    size_t gathered_nprobes = process_nprobes;
    int iproc;

    FOR_EACH(iproc, 1, dev->mpi_nprocs) {
      MPI_Request req;

      /* Asynchronously receive the accumulator of `iproc' */
      mutex_lock(dev->mpi_mutex);
      MPI(Irecv
        (accum_list, (int)msg_sz, MPI_CHAR, iproc, msg, MPI_COMM_WORLD, &req));
      mutex_unlock(dev->mpi_mutex);

      mpi_waiting_for_request(dev, &req);

      memcpy(per_probe_acc+gathered_nprobes, accum_list->accums,
        sizeof(struct accum)*accum_list->size);

      gathered_nprobes += accum_list->size;
    }
  }

exit:
  if(accum_list) MEM_RM(dev->allocator, accum_list);
  return res;
error:
  goto exit;
}
#endif

#ifndef SDIS_ENABLE_MPI
res_T
gather_green_functions
  (struct sdis_scene* scn,
   struct ssp_rng_proxy* rng_proxy,
   struct sdis_green_function* per_thread_green[],
   const struct accum* per_thread_acc_time,
   struct sdis_green_function** out_green)
{
  return gather_green_functions_no_mpi
    (scn, rng_proxy, per_thread_green, per_thread_acc_time, out_green);
}
#else
res_T
gather_green_functions
  (struct sdis_scene* scn,
   struct ssp_rng_proxy* rng_proxy,
   struct sdis_green_function* per_thread_green[],
   const struct accum* per_thread_acc_time,
   struct sdis_green_function** out_green)
{
  struct accum acc_time = ACCUM_NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_green_function** per_proc_green = NULL;
  unsigned ithread;
  int iproc;
  res_T res = RES_OK;
  ASSERT(scn && per_thread_green && out_green);

  if(!scn->dev->use_mpi) {
    return gather_green_functions_no_mpi
      (scn, rng_proxy, per_thread_green, per_thread_acc_time, out_green);
    goto exit;
  }

  /* Redux the per thread green function into the green function of the 1st
   * thread */
  res = green_function_redux_and_clear
    (per_thread_green[0], per_thread_green+1, scn->dev->nthreads-1);
  if(res != RES_OK) goto error;

  /* Gather the accumulators. The master process gathers all accumulators and
   * non master process gather their per thread accumulators only that is
   * sent to the master process */
  res = gather_accumulators
    (scn->dev, MPI_SDIS_MSG_ACCUM_TIME, per_thread_acc_time, &acc_time);
  if(res != RES_OK) goto error;

  /* Non master process */
  if(scn->dev->mpi_rank != 0) {
    /* Return the green of the 1st thread */
    SDIS(green_function_ref_get(per_thread_green[0]));
    green = per_thread_green[0];

    /* We have to finalize the green function priorly to its sent to the master
     * process. Its serialization failed without it. */
    res = green_function_finalize(green, rng_proxy, &acc_time);
    if(res != RES_OK) goto error;

    res  = send_green_function_to_master_process(scn->dev, green);
    if(res != RES_OK) goto error;

  /* Master process */
  } else {
    /* Allocate the list of per process green functions */
    per_proc_green = MEM_CALLOC(scn->dev->allocator,
      (size_t)scn->dev->mpi_nprocs, sizeof(*per_proc_green));
    if(!per_proc_green) {
      log_err(scn->dev,
        "Could not allocate the temporary list of per process "
        "green functions.\n");
      res = RES_MEM_ERR;
      goto error;
    }

    /* Set the gathered per thread green function stores on thread 0 at the
     * green function for master process */
    SDIS(green_function_ref_get(per_thread_green[0]));
    per_proc_green[0] = per_thread_green[0];

    /* Release per thread green functions */
    FOR_EACH(ithread, 0, scn->dev->nthreads) {
      SDIS(green_function_ref_put(per_thread_green[ithread]));
      per_thread_green[ithread] = NULL;
    }

    res = gather_green_functions_from_non_master_process(scn, per_proc_green+1);
    if(res != RES_OK) goto error;

    /* Redux the per proc green function into the green function of the master
     * process */
    res = green_function_redux_and_clear
      (per_proc_green[0], per_proc_green+1, (size_t)scn->dev->mpi_nprocs-1);
    if(res != RES_OK) goto error;

    /* Return the gatherd green function of the master process */
    SDIS(green_function_ref_get(per_proc_green[0]));
    green = per_proc_green[0];

    /* Finalize the green function */
    res = green_function_finalize(green, rng_proxy, &acc_time);
    if(res != RES_OK) goto error;
  }

exit:
  if(per_proc_green) {
    FOR_EACH(iproc, 0, scn->dev->mpi_nprocs) {
      if(per_proc_green[iproc]) {
        SDIS(green_function_ref_put(per_proc_green[iproc]));
      }
    }
    MEM_RM(scn->dev->allocator, per_proc_green);
  }
  *out_green = green;
  return res;
error:
  if(green) { SDIS(green_function_ref_put(green)); green = NULL; }
  goto exit;
}

#endif

#ifndef SDIS_ENABLE_MPI
res_T
gather_rng_proxy_sequence_id
  (struct sdis_device* dev,
   struct ssp_rng_proxy* proxy)
{
  ASSERT(dev && proxy);
  (void)dev, (void)proxy;
  return RES_OK;
}
#else

res_T
gather_rng_proxy_sequence_id
  (struct sdis_device* dev,
   struct ssp_rng_proxy* proxy)
{
  unsigned long proc_seq_id = 0;
  size_t seq_id = SSP_SEQUENCE_ID_NONE;
  res_T res = RES_OK;
  ASSERT(dev && proxy);

  if(!dev->use_mpi) goto exit;

  /* Retrieve the sequence id of the process */
  SSP(rng_proxy_get_sequence_id(proxy, &seq_id));
  CHK(seq_id <= ULONG_MAX);
  proc_seq_id = (unsigned long)seq_id;

  /* Non master process */
  if(dev->mpi_rank != 0) {

    /* Send the sequence id to the master process */
    mutex_lock(dev->mpi_mutex);
    MPI(Send(&proc_seq_id, 1, MPI_UNSIGNED_LONG, 0/*Dst*/,
      MPI_SDIS_MSG_RNG_PROXY_SEQUENCE_ID, MPI_COMM_WORLD));
    mutex_unlock(dev->mpi_mutex);

  /* Master process */
  } else {
    size_t nseqs_to_flush = 0;
    unsigned long max_seq_id = 0;
    int iproc;

    max_seq_id = proc_seq_id;

    /* Gather per process sequence id and defined the maximum sequence id */
    FOR_EACH(iproc, 1, dev->mpi_nprocs) {
      MPI_Request req;
      unsigned long tmp_seq_id;

      /* Asynchronously receive the sequence id of `iproc' */
      mutex_lock(dev->mpi_mutex);
      MPI(Irecv(&tmp_seq_id, 1, MPI_UNSIGNED_LONG, iproc,
        MPI_SDIS_MSG_RNG_PROXY_SEQUENCE_ID, MPI_COMM_WORLD, &req));
      mutex_unlock(dev->mpi_mutex);
      mpi_waiting_for_request(dev, &req);

      /* Define the maximum sequence id between all processes */
      max_seq_id = MMAX(max_seq_id, tmp_seq_id);
    }

    /* Flush the current sequence that is already consumed in addition to the
     * sequences queried by the other processes */
    nseqs_to_flush = 1/*Current sequence*/ + max_seq_id - proc_seq_id;
    res = ssp_rng_proxy_flush_sequences(proxy, nseqs_to_flush);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}
#endif

#ifndef SDIS_ENABLE_MPI
res_T
gather_res_T(struct sdis_device* dev, const res_T res)
{
  (void)dev;
  return res;
}
#else
res_T
gather_res_T(struct sdis_device* dev, const res_T proc_res)
{
  int32_t status;
  res_T res = proc_res;
  int iproc;
  ASSERT(dev);

  if(!dev->use_mpi) return proc_res;

  status = (int32_t)(proc_res);

  /* Send the local res status to all other processes */
  FOR_EACH(iproc, 0, dev->mpi_nprocs) {
    /* Do not send the res status to yourself */
    if(iproc == dev->mpi_rank) continue;

    mutex_lock(dev->mpi_mutex);
    MPI(Send(&status, 1, MPI_INT32_T, iproc, MPI_SDIS_MSG_RES_T,
      MPI_COMM_WORLD));
    mutex_unlock(dev->mpi_mutex);
  }

  /* Receive the res status of all other processes */
  res = proc_res;
  FOR_EACH(iproc, 0, dev->mpi_nprocs) {
    MPI_Request req;

    /* Do not receive the res status from yourself */
    if(iproc == dev->mpi_rank) continue;

    mutex_lock(dev->mpi_mutex);
    MPI(Irecv(&status, 1, MPI_INT32_T, iproc, MPI_SDIS_MSG_RES_T,
      MPI_COMM_WORLD, &req));
    mutex_unlock(dev->mpi_mutex);
    mpi_waiting_for_request(dev, &req);

    if(res == RES_OK && status != RES_OK) {
      res = (res_T)status;
    }
  }

  return res;
}

#endif

void
print_progress
  (struct sdis_device* dev,
   int32_t progress[],
   const char* label)
{
  ASSERT(dev && label);
#ifndef SDIS_ENABLE_MPI
  log_info(dev, "%s%3d%%%c", label, progress[0],
    dev->no_escape_sequence ? '\n' : '\r');
#else
  if(!dev->use_mpi) {
    log_info(dev, "%s%3d%%%c", label, progress[0],
      dev->no_escape_sequence ? '\n' : '\r');
  } else {
    int i;
    if(dev->mpi_rank != 0) return;
    mpi_fetch_progress(dev, progress);
    FOR_EACH(i, 0, dev->mpi_nprocs) {
      log_info(dev, "Process %d -- %s%3d%%%c", i, label, progress[i],
        i == dev->mpi_nprocs - 1 && !dev->no_escape_sequence ? '\r' : '\n');
    }
  }
#endif
}

void
print_progress_update
  (struct sdis_device* dev,
   int32_t progress[],
   const char* label)
{
  ASSERT(dev);
#ifndef SDIS_ENABLE_MPI
  print_progress(dev, progress, label);
#else
  if(!dev->use_mpi) {
    print_progress(dev, progress, label);
  } else {
    if(dev->mpi_rank != 0) {
      mpi_send_progress(dev, progress[0]);
    } else {
      mpi_fetch_progress(dev, progress);
      rewind_progress_printing(dev);
      print_progress(dev, progress, label);
    }
  }
#endif
}

void
print_progress_completion
  (struct sdis_device* dev,
   int32_t progress[],
   const char* label)
{
  ASSERT(dev);
  (void)dev, (void)progress, (void)label;

  /* Only print at 100% completion when MPI is enabled, because when last
   * printed non-master processes might still be running. When MPI is disabled,
   * 100% completion is printed during calculation */
#ifdef  SDIS_ENABLE_MPI
  if(dev->use_mpi && dev->mpi_rank == 0 && dev->mpi_nprocs > 1) {
    mpi_fetch_progress(dev, progress);
    rewind_progress_printing(dev);
    print_progress(dev, progress, label);

    /* When escape sequences are allowed, the last newline character of the
     * progress message is replaced with a carriage return. After the
     * calculation is complete, we therefore print an additional newline
     * character after this carriage return. */
    if(!dev->no_escape_sequence) {
      log_info(dev, "\n");
    }
  }
#endif
}

void
process_barrier(struct sdis_device* dev)
{
#ifndef SDIS_ENABLE_MPI
  (void)dev;
  return;
#else
  if(dev->use_mpi) {
    mpi_barrier(dev);
  }
#endif
}
