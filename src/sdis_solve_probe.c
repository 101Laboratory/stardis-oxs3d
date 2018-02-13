/* Copyright (C) |Meso|Star> 2016-2018 (contact@meso-star.com)
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
#include "sdis_estimator_c.h"
#include "sdis_solve_probe_Xd.h"

/* Generate the 2D solver */
#define SDIS_SOLVE_PROBE_DIMENSION 2
#include "sdis_solve_probe_Xd.h"

/* Generate the 3D solver */
#define SDIS_SOLVE_PROBE_DIMENSION 3
#include "sdis_solve_probe_Xd.h"

#include <star/ssp.h>
#include <omp.h>

res_T
sdis_solve_probe
  (struct sdis_scene* scn,
   const size_t nrealisations,
   const double position[3],
   const double time,
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   struct sdis_estimator** out_estimator)
{
  const struct sdis_medium* medium = NULL;
  struct sdis_estimator* estimator = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  double weight = 0;
  double sqr_weight = 0;
  size_t irealisation = 0;
  size_t N = 0; /* #realisations that do not fail */
  size_t i;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || !position || time < 0 || fp_to_meter <= 0
  || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(struct ssp_rng*));
  if(!rngs) {
    res = RES_MEM_ERR;
    goto error;
  }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs+i);
    if(res != RES_OK) goto error;
  }

  /* Create the estimator */
  res = estimator_create(scn->dev, &estimator);
  if(res != RES_OK) goto error;

  /* Retrieve the medium in which the submitted position lies */
  res = scene_get_medium(scn, position, &medium);
  if(res != RES_OK) goto error;

  /* Here we go! Launch the Monte Carlo estimation */
  #pragma omp parallel for schedule(static) reduction(+:weight,sqr_weight,N)
  for(irealisation = 0; irealisation < nrealisations; ++irealisation) {
    res_T res_local;
    double w;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occured */

    if(scene_is_2d(scn)) {
      res_local = probe_realisation_2d
        (scn, rng, medium, position, time, fp_to_meter, &w);
    } else {
      res_local = probe_realisation_3d
        (scn, rng, medium, position, time, fp_to_meter, &w);
    }
    if(res_local != RES_OK) {
      if(res_local == RES_BAD_OP) {
        ++estimator->nfailures;
      } else {
        ATOMIC_SET(&res, res_local);
        continue;
      }
    } else {
      weight += w;
      sqr_weight += w*w;
      ++N;
    }
  }

  estimator->nrealisations = N;
  estimator->temperature.E = weight / (double)N;
  estimator->temperature.V =
    sqr_weight / (double)N
  - estimator->temperature.E * estimator->temperature.E;
  estimator->temperature.SE = sqrt(estimator->temperature.V / (double)N);

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads)  {
      if(rngs[i]) SSP(rng_ref_put(rngs[i]));
    }
    MEM_RM(scn->dev->allocator, rngs);
  }
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

