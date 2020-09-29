/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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
 * Generic solve function
 ******************************************************************************/
static res_T
XD(solve_probe)
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_args* args,
   struct sdis_green_function** out_green, /* May be NULL <=> No green func */
   struct sdis_estimator** out_estimator) /* May be NULL <=> No estimator */
{
  struct sdis_medium* medium = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_green_function** greens = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  struct accum* acc_temps = NULL;
  struct accum* acc_times = NULL;
  size_t nrealisations = 0;
  int64_t irealisation = 0;
  size_t i;
  int progress = 0;
  int register_paths = SDIS_HEAT_PATH_NONE;
  ATOMIC nsolved_realisations = 0;
  ATOMIC res = RES_OK;

  if(!scn || !args || !args->nrealisations || args->nrealisations > INT64_MAX
  || args->fp_to_meter <= 0) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(!out_estimator && !out_green) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(args->time_range[0] < 0
    || args->time_range[1] < args->time_range[0]
    || (args->time_range[1] > DBL_MAX
       && args->time_range[0] != args->time_range[1]))
  {
    res = RES_BAD_ARG;
    goto error;
  }

#if SDIS_XD_DIMENSION == 2
  if(scene_is_2d(scn) == 0) { res = RES_BAD_ARG; goto error; }
#else
  if(scene_is_2d(scn) != 0) { res = RES_BAD_ARG; goto error; }
#endif

  /* Create the proxy RNG */
  if(args->rng_state) {
    res = ssp_rng_proxy_create_from_rng(scn->dev->allocator, args->rng_state,
      scn->dev->nthreads, &rng_proxy);
    if(res != RES_OK) goto error;
  } else {
    res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
      scn->dev->nthreads, &rng_proxy);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread RNG */
  rngs = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*rngs));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs+i);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread accumulators */
  acc_temps = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(*acc_temps));
  if(!acc_temps) { res = RES_MEM_ERR; goto error; }
  acc_times = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(*acc_times));
  if(!acc_times) { res = RES_MEM_ERR; goto error; }

  /* Retrieve the medium in which the submitted position lies */
  res = scene_get_medium(scn, args->position, NULL, &medium);
  if(res != RES_OK) goto error;

  /* Create the per thread green function */
  if(out_green) {
    greens = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*greens));
    if(!greens) { res = RES_MEM_ERR; goto error; }
    FOR_EACH(i, 0, scn->dev->nthreads) {
      res = green_function_create(scn, &greens[i]);
      if(res != RES_OK) goto error;
    }
  }

  /* Create the estimator */
  if(out_estimator) {
    res = estimator_create(scn->dev, SDIS_ESTIMATOR_TEMPERATURE, &estimator);
    if(res != RES_OK) goto error;
  }

  /* Here we go! Launch the Monte Carlo estimation */
  nrealisations = args->nrealisations;
  register_paths = out_estimator ? args->register_paths : SDIS_HEAT_PATH_NONE;
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    struct time t0, t1;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    struct accum* acc_temp = &acc_temps[ithread];
    struct accum* acc_time = &acc_times[ithread];
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

    /* Begin time registration */
    time_current(&t0);

    time = sample_time(rng, args->time_range);

    if(out_green) {
      res_local = green_function_create_path(greens[ithread], &green_path);
      if(res_local != RES_OK) {
        ATOMIC_SET(&res, res_local);
        goto error_it;
      }
      pgreen_path = &green_path;
    }
    if(register_paths) {
      heat_path_init(scn->dev->allocator, &heat_path);
      pheat_path = &heat_path;
    }

    res_simul = XD(probe_realisation)((size_t)irealisation, scn, rng, medium,
      args->position, time, args->fp_to_meter,
      args->ambient_radiative_temperature, args->reference_temperature,
      pgreen_path, pheat_path, &w);

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

    /* Update progress */
    n = (size_t)ATOMIC_INCR(&nsolved_realisations);
    pcent = (int)((double)n * 100.0 / (double)nrealisations + 0.5/*round*/);
    #pragma omp critical
    if(pcent > progress) {
      progress = pcent;
      log_info(scn->dev, "Solving probe temperature: %3d%%\r", progress);
    }
  exit_it:
    if(pheat_path) heat_path_release(pheat_path);
    continue;
  error_it:
    goto exit_it;
  }
  if(res != RES_OK) goto error;

  /* Add a new line after the progress status */
  log_info(scn->dev, "Solving probe temperature: %3d%%\n", progress);

  /* Setup the estimated temperature and per realisation time */
  if(out_estimator) {
    struct accum acc_temp;
    struct accum acc_time;

    sum_accums(acc_temps, scn->dev->nthreads, &acc_temp);
    sum_accums(acc_times, scn->dev->nthreads, &acc_time);
    ASSERT(acc_temp.count == acc_time.count);

    estimator_setup_realisations_count(estimator, nrealisations, acc_temp.count);
    estimator_setup_temperature(estimator, acc_temp.sum, acc_temp.sum2);
    estimator_setup_realisation_time(estimator, acc_time.sum, acc_time.sum2);
    res = estimator_save_rng_state(estimator, rng_proxy);
    if(res != RES_OK) goto error;
  }

  if(out_green) {
    struct accum acc_time;

    /* Redux the per thread green function into the green of the 1st thread */
    green = greens[0]; /* Return the green of the 1st thread */
    greens[0] = NULL; /* Make invalid the 1st green for 'on exit' clean up*/
    res = green_function_redux_and_clear(green, greens+1, scn->dev->nthreads-1);
    if(res != RES_OK) goto error;

    /* Finalize the estimated green */
    sum_accums(acc_times, scn->dev->nthreads, &acc_time);
    res = green_function_finalize(green, rng_proxy, &acc_time);
    if(res != RES_OK) goto error;
  }

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads)  {
      if(rngs[i]) SSP(rng_ref_put(rngs[i]));
    }
    MEM_RM(scn->dev->allocator, rngs);
  }
  if(greens) {
    FOR_EACH(i, 0, scn->dev->nthreads) {
      if(greens[i]) SDIS(green_function_ref_put(greens[i]));
    }
    MEM_RM(scn->dev->allocator, greens);
  }
  if(acc_temps) MEM_RM(scn->dev->allocator, acc_temps);
  if(acc_times) MEM_RM(scn->dev->allocator, acc_times);
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_green) *out_green = green;
  if(out_estimator) *out_estimator = estimator;
  return (res_T)res;
error:
  if(green) {
    SDIS(green_function_ref_put(green));
    green = NULL;
  }
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

#include "sdis_Xd_end.h"

