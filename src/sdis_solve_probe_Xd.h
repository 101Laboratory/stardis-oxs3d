/* Copyright (C) 2016-2022 |Meso|Star> (contact@meso-star.com)
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

#include "sdis_c.h"
#include "sdis_device_c.h"
#include "sdis_estimator_c.h"
#include "sdis_log.h"
#include "sdis_green.h"
#include "sdis_misc.h"
#include "sdis_realisation.h"
#include "sdis_scene_c.h"

#include <rsys/clock_time.h>
#include <star/ssp.h>
#include <omp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Helper function
 ******************************************************************************/
#ifndef SDIS_SOLVE_PROBE_XD_H
#define SDIS_SOLVE_PROBE_XD_H

static INLINE res_T
check_solve_probe_args(const struct sdis_solve_probe_args* args)
{
  if(!args) return RES_BAD_ARG;

  /* Check #realisations */
  if(!args->nrealisations || args->nrealisations > INT64_MAX) {
    return RES_BAD_ARG;
  }

  /* Check time range */
  if(args->time_range[0] < 0 || args->time_range[1] < args->time_range[0]) {
    return RES_BAD_ARG;
  }
  if(args->time_range[1] > DBL_MAX
  && args->time_range[0] != args->time_range[1]) {
    return RES_BAD_ARG;
  }

  /* Check picard order */
  if(args->picard_order < 1) {
    return RES_BAD_ARG;
  }

  return RES_OK;
}

#endif /* SDIS_SOLVE_PROBE_XD_H */

/*******************************************************************************
 * Generic solve function
 ******************************************************************************/
static res_T
XD(solve_probe)
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_args* args,
   struct sdis_green_function** out_green, /* May be NULL <=> No green func */
   struct sdis_estimator** out_estimator) /* May be NULL <=> No estimator */
{
  /* Time registration */
  struct time time0, time1;
  char buf[128]; /* Temporary buffer used to store formated time */

  /* Device variables */
  struct mem_allocator* allocator = NULL;
  size_t nthreads = 0;

  /* Stardis variables */
  struct sdis_medium* medium = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_green_function** per_thread_green = NULL;

  /* Random Number generator */
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** per_thread_rng = NULL;

  /* Miscellaneous */
  struct accum* per_thread_acc_temp = NULL;
  struct accum* per_thread_acc_time = NULL;
  size_t nrealisations = 0;
  int64_t irealisation = 0;
  int32_t* progress = NULL; /* Per process progress bar */
  int register_paths = SDIS_HEAT_PATH_NONE;
  int is_master_process = 1;
  ATOMIC nsolved_realisations = 0;
  ATOMIC res = RES_OK;

  if(!scn) { res = RES_BAD_ARG; goto error; }
  if(!out_estimator && !out_green) { res = RES_BAD_ARG; goto error; }
  res = check_solve_probe_args(args);
  if(res != RES_OK) goto error;
  res = XD(scene_check_dimensionality)(scn);
  if(res != RES_OK) goto error;

  if(out_green && args->picard_order != 1) {
    log_err(scn->dev, "%s: the evaluation of the green function does not make "
      "sense when dealing with the non-linearities of the system; i.e. picard "
      "order must be set to 1 while it is currently set to %lu.\n",
      FUNC_NAME, (unsigned long)args->picard_order);
    res = RES_BAD_ARG;
    goto error;
  }

#ifdef SDIS_ENABLE_MPI
  is_master_process = !scn->dev->use_mpi || scn->dev->mpi_rank == 0;
#endif

  nthreads = scn->dev->nthreads;
  allocator = scn->dev->allocator;

  /* Create the per thread RNGs */
  res = create_per_thread_rng
    (scn->dev, args->rng_state, &rng_proxy, &per_thread_rng);
  if(res != RES_OK) goto error;

  /* Allocate the per process progress status */
  res = alloc_process_progress(scn->dev, &progress);
  if(res != RES_OK) goto error;

  /* Create the per thread accumulators */
  per_thread_acc_temp = MEM_CALLOC(allocator, nthreads, sizeof(struct accum));
  per_thread_acc_time = MEM_CALLOC(allocator, nthreads, sizeof(struct accum));
  if(!per_thread_acc_temp) { res = RES_MEM_ERR; goto error; }
  if(!per_thread_acc_time) { res = RES_MEM_ERR; goto error; }

  /* Retrieve the medium in which the submitted position lies */
  res = scene_get_medium(scn, args->position, NULL, &medium);
  if(res != RES_OK) goto error;

  /* Create the per thread green function */
  if(out_green) {
    res = create_per_thread_green_function(scn, &per_thread_green);
    if(res != RES_OK) goto error;
  }

  /* Create the estimator on the master process only. No estimator is needed
   * for non master process */
  if(out_estimator && is_master_process) {
    res = estimator_create(scn->dev, SDIS_ESTIMATOR_TEMPERATURE, &estimator);
    if(res != RES_OK) goto error;
  }

  /* Synchronise the processes */
  process_barrier(scn->dev);

  #define PROGRESS_MSG "Solving probe temperature: "
  print_progress(scn->dev, progress, PROGRESS_MSG);

  /* Begin time registration of the computation */
  time_current(&time0);

  /* Here we go! Launch the Monte Carlo estimation */
  nrealisations = compute_process_realisations_count(scn->dev, args->nrealisations);
  register_paths = out_estimator && is_master_process
    ? args->register_paths : SDIS_HEAT_PATH_NONE;
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    struct probe_realisation_args realis_args = PROBE_REALISATION_ARGS_NULL;
    struct time t0, t1;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = per_thread_rng[ithread];
    struct accum* acc_temp = &per_thread_acc_temp[ithread];
    struct accum* acc_time = &per_thread_acc_time[ithread];
    struct green_path_handle* pgreen_path = NULL;
    struct green_path_handle green_path = GREEN_PATH_HANDLE_NULL;
    struct sdis_heat_path* pheat_path = NULL;
    struct sdis_heat_path heat_path;
    double w = NaN;
    double time;
    size_t n;
    int pcent;
    res_T res_local = RES_OK;
    res_T res_simul = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    /* Begin time registration of the realisation */
    time_current(&t0);

    time = sample_time(rng, args->time_range);

    if(out_green) {
      res_local = green_function_create_path
        (per_thread_green[ithread], &green_path);
      if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); goto error_it; }
      pgreen_path = &green_path;
    }

    if(register_paths) {
      heat_path_init(scn->dev->allocator, &heat_path);
      pheat_path = &heat_path;
    }

    /* Invoke the probe realisation */
    realis_args.rng = rng;
    realis_args.medium = medium;
    realis_args.time = time;
    realis_args.picard_order = args->picard_order;
    realis_args.green_path = pgreen_path;
    realis_args.heat_path = pheat_path;
    realis_args.irealisation = (size_t)irealisation;
    dX(set)(realis_args.position, args->position);
    res_simul = XD(probe_realisation)(scn, &realis_args, &w);

    /* Handle fatal error */
    if(res_simul != RES_OK && res_simul != RES_BAD_OP) {
      ATOMIC_SET(&res, res_simul);
      goto error_it;
    }

    if(pheat_path) {
      pheat_path->status = res_simul == RES_OK
        ? SDIS_HEAT_PATH_SUCCESS
        : SDIS_HEAT_PATH_FAILURE;

      /* Check if the path must be saved regarding the register_paths mask */
      if(!(register_paths & (int)pheat_path->status)) {
        heat_path_release(pheat_path);
        pheat_path = NULL;
      } else { /* Register the sampled path */
        res_local = estimator_add_and_release_heat_path(estimator, pheat_path);
        if(res_local != RES_OK) {
          ATOMIC_SET(&res, res_local);
          goto error_it;
        }
        pheat_path = NULL;
      }
    }

    /* Stop time registration */
    time_sub(&t0, time_current(&t1), &t0);

    /* Update accumulators */
    if(res_simul == RES_OK) {
      const double usec = (double)time_val(&t0, TIME_NSEC) * 0.001;
      acc_temp->sum += w;    acc_temp->sum2 += w*w;       ++acc_temp->count;
      acc_time->sum += usec; acc_time->sum2 += usec*usec; ++acc_time->count;
    }

    /* Update the progress status */
    n = (size_t)ATOMIC_INCR(&nsolved_realisations);
    pcent = (int)((double)n * 100.0 / (double)nrealisations + 0.5/*round*/);

    #pragma omp critical
    if(pcent > progress[0]) {
      progress[0] = pcent;
      print_progress_update(scn->dev, progress, PROGRESS_MSG);
    }

  exit_it:
    if(pheat_path) heat_path_release(pheat_path);
    continue;
  error_it:
    goto exit_it;
  }

  /* Synchronise processes */
  process_barrier(scn->dev);

  res = gather_res_T(scn->dev, (res_T)res);
  if(res != RES_OK) goto error;

  print_progress_update(scn->dev, progress, PROGRESS_MSG);
  log_info(scn->dev, "\n");
  #undef PROGRESS_MSG

  /* Report computation time */
  time_sub(&time0, time_current(&time1), &time0);
  time_dump(&time0, TIME_ALL, NULL, buf, sizeof(buf));
  log_info(scn->dev, "Probe temperature solved in %s.\n", buf);

  /* Gather the RNG proxy sequence IDs and ensure that the RNG proxy state of
   * the master process is greater than the RNG proxy state of all other
   * processes */
  res = gather_rng_proxy_sequence_id(scn->dev, rng_proxy);
  if(res != RES_OK) goto error;

  /* Setup the estimated values */
  if(out_estimator) {
    struct accum acc_temp;
    struct accum acc_time;

    time_current(&time0);

    #define GATHER_ACCUMS(Msg, Acc) {                                          \
      res = gather_accumulators(scn->dev, Msg, per_thread_##Acc, &Acc);        \
      if(res != RES_OK) goto error;                                            \
    } (void)0
    GATHER_ACCUMS(MPI_SDIS_MSG_ACCUM_TEMP, acc_temp);
    GATHER_ACCUMS(MPI_SDIS_MSG_ACCUM_TEMP, acc_time);
    #undef GATHER_ACCUMS

    time_sub(&time0, time_current(&time1), &time0);
    time_dump(&time0, TIME_ALL, NULL, buf, sizeof(buf));
    log_info(scn->dev, "Accumulators gathered in %s.\n",  buf);

    /* Return an estimator only on master process */
    if(is_master_process) {
      ASSERT(acc_temp.count == acc_time.count);
      estimator_setup_realisations_count(estimator, args->nrealisations, acc_temp.count);
      estimator_setup_temperature(estimator, acc_temp.sum, acc_temp.sum2);
      estimator_setup_realisation_time(estimator, acc_time.sum, acc_time.sum2);
      res = estimator_save_rng_state(estimator, rng_proxy);
      if(res != RES_OK) goto error;
    }
  }

  /* Setup the green function */
  if(out_green) {
    time_current(&time0);

    res = gather_green_functions
      (scn, rng_proxy, per_thread_green, per_thread_acc_time, &green);
    if(res != RES_OK) goto error;

    time_sub(&time0, time_current(&time1), &time0);
    time_dump(&time0, TIME_ALL, NULL, buf, sizeof(buf));
    log_info(scn->dev, "Green functions gathered in %s.\n", buf);

    /* Return a green function only on master process */
    if(!is_master_process) {
      SDIS(green_function_ref_put(green));
      green = NULL;
    }
  }

exit:
  if(per_thread_rng) release_per_thread_rng(scn->dev, per_thread_rng);
  if(per_thread_green) release_per_thread_green_function(scn, per_thread_green);
  if(progress) free_process_progress(scn->dev, progress);
  if(per_thread_acc_temp) MEM_RM(scn->dev->allocator, per_thread_acc_temp);
  if(per_thread_acc_time) MEM_RM(scn->dev->allocator, per_thread_acc_time);
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_green) *out_green = green;
  if(out_estimator) *out_estimator = estimator;
  return (res_T)res;
error:
  if(estimator) { SDIS(estimator_ref_put(estimator)); estimator = NULL; }
  if(green) { SDIS(green_function_ref_put(green)); green = NULL; }
  goto exit;
}

#include "sdis_Xd_end.h"

