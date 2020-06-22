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
#include "sdis_realisation.h"
#include "sdis_scene_c.h"

#include <rsys/algorithm.h>
#include <rsys/clock_time.h>
#include <rsys/dynamic_array.h>

#include "sdis_Xd_begin.h"

#ifndef SDIS_SOLVE_MEDIUM_XD_H
#define SDIS_SOLVE_MEDIUM_XD_H

/*
 * Define the data structures and functions that are not generic to the
 * SDIS_XD_DIMENSION parameter
 */

struct enclosure_cumul {
  const struct enclosure* enc;
  double cumul;
};

/* Define the darray_enclosure_cumul dynamic array */
#define DARRAY_NAME enclosure_cumul
#define DARRAY_DATA struct enclosure_cumul
#include <rsys/dynamic_array.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
cmp_double_to_enc_cumuls(const void* a, const void* b)
{
  const double key = *(const double*)a;
  const struct enclosure_cumul* enc_cumul = (const struct enclosure_cumul*)b;
  if(key < enc_cumul->cumul) return -1;
  if(key > enc_cumul->cumul) return +1;
  return 0;
}

static res_T
compute_medium_enclosure_cumulative
  (struct sdis_scene* scn,
   const struct sdis_medium* mdm,
   struct darray_enclosure_cumul* cumul)
{
  struct htable_enclosure_iterator it, end;
  double accum = 0;
  res_T res = RES_OK;
  ASSERT(scn && mdm && cumul);

  darray_enclosure_cumul_clear(cumul);

  htable_enclosure_begin(&scn->enclosures, &it);
  htable_enclosure_end(&scn->enclosures, &end);
  while(!htable_enclosure_iterator_eq(&it, &end)) {
    struct enclosure_cumul enc_cumul;
    const struct enclosure* enc = htable_enclosure_iterator_data_get(&it);
    htable_enclosure_iterator_next(&it);

    if(sdis_medium_get_id(mdm) != enc->medium_id) continue;

    accum += enc->V;
    enc_cumul.enc = enc;
    enc_cumul.cumul = accum;
    res = darray_enclosure_cumul_push_back(cumul, &enc_cumul);
    if(res != RES_OK) goto error;
  }

  if(darray_enclosure_cumul_size_get(cumul) == 0) {
    log_err(scn->dev,
      "%s: there is no enclosure that encompasses the submitted medium.\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  darray_enclosure_cumul_clear(cumul);
  goto exit;
}

static const struct enclosure*
sample_medium_enclosure
  (const struct darray_enclosure_cumul* cumul, struct ssp_rng* rng)
{
  const struct enclosure_cumul* enc_cumuls = NULL;
  const struct enclosure_cumul* enc_cumul_found = NULL;
  double r;
  size_t i;
  size_t sz;
  ASSERT(cumul && rng && darray_enclosure_cumul_size_get(cumul));

  sz = darray_enclosure_cumul_size_get(cumul);
  enc_cumuls = darray_enclosure_cumul_cdata_get(cumul);
  if(sz == 1) {
    enc_cumul_found = enc_cumuls;
  } else {
    /* Generate an uniform random number in [0, cumul[ */
    r = ssp_rng_canonical(rng);
    r = r * enc_cumuls[sz-1].cumul;

    enc_cumul_found = search_lower_bound
      (&r, enc_cumuls, sz, sizeof(*enc_cumuls), cmp_double_to_enc_cumuls);
    ASSERT(enc_cumul_found);

    /* search_lower_bound returns the first entry that is not less than `r'.
     * The following code discards entries that are also equal to `r'. */
    i = (size_t)(enc_cumul_found - enc_cumuls);
    while(enc_cumuls[i].cumul == r && i < sz) ++i;
    ASSERT(i < sz);

    enc_cumul_found = enc_cumuls + i;
  }
  return enc_cumul_found->enc;
}

#endif /* !SDIS_SOLVE_MEDIUM_XD_H */

/*******************************************************************************
 * Helper functions generic to the SDIS_XD_DIMENSION parameter
 ******************************************************************************/
static res_T
XD(sample_enclosure_position)
  (const struct enclosure* enc,
   struct ssp_rng* rng,
   double pos[DIM])
{
  const size_t MAX_NCHALLENGES = 1000;
  float lower[DIM], upper[DIM];
  size_t ichallenge;
  size_t i;
  res_T res = RES_OK;
  ASSERT(enc && rng && pos);

  SXD(scene_view_get_aabb(enc->sXd(view), lower, upper));

  FOR_EACH(i, 0, DIM) {
    if(lower[i] > upper[i] || eq_epsf(lower[i], upper[i], 1.e-6f)) {
      res = RES_BAD_ARG; /* Degenerated enclosure */
      goto error;
    }
  }

  FOR_EACH(ichallenge, 0, MAX_NCHALLENGES) {
    struct sXd(hit) hit = SXD_HIT_NULL;
    const float dir[3] = {1,0,0};
    const float range[2] = {0, FLT_MAX};
    float org[DIM];

    /* Generate an uniform position into the enclosure AABB */
    FOR_EACH(i, 0, DIM) {
      org[i] = ssp_rng_uniform_float(rng, lower[i], upper[i]);
    }

    /* Check that pos lies into the enclosure; trace a ray and check that it
     * hits something and that the normal points towards the traced ray
     * direction (enclosure normals point inword the enclosure) */
    SXD(scene_view_trace_ray(enc->sXd(view), org, dir, range, NULL, &hit));
    if(!SXD_HIT_NONE(&hit) && fX(dot)(dir, hit.normal) < 0) {
      dX_set_fX(pos, org);
      break;
    }
  }

  if(ichallenge >= MAX_NCHALLENGES) {
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
static res_T
XD(solve_medium)
  (struct sdis_scene* scn,
   const size_t nrealisations,
   struct sdis_medium* mdm,
   const double time_range[2],
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   const double Tarad, /* Ambient radiative temperature */
   const double Tref, /* Reference temperature */
   const int register_paths, /* Combination of enum sdis_heat_path_flag */
   struct sdis_green_function** out_green, /* May be NULL <=> No green func */
   struct sdis_estimator** out_estimator) /* May be NULL <=> No estimator */
{
  struct darray_enclosure_cumul cumul;
  struct sdis_green_function* green = NULL;
  struct sdis_green_function** greens = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  struct sdis_estimator* estimator = NULL;
  struct accum* acc_temps = NULL;
  struct accum* acc_times = NULL;
  int64_t irealisation;
  int cumul_is_init = 0;
  size_t i;
  int progress = 0;
  ATOMIC nsolved_realisations = 0;
  ATOMIC res = RES_OK;

  if(!scn || !mdm || !nrealisations || nrealisations > INT64_MAX
  || fp_to_meter <= 0 || Tref <  0) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(!out_estimator && !out_green) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(out_estimator) {
    if(!time_range || time_range[0] < 0 || time_range[0] > time_range[1]
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

  /* Create the per thread accumulators */
  acc_temps = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(*acc_temps));
  if(!acc_temps) { res = RES_MEM_ERR; goto error; }
  acc_times = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(*acc_times));
  if(!acc_times) { res = RES_MEM_ERR; goto error; }

  /* Compute the enclosure cumulative */
  darray_enclosure_cumul_init(scn->dev->allocator, &cumul);
  cumul_is_init = 1;
  res = compute_medium_enclosure_cumulative(scn, mdm, &cumul);
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
    const struct enclosure* enc = NULL;
    struct sdis_heat_path* pheat_path = NULL;
    struct sdis_heat_path heat_path;
    double weight;
    double time;
    double pos[DIM];
    size_t n;
    int pcent;
    res_T res_local = RES_OK;
    res_T res_simul = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    time_current(&t0);

    if(!out_green) {
      /* Sample the time */
      time = sample_time(rng, time_range);

      /* Prepare path registration if necessary */
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

    /* Uniformly Sample an enclosure that surround the submitted medium and
     * uniformly sample a position into it */
    enc = sample_medium_enclosure(&cumul, rng);
    res_local = XD(sample_enclosure_position)(enc, rng, pos);
    if(res_local != RES_OK) {
      log_err(scn->dev, "%s: could not sample a medium position.\n", FUNC_NAME);
      ATOMIC_SET(&res, res_local);
      continue;
    }

    /* Run a probe realisation */
    res_simul = XD(probe_realisation)((size_t)irealisation, scn, rng, mdm, pos,
      time, fp_to_meter, Tarad, Tref, pgreen_path, pheat_path, &weight);

    if(res_simul != RES_OK && res_simul != RES_BAD_OP) {
      ATOMIC_SET(&res, res_simul);
      continue;
    }

    /* Finalize the registered path */
    if(pheat_path) {
      pheat_path->status = res_simul == RES_OK
        ? SDIS_HEAT_PATH_SUCCESS
        : SDIS_HEAT_PATH_FAILURE;

      /* Check if the path must be saved regarding the register_paths mask */
      if(!(register_paths & (int)pheat_path->status)) {
        heat_path_release(pheat_path);
      } else { /* Register the sampled path */
        res_local = estimator_add_and_release_heat_path(estimator, pheat_path);
        if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); continue; }
      }
    }

    /* Stop time registration */
    time_sub(&t0, time_current(&t1), &t0);

    /* Update accumulators */
    if(res_simul == RES_OK) {
      const double usec = (double)time_val(&t0, TIME_NSEC) * 0.001;
      acc_temp->sum += weight; acc_temp->sum2 += weight*weight; ++acc_temp->count;
      acc_time->sum += usec;   acc_time->sum2 += usec*usec;     ++acc_time->count;
    }

    /* Update progress */
    n = (size_t)ATOMIC_INCR(&nsolved_realisations);
    pcent = (int)((double)n * 100.0 / (double)nrealisations + 0.5/*round*/);
    #pragma omp critical
    if(pcent > progress) {
      progress = pcent;
      log_info(scn->dev, "Solving medium temperature: %3d%%\r", progress);
    }
  }
  if(res != RES_OK) goto error;

  /* Add a new line after the progress status */
  log_info(scn->dev, "Solving medium temperature: %3d%%\n", progress);

  /* Setup the estimated temperature */
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
    FOR_EACH(i, 0, scn->dev->nthreads) {
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
  if(cumul_is_init) darray_enclosure_cumul_release(&cumul);
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_estimator) *out_estimator = estimator;
  if(out_green) *out_green = green;
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
