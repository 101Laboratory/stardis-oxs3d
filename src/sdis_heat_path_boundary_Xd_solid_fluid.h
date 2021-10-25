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
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Boundary path between a solid and a fluid
 ******************************************************************************/
res_T
XD(solid_fluid_boundary_path)
  (struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  /* Input/output arguments of the function used to sample a reinjection */
  struct XD(sample_reinjection_step_args) samp_reinject_step_args =
    XD(SAMPLE_REINJECTION_STEP_ARGS_NULL);
  struct XD(reinjection_step) reinject_step =
    XD(REINJECTION_STEP_NULL);

  /* Fragment on the fluid side of the boundary */
  struct sdis_interface_fragment frag_fluid;

  /* Data attached to the boundary */
  struct sdis_interface* interf = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;

  double h_cond; /* Conductive coefficient */
  double h_conv; /* Convective coefficient */
  double h_radi; /* Radiative coefficient */
  double h; /* Sum of h_<conv|cond|radi> */
  double p_conv; /* Convective proba */
  double p_radi; /* Radiative proba */

  double epsilon; /* Interface emissivity */
  double lambda; /* Solid conductivity */
  double delta_boundary; /* Orthogonal reinjection dst at the boundary */
  double delta; /* Orthogonal fitted reinjection dst at the boundary */

  double r;
  enum sdis_side solid_side = SDIS_SIDE_NULL__;
  enum sdis_side fluid_side = SDIS_SIDE_NULL__;
  res_T res = RES_OK;

  ASSERT(scn && rwalk && rng && T && ctx);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag));

  /* Retrieve the solid and the fluid split by the boundary */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  solid = interface_get_medium(interf, SDIS_FRONT);
  fluid = interface_get_medium(interf, SDIS_BACK);
  solid_side = SDIS_FRONT;
  fluid_side = SDIS_BACK;
  if(solid->type != SDIS_SOLID) {
    SWAP(struct sdis_medium*, solid, fluid);
    SWAP(enum sdis_side, solid_side, fluid_side);
    ASSERT(fluid->type == SDIS_FLUID);
  }

  /* Setup a fragment for the fluid side */
  frag_fluid = *frag;
  frag_fluid.side = fluid_side;

  /* Fetch the solid properties */
  lambda = solid_get_thermal_conductivity(solid, &rwalk->vtx);
  delta = solid_get_delta(solid, &rwalk->vtx);

  /* Fetch the interface properties on the fluid side */
  epsilon = interface_side_get_emissivity(interf, &frag_fluid);

  /* Note that the reinjection distance is *FIXED*. It MUST ensure that the
   * orthogonal distance from the boundary to the reinjection point is at most
   * equal to delta. */
  delta_boundary = sqrt(DIM) * delta;

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

  /* Compute the convective, conductive and radiative coefficients */
  h_conv = interface_get_convection_coef(interf, frag);
  h_cond = lambda / (delta * scn->fp_to_meter);
  h_radi = 4.0 * BOLTZMANN_CONSTANT * ctx->Tref3 * epsilon;
  h = h_conv + h_cond + h_radi;

  /* Compute the probas */
  p_conv = h_conv / h;
  p_radi = h_radi / h;

  r = ssp_rng_canonical(rng);

  /* Switch in radiative path */
  if(r < p_radi) {
    T->func = XD(radiative_path);
    rwalk->mdm = fluid;
    rwalk->hit_side = fluid_side;

  /* Switch to convective path */
  } else if(r < p_radi + p_conv) {
    T->func = XD(convective_path);
    rwalk->mdm = fluid;
    rwalk->hit_side = fluid_side;

  /* Switch in conductive path */
  } else {
    struct XD(solid_reinjection_args) solid_reinject_args =
      XD(SOLID_REINJECTION_ARGS_NULL);

    /* Perform the reinjection into the solid */
    solid_reinject_args.reinjection = &reinject_step;
    solid_reinject_args.rwalk_ctx = ctx;
    solid_reinject_args.rwalk = rwalk;
    solid_reinject_args.rng = rng;
    solid_reinject_args.T = T;
    solid_reinject_args.fp_to_meter = scn->fp_to_meter;
    res = XD(solid_reinjection)(solid, &solid_reinject_args);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
