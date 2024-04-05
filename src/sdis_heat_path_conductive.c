/* Copyright (C) 2016-2023 |Méso|Star> (contact@meso-star.com)
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

#include "sdis_heat_path.h"
#include "sdis_heat_path_conductive_c.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"

/*******************************************************************************
 * Local function
 ******************************************************************************/
res_T
check_solid_constant_properties
  (struct sdis_device* dev,
   const int evaluate_green,
   const int use_wos_diffusion,
   const struct solid_props* props_ref,
   const struct solid_props* props)
{
  res_T res = RES_OK;
  ASSERT(dev && props_ref && props);

  if(props_ref->lambda != props->lambda) {
    log_err(dev,
      "%s: invalid thermal conductivity. One assumes a constant conductivity "
      "for the whole solid.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  if(props_ref->rho != props->rho) {
    log_err(dev,
      "%s: invalid volumic mass. One assumes a constant volumic mass for "
      "the whole solid.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  if(props_ref->cp != props->cp) {
    log_err(dev,
       "%s: invalid calorific capacity. One assumes a constant calorific "
       "capacity for the whole solid.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  if((evaluate_green || use_wos_diffusion) && props_ref->power != props->power) {
    log_err(dev,
      "%s: invalid variable power density. Stardis expects a constant power "
      "density per solid when using WoS diffusion and/or green function "
      "evaluation.", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
conductive_path_2d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* T)
{
  res_T res = RES_OK;
  ASSERT(ctx);

  switch(ctx->diff_algo) {
    case SDIS_DIFFUSION_DELTA_SPHERE:
      res = conductive_path_delta_sphere_2d(scn, ctx, rwalk, rng, T);
      break;
    case SDIS_DIFFUSION_WOS:
      res = conductive_path_wos_2d(scn, ctx, rwalk, rng, T);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return res;
}

res_T
conductive_path_3d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* T)
{
  res_T res = RES_OK;
  ASSERT(ctx);

  switch(ctx->diff_algo) {
    case SDIS_DIFFUSION_DELTA_SPHERE:
      res = conductive_path_delta_sphere_3d(scn, ctx, rwalk, rng, T);
      break;
    case SDIS_DIFFUSION_WOS:
      res = conductive_path_wos_3d(scn, ctx, rwalk, rng, T);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return res;
}

/* Generate the conductive path functions */
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_conductive_delta_sphere_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_conductive_delta_sphere_Xd.h"
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_conductive_wos_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_conductive_wos_Xd.h"
