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

#include "sdis_realisation.h"

/* Generate the generic realisations */
#define SDIS_XD_DIMENSION 2
#include "sdis_realisation_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_realisation_Xd.h"

res_T
ray_realisation_3d
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct sdis_medium* medium,
   const double position[],
   const double direction[],
   const double time,
   const double fp_to_meter,
   const double Tarad,
   const double Tref,
   double* weight)
{
  struct rwalk_context ctx = RWALK_CONTEXT_NULL;
  struct rwalk_3d rwalk = RWALK_NULL_3d;
  struct temperature_3d T = TEMPERATURE_NULL_3d;
  float dir[3];
  res_T res = RES_OK;
  ASSERT(scn && position && direction && time>=0 && fp_to_meter>0 && weight);
  ASSERT(Tref >= 0 && medium && medium->type == SDIS_FLUID);

  d3_set(rwalk.vtx.P, position);
  rwalk.vtx.time = time;
  rwalk.hit = S3D_HIT_NULL;
  rwalk.hit_side = SDIS_SIDE_NULL__;
  rwalk.mdm = medium;

  ctx.Tarad = Tarad;
  ctx.Tref3 = Tref*Tref*Tref;

  f3_set_d3(dir, direction);

  res = trace_radiative_path_3d(scn, dir, fp_to_meter, &ctx, &rwalk, rng, &T);
  if(res != RES_OK) goto error;

  if(!T.done) {
    res = compute_temperature_3d(scn, fp_to_meter, &ctx, &rwalk, rng, &T);
    if(res != RES_OK) goto error;
  }

  *weight = T.value;

exit:
  return res;
error:
  goto exit;
}

