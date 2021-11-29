/* Copyright (C) 2016-2021 |Meso|Star> (contact@meso-star.com)
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

#include <rsys/clock_time.h>
#include <rsys/mem_allocator.h>

/* Number random numbers in a sequence, i.e. number of consecutive random
 * numbers that can be used by a thread */
#define RNG_SEQUENCE_SIZE 100000

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
#ifdef SDIS_ENABLE_MPI
static void
rewind_progress_printing(struct sdis_device* dev)
{
  size_t i;
  if(!dev->use_mpi || dev->mpi_nprocs == 1) return;
  FOR_EACH(i, 0, dev->mpi_nprocs-1) {
    log_info(dev, "\033[1A\r"); /* Move up */
  }
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
  proxy_args.type = SSP_RNG_MT19937_64;
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
  if(rngs) { destroy_per_thread_rng(dev, rngs); rngs = NULL; }
  if(proxy) { SSP(rng_proxy_ref_put(proxy)); proxy = NULL; }
  goto exit;
}

void
destroy_per_thread_rng(struct sdis_device* dev, struct ssp_rng* rngs[])
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
    res = green_function_create(scn, &greens[i]);
    if(res != RES_OK) goto error;
  }

exit:
  *out_greens = greens;
  return res;
error:
  if(greens) {
    destroy_per_thread_green_function(scn, greens);
    greens = NULL;
  }
  goto exit;
}

void
destroy_per_thread_green_function
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
compute_process_realisations_count
  (const struct sdis_device* dev,
   const size_t nrealisations)
{
#ifndef SDIS_ENABLE_MPI
  (void)dev, (void)nrealisations;
  return nrealisations;
#else
  size_t per_process_nrealisations = 0;
  size_t remaining_nrealisations = 0;
  ASSERT(dev);

  if(!dev->use_mpi) return nrealisations;

  /* Compute minimum the number of realisations on each process */
  per_process_nrealisations = nrealisations / (size_t)dev->mpi_nprocs;

  /* Define the remaining number of realisations that are not handle by one
   * process */
  remaining_nrealisations =
    nrealisations
  - per_process_nrealisations * (size_t)dev->mpi_nprocs;

  /* Distribute the remaining realisations onto the processes */
  if((size_t)dev->mpi_rank >= remaining_nrealisations) {
    return per_process_nrealisations;
  } else {
    return per_process_nrealisations + 1;
  }
#endif
}

#ifndef SDIS_ENABLE_MPI
res_T
gather_accumulators
  (struct sdis_device* dev,
   const struct accum* per_thread_acc_temp,
   const struct accum* per_thread_acc_time,
   struct accum* acc_temp,
   struct accum* acc_time)
{
  ASSERT(dev);
  /* Gather thread accumulators */
  sum_accums(per_thread_acc_temp, dev->nthreads, acc_temp);
  sum_accums(per_thread_acc_time, dev->nthreads, acc_time);
  return RES_OK;
}
#endif

#ifdef SDIS_ENABLE_MPI
res_T
gather_accumulators
  (struct sdis_device* dev,
   const struct accum* per_thread_acc_temp,
   const struct accum* per_thread_acc_time,
   struct accum* acc_temp,
   struct accum* acc_time)
{
  char buf[128];
  struct time t0, t1;
  struct accum* per_proc_acc_temp = NULL;
  struct accum* per_proc_acc_time = NULL;
  size_t nprocs = 0;
  res_T res = RES_OK;
  ASSERT(dev && per_thread_acc_temp && per_thread_acc_time);
  ASSERT(acc_temp && acc_time);

  time_current(&t0);

  if(!dev->use_mpi) {
    /* Gather thread accumulators */
    sum_accums(per_thread_acc_temp, dev->nthreads, acc_temp);
    sum_accums(per_thread_acc_time, dev->nthreads, acc_time);
    goto exit;
  }

  nprocs = (size_t)dev->mpi_nprocs;
  per_proc_acc_temp = MEM_CALLOC(dev->allocator, nprocs, sizeof(struct accum));
  per_proc_acc_time = MEM_CALLOC(dev->allocator, nprocs, sizeof(struct accum));
  if(!per_proc_acc_temp) { res = RES_MEM_ERR; goto error; }
  if(!per_proc_acc_time) { res = RES_MEM_ERR; goto error; }

  /* Gather thread accumulators */
  sum_accums(per_thread_acc_temp, dev->nthreads, &per_proc_acc_temp[0]);
  sum_accums(per_thread_acc_time, dev->nthreads, &per_proc_acc_time[0]);

  /* Non master process */
  if(dev->mpi_rank != 0) {

    /* Send the temperature/time accumulator to the master process */
    mutex_lock(dev->mpi_mutex);
    MPI(Send(&per_proc_acc_temp[0], sizeof(per_proc_acc_temp[0]), MPI_CHAR,
      0/*Dst*/, MPI_SDIS_MSG_ACCUM_TEMP, MPI_COMM_WORLD));
    MPI(Send(&per_proc_acc_time[0], sizeof(per_proc_acc_time[0]), MPI_CHAR,
      0/*Dst*/, MPI_SDIS_MSG_ACCUM_TIME, MPI_COMM_WORLD));
    mutex_unlock(dev->mpi_mutex);

    *acc_temp = per_proc_acc_temp[0];
    *acc_time = per_proc_acc_time[0];

  /* Master process */
  } else {
    int iproc;

    /* Gather process accumulators */
    FOR_EACH(iproc, 1, dev->mpi_nprocs) {
      MPI_Request req;

      /* Asynchronously receive the temperature accumulator of `iproc' */
      mutex_lock(dev->mpi_mutex);
      MPI(Irecv(&per_proc_acc_temp[iproc], sizeof(per_proc_acc_temp[iproc]),
        MPI_CHAR, iproc, MPI_SDIS_MSG_ACCUM_TEMP, MPI_COMM_WORLD, &req));
      mutex_unlock(dev->mpi_mutex);
      mpi_waiting_for_request(dev, &req);

      /* Asynchronously receive the time accumulator of `iproc' */
      mutex_lock(dev->mpi_mutex);
      MPI(Irecv(&per_proc_acc_time[iproc], sizeof(per_proc_acc_time[iproc]),
        MPI_CHAR, iproc, MPI_SDIS_MSG_ACCUM_TIME, MPI_COMM_WORLD, &req));
      mutex_unlock(dev->mpi_mutex);
      mpi_waiting_for_request(dev, &req);
    }

    /* Sum the process accumulators */
    sum_accums(per_proc_acc_temp, (size_t)dev->mpi_nprocs, acc_temp);
    sum_accums(per_proc_acc_time, (size_t)dev->mpi_nprocs, acc_time);
  }

exit:
  if(res == RES_OK) {
    time_sub(&t0, time_current(&t1), &t0);
    time_dump(&t0, TIME_ALL, NULL, buf, sizeof(buf));
    log_info(dev, "Accumulators gathered in %s.\n",  buf);
  }
  if(per_proc_acc_temp) MEM_RM(dev->allocator, per_proc_acc_temp);
  if(per_proc_acc_time) MEM_RM(dev->allocator, per_proc_acc_time);
  return res;
error:
  goto exit;
}
#endif /* SDIS_ENABLE_MPI */

res_T
setup_estimator
  (struct sdis_estimator* estimator,
   const struct ssp_rng_proxy* proxy,
   const struct accum* acc_temp,
   const struct accum* acc_time,
   const size_t nrealisations)
{
  res_T res = RES_OK;
  ASSERT(estimator && proxy && acc_temp && acc_time);

  estimator_setup_realisations_count(estimator, nrealisations, acc_temp->count);
  estimator_setup_temperature(estimator, acc_temp->sum, acc_temp->sum2);
  estimator_setup_realisation_time(estimator, acc_time->sum, acc_time->sum2);

  /* TODO correctly handle RNG state with MPI. Currently, we only store the RNG
   * proxy state of the master process, but non-master processes can rely on
   * much more advanced seeds. Therefore, rerun the simulation with the saved
   * RNG state can lead to non-master processes generating random numbers that
   * were already generated at the previous run. */
  res = estimator_save_rng_state(estimator, proxy);
  if(res != RES_OK) goto error;

#ifdef SDIS_ENABLE_MPI
  if(estimator->dev->use_mpi) {
    log_warn(estimator->dev,
      "The estimator RNG state is not well defined when MPI is used.\n");
  }
#endif

exit:
  return res;
error:
  goto exit;
}

void
print_progress
  (struct sdis_device* dev,
   int32_t progress[],
   const char* label)
{
  ASSERT(dev && label);
#ifndef SDIS_ENABLE_MPI
  log_info(dev, "%s%3d%%\r", label, progress[0]);
#else
  if(!dev->use_mpi) {
    log_info(dev, "%s%3d%%\r", label, progress[0]);
  } else {
    if(dev->mpi_rank != 0) return;
    if(dev->mpi_nprocs == 1) {
      log_info(dev, "%s%3d%%\r", label, progress[0]);
    } else {
      int i;
      mpi_fetch_progress(dev, progress);
      FOR_EACH(i, 0, dev->mpi_nprocs) {
        log_info(dev, "Process %d -- %s%3d%%%c",
          i, label, progress[i], i == dev->mpi_nprocs - 1 ? '\r' : '\n');
      }
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
waiting_for_process_completion(struct sdis_device* dev)
{
#ifndef SDIS_ENABLE_MPI
  (void)dev;
  return;
#else
  if(dev->use_mpi) {
    mpi_synchronise_processes(dev);
  }
#endif
}
