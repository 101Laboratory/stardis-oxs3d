/* Copyright (C) 2016-2024 |Méso|Star> (contact@meso-star.com)
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

#include "sdis_medium_c.h"
#include "sdis_realisation.h"

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE res_T
check_ray_realisation_args
  (struct sdis_scene* scn,
   const struct ray_realisation_args* args)
{
  const struct enclosure* enc = NULL;
  struct sdis_medium* mdm = NULL;
  res_T res = RES_OK;
  ASSERT(scn);

  /* Check pointers */
  if(!args || !args->rng) return RES_BAD_ARG;

  if(args->time < 0) return RES_BAD_ARG;
  if(args->picard_order <= 0) return RES_BAD_ARG;

  if((unsigned)args->diff_algo >= SDIS_DIFFUSION_ALGORITHMS_COUNT__) {
    return RES_BAD_ARG;
  }

  /* Check the enclosure id */
  if(args->enc_id == ENCLOSURE_ID_NULL) {
    return RES_BAD_ARG;
  }
  enc = scene_get_enclosure(scn, args->enc_id);
  if((res = scene_get_enclosure_medium(scn, enc, &mdm)) != RES_OK) return res;
  if(sdis_medium_get_type(mdm) != SDIS_FLUID) {
    return RES_BAD_ARG;
  }

  return RES_OK;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
/* Generate the generic realisations */
#define SDIS_XD_DIMENSION 2
#include "sdis_realisation_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_realisation_Xd.h"

res_T
ray_realisation_3d
  (struct sdis_scene* scn,
   struct ray_realisation_args* args,
   double* weight)
{
  struct rwalk_context ctx = RWALK_CONTEXT_NULL;
  struct rwalk rwalk = RWALK_NULL;
  struct temperature T = TEMPERATURE_NULL;
  float dir[3];
  res_T res = RES_OK;
  ASSERT(scn && weight && check_ray_realisation_args(scn, args) == RES_OK);

  d3_set(rwalk.vtx.P, args->position);
  rwalk.vtx.time = args->time;
  rwalk.hit_3d = S3D_HIT_NULL;
  rwalk.hit_side = SDIS_SIDE_NULL__;
  rwalk.enc_id = args->enc_id;

  ctx.heat_path = args->heat_path;
  ctx.Tmin  = scn->tmin;
  ctx.Tmin2 = ctx.Tmin * ctx.Tmin;
  ctx.Tmin3 = ctx.Tmin * ctx.Tmin2;
  ctx.That  = scn->tmax;
  ctx.That2 = ctx.That * ctx.That;
  ctx.That3 = ctx.That * ctx.That2;
  ctx.max_branchings = args->picard_order - 1;
  ctx.irealisation = args->irealisation;
  ctx.diff_algo = args->diff_algo;

  f3_set_d3(dir, args->direction);

  /* Register the starting position against the heat path */
  res = register_heat_vertex
    (args->heat_path, &rwalk.vtx, 0, SDIS_HEAT_VERTEX_RADIATIVE, 0);
  if(res != RES_OK) goto error;

  res = trace_radiative_path_3d(scn, dir, &ctx, &rwalk, args->rng, &T);
  if(res != RES_OK) goto error;

  if(!T.done) {
    res = sample_coupled_path_3d(scn, &ctx, &rwalk, args->rng, &T);
    if(res != RES_OK) goto error;
  }

  *weight = T.value;

exit:
  return res;
error:
  goto exit;
}

