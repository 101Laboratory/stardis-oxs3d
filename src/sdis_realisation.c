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
   const double position[3],
   const double direction[3],
   const double time,
   struct sdis_heat_path* heat_path, /* May be NULL */
   double* weight)
{
  struct rwalk_context ctx = RWALK_CONTEXT_NULL;
  struct rwalk_3d rwalk = RWALK_NULL_3d;
  struct temperature_3d T = TEMPERATURE_NULL_3d;
  float dir[3];
  res_T res = RES_OK;
  ASSERT(scn && position && direction && time>=0 && weight);
  ASSERT(medium && medium->type == SDIS_FLUID);

  d3_set(rwalk.vtx.P, position);
  rwalk.vtx.time = time;
  rwalk.hit = S3D_HIT_NULL;
  rwalk.hit_side = SDIS_SIDE_NULL__;
  rwalk.mdm = medium;

  ctx.heat_path = heat_path;
  ctx.Tarad = scn->ambient_radiative_temperature;
  ctx.That = scn->maximum_temperature;
  ctx.That2 = ctx.That * ctx.That;
  ctx.That3 = ctx.That * ctx.That2;
  ctx.Tref3 =
    scn->reference_temperature
  * scn->reference_temperature
  * scn->reference_temperature;

  f3_set_d3(dir, direction);

  /* Register the starting position against the heat path */
  res = register_heat_vertex(heat_path, &rwalk.vtx, 0, SDIS_HEAT_VERTEX_RADIATIVE);
  if(res != RES_OK) goto error;

  res = trace_radiative_path_3d(scn, dir, &ctx, &rwalk, rng, &T);
  if(res != RES_OK) goto error;

  if(!T.done) {
    res = compute_temperature_3d(scn, &ctx, &rwalk, rng, &T);
    if(res != RES_OK) goto error;
  }

  *weight = T.value;

exit:
  return res;
error:
  goto exit;
}

