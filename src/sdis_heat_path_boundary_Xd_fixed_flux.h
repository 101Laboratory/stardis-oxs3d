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

#include "sdis_green.h"
#include "sdis_heat_path_boundary_c.h"
#include "sdis_interface_c.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Boundary path between a solid and a fluid with a fixed flux
 ******************************************************************************/
res_T
XD(solid_boundary_with_flux_path)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   const double phi,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  /* Input/output arguments of the function used to sample a reinjection */
  struct XD(sample_reinjection_step_args) samp_reinject_step_args =
    XD(SAMPLE_REINJECTION_STEP_ARGS_NULL);
  struct XD(reinjection_step) reinject_step = XD(REINJECTION_STEP_NULL);

  /* Reinjection arguments */
  struct XD(solid_reinjection_args) solid_reinject_args = 
    XD(SOLID_REINJECTION_ARGS_NULL);

  /* Data attached to the boundary */
  struct sdis_interface* interf = NULL;
  struct sdis_medium* solid = NULL;

  /* Miscellaneous terms */
  double lambda; /* Solid conductivity */
  double delta_boundary; /* Orthogonal reinjection dst at the boundary */
  double delta; /* Orthogonal fitted reinjection dst at the boundary */
  double delta_m; /* Delta in meters */
  double flux_term;
  enum sdis_side solid_side = SDIS_SIDE_NULL__;
  res_T res = RES_OK;

  ASSERT(frag && phi != SDIS_FLUX_NONE);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag));
  (void)ctx;

  /* Retrieve the solid split by the interface */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  solid = interface_get_medium(interf, frag->side);
  solid_side = frag->side;
  ASSERT(phi == interface_side_get_flux(interf, frag));
  ASSERT(solid->type == SDIS_SOLID);

  /* Fetch the solid properties */
  lambda = solid_get_thermal_conductivity(solid, &rwalk->vtx);
  delta = solid_get_delta(solid, &rwalk->vtx);

  /* Note that the reinjection distance is *FIXED*. It MUST ensure that the
   * orthogonal distance from the boundary to the reinjection point is at most
   * equal to delta. */
  delta_boundary = delta * sqrt(DIM);

  /* Sample a reinjection step */
  samp_reinject_step_args.rng = rng;
  samp_reinject_step_args.solid = solid;
  samp_reinject_step_args.rwalk = rwalk;
  samp_reinject_step_args.distance = delta_boundary;
  samp_reinject_step_args.side = solid_side;
  res = XD(sample_reinjection_step_solid_fluid)
    (scn, &samp_reinject_step_args, &reinject_step);
  if(res != RES_OK) goto error;

  /* Define the orthogonal dst from the boundary to the reinjection position */
  delta = reinject_step.distance / sqrt(DIM);
  delta_m = delta * scn->fp_to_meter;

  /* Handle the flux */
  flux_term = delta_m / lambda;
  T->value += phi * flux_term;
  if(ctx->green_path) {
    res = green_path_add_flux_term(ctx->green_path, interf, frag, flux_term);
    if(res != RES_OK) goto error;
  }

  /* Perform the reinjection into the solid */
  solid_reinject_args.reinjection = &reinject_step;
  solid_reinject_args.rwalk_ctx = ctx;
  solid_reinject_args.rwalk = rwalk;
  solid_reinject_args.rng = rng;
  solid_reinject_args.T = T;
  solid_reinject_args.fp_to_meter = scn->fp_to_meter;
  res = XD(solid_reinjection)(solid, &solid_reinject_args);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
