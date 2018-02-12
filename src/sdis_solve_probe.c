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
  struct ssp_rng* rng = NULL;
  double weight = 0;
  double sqr_weight = 0;
  size_t irealisation = 0;
  res_T res = RES_OK;

  if(!scn || !nrealisations || !position || time < 0 || fp_to_meter <= 0
  || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = scene_get_medium(scn, position, &medium);
  if(res != RES_OK) goto error;

  res = ssp_rng_create(scn->dev->allocator, &ssp_rng_mt19937_64, &rng);
  if(res != RES_OK) goto error;
  res = estimator_create(scn->dev, &estimator);
  if(res != RES_OK) goto error;

  FOR_EACH(irealisation, 0, nrealisations) {
    double w;

    if(scene_is_2d(scn)) {
      res = probe_realisation_2d
        (scn, rng, medium, position, time, fp_to_meter, &w);
    } else {
      res = probe_realisation_3d
        (scn, rng, medium, position, time, fp_to_meter, &w);
    }
    if(res != RES_OK) {
      if(res == RES_BAD_OP) {
        ++estimator->nfailures;
      } else {
        goto error;
      }
    } else {
      weight += w;
      sqr_weight += w*w;
      ++estimator->nrealisations;
    }
  }

  estimator->temperature.E = weight / (double)estimator->nrealisations;
  estimator->temperature.V =
    sqr_weight / (double)estimator->nrealisations
  - estimator->temperature.E * estimator->temperature.E;
  estimator->temperature.SE =
    sqrt(estimator->temperature.V / (double)estimator->nrealisations);

exit:
  if(rng) SSP(rng_ref_put(rng));
  if(out_estimator) *out_estimator = estimator;
  return res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

