/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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
#include "sdis_green.h"
#include "sdis_misc.h"
#include "sdis_realisation.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>
#include <omp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Generic solve function
 ******************************************************************************/
static res_T
XD(solve_probe)
  (struct sdis_scene* scn,
   const size_t nrealisations,
   const double position[3],
   const double time_range[2],
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   const double Tarad, /* Ambient radiative temperature */
   const double Tref, /* Reference temperature */
   const int register_paths, /* Combination of enum sdis_heat_path_flag */
   struct sdis_green_function** out_green, /* May be NULL <=> No green func */
   struct sdis_estimator** out_estimator)
{
  struct sdis_medium* medium = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_green_function** greens = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  struct accum* accums = NULL;
  int64_t irealisation = 0;
  size_t i;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !position
  || fp_to_meter <= 0 || Tref < 0) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(!out_estimator && !out_green) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(out_estimator) {
    if(!time_range || time_range[0] < 0 || time_range[1] < time_range[0]
    || (time_range[1] > DBL_MAX && time_range[0] != time_range[1])) {
      res = RES_BAD_ARG;
      goto error;
    }
  }

#if SDIS_XD_DIMENSION == 2
  if(scene_is_2d(scn) == 0) { res = RES_BAD_ARG; goto error; }
#else
  if(scene_is_2d(scn) != 0) { res = RES_BAD_ARG; goto error; }
#endif

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*rngs));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs+i);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread accumulator */
  accums = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*accums));
  if(!accums) { res = RES_MEM_ERR; goto error; }

  /* Retrieve the medium in which the submitted position lies */
  res = scene_get_medium(scn, position, NULL, &medium);
  if(res != RES_OK) goto error;

  if(out_green) {
    /* Create the per thread green function */
    greens = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*greens));
    if(!greens) { res = RES_MEM_ERR; goto error; }
    FOR_EACH(i, 0, scn->dev->nthreads) {
      res = green_function_create(scn->dev, &greens[i]);
      if(res != RES_OK) goto error;
    }

  }

  /* Create the estimator */
  if(out_estimator) {
    res = estimator_create(scn->dev, SDIS_ESTIMATOR_TEMPERATURE, &estimator);
    if(res != RES_OK) goto error;
  }

  /* Here we go! Launch the Monte Carlo estimation */
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    struct accum* accum = &accums[ithread];
    struct green_path_handle* pgreen_path = NULL;
    struct green_path_handle green_path = GREEN_PATH_HANDLE_NULL;
    struct sdis_heat_path* pheat_path = NULL;
    struct sdis_heat_path heat_path;
    double w = NaN;
    double time;
    res_T res_local;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    if(!out_green) {
      time = sample_time(rng, time_range);
      if(register_paths) {
        heat_path_init(scn->dev->allocator, &heat_path);
        pheat_path = &heat_path;
      }
    } else {
      /* Do not take care of the submitted time when registering the green
       * function. Simply takes 0 as relative time */
      time = 0;
      res_local = green_function_create_path(greens[ithread], &green_path);
      if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); continue; }

      pgreen_path = &green_path;
    }

    res_local = XD(probe_realisation)((size_t)irealisation, scn, rng, medium,
      position, time, fp_to_meter, Tarad, Tref, pgreen_path, pheat_path, &w);
    if(res_local == RES_OK) {
      accum->sum += w;
      accum->sum2 += w*w;
      ++accum->count;
    } else if(res_local != RES_BAD_OP) {
      ATOMIC_SET(&res, res_local);
      continue;
    }

    if(pheat_path) {
      pheat_path->status = res_local == RES_OK
        ? SDIS_HEAT_PATH_SUCCEED
        : SDIS_HEAT_PATH_FAILED;

      /* Check if the path must be saved regarding the register_paths mask */
      if(!(register_paths & (int)pheat_path->status)) {
        heat_path_release(pheat_path);
      } else { /* Register the sampled path */
        res_local = estimator_add_and_release_heat_path(estimator, pheat_path);
        if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); continue; }
      }
    }
  }
  if(res != RES_OK) goto error;

  /* Setup the estimated temperature */
  if(out_estimator) {
    struct accum acc;
    sum_accums(accums, scn->dev->nthreads, &acc);
    estimator_setup_realisations_count(estimator, nrealisations, acc.count);
    estimator_setup_temperature(estimator, acc.sum, acc.sum2);
  }

  if(out_green) {
    green = greens[0]; /* Return the green of the 1st thread */
    greens[0] = NULL; /* Make invalid the 1st green for 'on exit' clean up*/
    FOR_EACH(i, 1, scn->dev->nthreads) { /* Merge the per thread green */
      res = green_function_merge_and_clear(green, greens[i]);
      if(res != RES_OK) goto error;
    }

    /* Finalize the estimated green */
    res = green_function_finalize(green, rng_proxy);
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
  if(accums) MEM_RM(scn->dev->allocator, accums);
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

