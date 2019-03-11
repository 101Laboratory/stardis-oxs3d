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
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_realisation.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>
#include <omp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Local functions
 ******************************************************************************/
static res_T
XD(solve_probe_boundary)
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t iprim, /* Identifier of the primitive on which the probe lies */
   const double uv[2], /* Parametric coordinates of the probe onto the primitve */
   const double time_range[2], /* Observation time */
   const enum sdis_side side, /* Side of iprim on which the probe lies */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
   const int register_paths, /* Combination of enum sdis_heat_path_flag */
   struct sdis_estimator** out_estimator)
{
  struct sdis_estimator* estimator = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  struct accum* accums = NULL;
  int64_t irealisation = 0;
  size_t i;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !uv
  || !time_range || time_range[0] < 0 || time_range[1] < time_range[0]
  || (time_range[1] > DBL_MAX && time_range[0] != time_range[1])
  || fp_to_meter <= 0 || Tref < 0 || (side != SDIS_FRONT && side != SDIS_BACK)
  || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

#if SDIS_XD_DIMENSION == 2
  if(scene_is_2d(scn) == 0) { res = RES_BAD_ARG; goto error; }
#else
  if(scene_is_2d(scn) != 0) { res = RES_BAD_ARG; goto error; }
#endif

  /* Check the primitive identifier */
  if(iprim >= scene_get_primitives_count(scn)) {
    log_err(scn->dev,
      "%s: invalid primitive identifier `%lu'. "
      "It must be in the [0 %lu] range.\n",
      FUNC_NAME,
      (unsigned long)iprim,
      (unsigned long)scene_get_primitives_count(scn)-1);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check parametric coordinates */
#if SDIS_XD_DIMENSION  == 2
  {
    const double v = CLAMP(1.0 - uv[0], 0, 1);
    if(uv[0] < 0 || uv[0] > 1 || !eq_eps(uv[0] + v, 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates %g."
        "u + (1-u) must be equal to 1 with u [0, 1].\n",
        FUNC_NAME, uv[0]);
      res = RES_BAD_ARG;
      goto error;
    }
  }
#else /* SDIS_XD_DIMENSION == 3 */
  {
    const double w = CLAMP(1 - uv[0] - uv[1], 0, 1);
    if(uv[0] < 0 || uv[1] < 0 || uv[0] > 1 || uv[1] > 1
    || !eq_eps(w + uv[0] + uv[1], 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates [%g, %g]. "
        "u + v + (1-u-v) must be equal to 1 with u and v in [0, 1].\n",
        FUNC_NAME, uv[0], uv[1]);
      res = RES_BAD_ARG;
      goto error;
    }
  }
#endif

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(struct ssp_rng*));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs+i);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread accumulator */
  accums = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*accums));
  if(!accums) { res = RES_MEM_ERR; goto error; }

  /* Create the estimator */
  res = estimator_create(scn->dev, SDIS_ESTIMATOR_TEMPERATURE, &estimator);
  if(res != RES_OK) goto error;

  /* Here we go! Launch the Monte Carlo estimation */
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    struct accum* accum = &accums[ithread];
    struct sdis_heat_path* pheat_path = NULL;
    struct sdis_heat_path heat_path;
    double w = NaN;
    double time;
    res_T res_local;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    time = sample_time(rng, time_range);

    if(register_paths) {
      heat_path_init(scn->dev->allocator, &heat_path);
      pheat_path = &heat_path;
    }

    res_local = XD(boundary_realisation)(scn, rng, iprim, uv, time, side,
      fp_to_meter, Tarad, Tref, pheat_path, &w);
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
  sum_accums(accums, scn->dev->nthreads, &accums[0]);
  estimator_setup_realisations_count(estimator, nrealisations, accums[0].count);
  estimator_setup_temperature(estimator, accums[0].sum, accums[0].sum2);

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) {
      if(rngs[i]) SSP(rng_ref_put(rngs[i]));
    }
    MEM_RM(scn->dev->allocator, rngs);
  }
  if(accums) MEM_RM(scn->dev->allocator, accums);
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_estimator) *out_estimator = estimator;
  return (res_T)res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

static res_T
XD(solve_probe_boundary_flux)
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t iprim, /* Identifier of the primitive on which the probe lies */
   const double uv[2], /* Parametric coordinates of the probe onto the primitve */
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
   struct sdis_estimator** out_estimator)
{
  struct sdis_estimator* estimator = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  const struct sdis_interface* interf;
  const struct sdis_medium *fmd, *bmd;
  enum sdis_side solid_side, fluid_side;
  struct sdis_interface_fragment frag;
  struct accum* acc_tp = NULL; /* Per thread temperature accumulator */
  struct accum* acc_fl = NULL; /* Per thread flux accumulator */
  struct accum* acc_fc = NULL; /* Per thread convective flux accumulator */
  struct accum* acc_fr = NULL; /* Per thread radiative flux accumulator */
  int64_t irealisation = 0;
  size_t i;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !uv
  || !time_range || time_range[0] < 0 || time_range[1] < time_range[0]
  || (time_range[1] > DBL_MAX && time_range[0] != time_range[1])
  || fp_to_meter <= 0 || Tref < 0
  || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

#if SDIS_XD_DIMENSION == 2
  if(scene_is_2d(scn) == 0) { res = RES_BAD_ARG; goto error; }
#else
  if(scene_is_2d(scn) != 0) { res = RES_BAD_ARG; goto error; }
#endif

  /* Check the primitive identifier */
  if(iprim >= scene_get_primitives_count(scn)) {
    log_err(scn->dev,
      "%s: invalid primitive identifier `%lu'. "
      "It must be in the [0 %lu] range.\n",
      FUNC_NAME,
      (unsigned long)iprim,
      (unsigned long)scene_get_primitives_count(scn)-1);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check parametric coordinates */
  if(scene_is_2d(scn)) {
    const double v = CLAMP(1.0 - uv[0], 0, 1);
    if(uv[0] < 0 || uv[0] > 1 || !eq_eps(uv[0] + v, 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates %g. "
        "u + (1-u) must be equal to 1 with u [0, 1].\n",
        FUNC_NAME, uv[0]);
      res = RES_BAD_ARG;
      goto error;
    }
  } else {
    const double w = CLAMP(1 - uv[0] - uv[1], 0, 1);
    if(uv[0] < 0 || uv[1] < 0 || uv[0] > 1 || uv[1] > 1
      || !eq_eps(w + uv[0] + uv[1], 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates [%g, %g]. "
        "u + v + (1-u-v) must be equal to 1 with u and v in [0, 1].\n",
        FUNC_NAME, uv[0], uv[1]);
      res = RES_BAD_ARG;
      goto error;
    }
  }
  /* Check medium is fluid on one side and solid on the other */
  interf = scene_get_interface(scn, (unsigned)iprim);
  fmd = interface_get_medium(interf, SDIS_FRONT);
  bmd = interface_get_medium(interf, SDIS_BACK);
  if(!fmd || !bmd
  || (  !(fmd->type == SDIS_FLUID && bmd->type == SDIS_SOLID)
     && !(fmd->type == SDIS_SOLID && bmd->type == SDIS_FLUID))) {
    res = RES_BAD_ARG;
    goto error;
  }
  solid_side = (fmd->type == SDIS_SOLID) ? SDIS_FRONT : SDIS_BACK;
  fluid_side = (fmd->type == SDIS_FLUID) ? SDIS_FRONT : SDIS_BACK;

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(struct ssp_rng*));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs + i);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread accumulator */
  #define ALLOC_ACCUMS(Dst) {                                                  \
    Dst = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*Dst));   \
    if(!Dst) { res = RES_MEM_ERR; goto error; }                                \
  } (void)0
  ALLOC_ACCUMS(acc_tp);
  ALLOC_ACCUMS(acc_fc);
  ALLOC_ACCUMS(acc_fl);
  ALLOC_ACCUMS(acc_fr);
  #undef ALLOC_ACCUMS

  /* Prebuild the interface fragment */
  res = XD(build_interface_fragment)
    (&frag, scn, (unsigned)iprim, uv, fluid_side);
  if(res != RES_OK) goto error;

  /* Create the estimator */
  res = estimator_create(scn->dev, SDIS_ESTIMATOR_FLUX, &estimator);
  if(res != RES_OK) goto error;

  /* Here we go! Launch the Monte Carlo estimation */
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    struct accum* acc_temp = &acc_tp[ithread];
    struct accum* acc_flux = &acc_fl[ithread];
    struct accum* acc_fcon = &acc_fc[ithread];
    struct accum* acc_frad = &acc_fr[ithread];
    double time, epsilon, hc, hr;
    int flux_mask = 0;
    double T_brf[3] = { 0, 0, 0 };
    res_T res_local;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    time = sample_time(rng, time_range);

    /* Compute hr and hc */
    frag.time = time;
    epsilon = interface_side_get_emissivity(interf, &frag);
    hc = interface_get_convection_coef(interf, &frag);
    hr = 4.0 * BOLTZMANN_CONSTANT * Tref * Tref * Tref * epsilon;

    /* Fluid, Radiative and Solid temperatures */
    flux_mask = 0;
    if(hr > 0) flux_mask |= FLUX_FLAG_RADIATIVE;
    if(hc > 0) flux_mask |= FLUX_FLAG_CONVECTIVE;
    res_local = XD(boundary_flux_realisation)(scn, rng, iprim, uv, time,
      solid_side, fp_to_meter, Tarad, Tref, flux_mask, T_brf);
    if(res_local == RES_OK) {
      const double Tboundary = T_brf[0];
      const double Tradiative = T_brf[1];
      const double Tfluid = T_brf[2];
      const double w_conv = hc * (Tboundary - Tfluid);
      const double w_rad = hr * (Tboundary - Tradiative);
      const double w_total = w_conv + w_rad;
      /* Temperature */
      acc_temp->sum += Tboundary;
      acc_temp->sum2 += Tboundary*Tboundary;
      ++acc_temp->count;
      /* Overwall flux */
      acc_flux->sum += w_total;
      acc_flux->sum2 += w_total*w_total;
      ++acc_flux->count;
      /* Convective flux */
      acc_fcon->sum  += w_conv;
      acc_fcon->sum2 += w_conv*w_conv;
      ++acc_fcon->count;
      /* Radiative flux */
      acc_frad->sum += w_rad;
      acc_frad->sum2 += w_rad*w_rad;
      ++acc_frad->count;
    } else if(res_local != RES_BAD_OP) {
      ATOMIC_SET(&res, res_local);
      continue;
    }
  }
  if(res != RES_OK) goto error;

  /* Redux the per thread accumulators  */
  sum_accums(acc_tp, scn->dev->nthreads, &acc_tp[0]);
  sum_accums(acc_fc, scn->dev->nthreads, &acc_fc[0]);
  sum_accums(acc_fr, scn->dev->nthreads, &acc_fr[0]);
  sum_accums(acc_fl, scn->dev->nthreads, &acc_fl[0]);
  ASSERT(acc_tp[0].count == acc_fl[0].count);
  ASSERT(acc_tp[0].count == acc_fr[0].count);
  ASSERT(acc_tp[0].count == acc_fc[0].count);

  /* Setup the estimated values */
  estimator_setup_realisations_count(estimator, nrealisations, acc_tp[0].count);
  estimator_setup_temperature(estimator, acc_tp[0].sum, acc_tp[0].sum2);
  estimator_setup_flux(estimator, FLUX_CONVECTIVE, acc_fc[0].sum, acc_fc[0].sum2);
  estimator_setup_flux(estimator, FLUX_RADIATIVE, acc_fr[0].sum, acc_fr[0].sum2);
  estimator_setup_flux(estimator, FLUX_TOTAL, acc_fl[0].sum, acc_fl[0].sum2);

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) {if(rngs[i]) SSP(rng_ref_put(rngs[i]));}
    MEM_RM(scn->dev->allocator, rngs);
  }
  if(acc_tp) MEM_RM(scn->dev->allocator, acc_tp);
  if(acc_fc) MEM_RM(scn->dev->allocator, acc_fc);
  if(acc_fr) MEM_RM(scn->dev->allocator, acc_fr);
  if(acc_fl) MEM_RM(scn->dev->allocator, acc_fl);
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_estimator) *out_estimator = estimator;
  return (res_T)res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

#include "sdis_Xd_end.h"
