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
XD(solid_fluid_boundary_picardN_path)
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
  double h_radi_hat; /* Radiative coefficient with That */
  double h_radi_min;
  double h_hat; /* Sum of h_<conv|cond|rad_hat> */
  double p_conv; /* Convective proba */
  double p_cond; /* Conductive proba */

  double epsilon; /* Interface emissivity */
  double lambda; /* Solid conductivity */
  double delta_boundary; /* Orthogonal reinjection dst at the boundary */
  double delta; /* Orthogonal fitted reinjection dst at the boundary */

  double r;
  enum sdis_heat_vertex_type current_vertex_type;
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

  /* Fetch the boundary emissivity */
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

  /* Define the orthogonal dst from the reinjection pos to the interface */
  delta = reinject_step.distance / sqrt(DIM);

  /* Compute the convective, conductive and the upper bound radiative coef */
  h_conv = interface_get_convection_coef(interf, frag);
  h_cond = lambda / (delta * scn->fp_to_meter);
  h_radi_hat = 4.0 * BOLTZMANN_CONSTANT * That3 * epsilon;

  /* Compute a global upper bound coefficient */
  h_hat = h_conv + h_cond + h_radi_hat;

  /* Compute the probas to switch in solid, fluid or radiative random walk */
  p_conv = h_conv / h_hat;
  p_cond = h_cond / h_hat;

  /* Null collision */
  for(;;) {
    double h_radi, h_radi_min, h_radi_max; /* Radiative coefficients */
    double p_radi, p_radi_min, p_radi_max; /* Radiative probas */
    double T0, T1, T2, T3, T4, T5; /* Computed temperatures */
    
    r = ssp_rng_canonical(rng);

    /* Switch in convective path */
    if(r < p_conv) {
      T->func = XD(convective_path);
      rwalk->mdm = fluid;
      rwalk->hit_side = fluid_side;
      break exit;
    }

    /* Switch in conductive path */
    if(r < p_conv + p_cond) {
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
      break exit;
    }

    /* Sample a radiative path */
    T_s = *T;
    rwalk_s = *rwalk;
    rwalk_s.mdm = fluid_side;
    rwalk_s.hit_side = fluid_side;
    res = XD(radiative_path)(scn, ctx, &rwalk_s, rng, &T_s);
    if(res != RES_OK) goto error;

    h_radi_min = 4.0 * BOLTZMANN_CONSTANT * Tmin3 * epsilon;
    p_radi_min = h_radi_min / h_radi_hat;

    /* Switch in radiative path */
    if(r < p_cond + p_cond + p_radi_min) { *rwalk = rwalk_s; *T = T_s; break; }

    #define COMPUTE_TEMPERATURE(Result, RWalk, Temperature) {                  \
      struct XD(temperature) T_p = *(Temperature);                             \
      struct XD(rwalk) rwalk_p = *(RWalk);                                     \
      res = XD(compute_temperature)(scn, ctx, &rwalk_p, rng, &T_p);            \
      if(res != RES_OK) goto error;                                            \
      Result = (Temperature)->temperature;                                     \
    } (void)0

    #define CHECK_PMIN_PMAX() {                                                \
      p_radi_min = h_radi_min / h_radi_hat;                                    \
      p_radi_max = h_radi_max / h_radi_hat;                                    \
      /* Switch in radiative path */                                           \
      if(r < p_cond + p_conv + p_radi_min) { *rwalk=rwalk_s; *T=T_s; break; }  \
      /* Null collision reject the sampled path */                             \
      if(r > p_cond + p_conv + p_radi_max) { continue; /* Null collision */ }  \
    } (void)0

    COMPUTE_TEMPERATURE(T0, &rwalk_s, &T_s);
    h_radi_min = BOLTZMANN_CONSTANT*(Tmin3 * 3*Tmin2*T0);
    h_radi_max = BOLTZMANN_CONSTANT*(That3 * 3*That2*T0);
    CHECK_PMIN_PMAX();
    
    COMPUTE_TEMPERATURE(T1, &rwalk_s, &T_s);
    h_radi_min = BOLTZMANN_CONSTANT*(Tmin3 + Tmin2*T0 + 2*Tmin*T0*T1);
    h_radi_max = BOLTZMANN_CONSTANT*(That3 + That2*T0 + 2*That*T0*T1);
    CHECK_PMIN_PMAX();
    
    COMPUTE_TEMPERATURE(T2, &rwalk_s, &T_s);
    h_radi_min = BOLTZMANN_CONSTANT*(Tmin3 + Tmin2*T0 + Tmin*T0*T1 + T0*T1*T2);
    h_radi_max = BOLTZMANN_CONSTANT*(That3 + That2*T0 + That*T0*T1 + T0*T1*T2);
    CHECK_PMIN_PMAX();

    COMPUTE_TEMPERATURE(T3, rwalk, T);
    h_radi_min = BOLTZMANN_CONSTANT*(Tmin2*T3 + Tmin*T0*T3 + T0*T1*T3 + T0*T1*T2);
    h_radi_max = BOLTZMANN_CONSTANT*(That2*T3 + That*T0*T3 + T0*T1*T3 + T0*T1*T2);
    CHECK_PMIN_PMAX();

    COMPUTE_TEMPERATURE(T4, rwalk, T);
    h_radi_min = BOLTZMANN_CONSTANT*(Tmin*T3*T4 + T0*T3*T4 + T0*T1*T3 + T0*T1*T2);
    h_radi_max = BOLTZMANN_CONSTANT*(That*T3*T4 + T0*T3*T4 + T0*T1*T3 + T0*T1*T2);
    CHECK_PMIN_PMAX();

    COMPUTE_TEMPERATURE(T5, rwalk, T);
    h_radi = BOLTZMANN_CONSTANT*(T3*T4*T5 + T0*T3*T4 + T0*T1*T3 + T0*T1*T2);
    p_radi = h_radi / h_radi_hat;

    /* Switch in radiative path */
    if(r < p_cond + p_conv + p_radi) { *rwalk=rwalk_s; *T=T_s; break; }

    /* Null-collision, looping at the beginning */

    #undef COMPUTE_TEMPERATURE
    #undef CHECK_PMIN_PMAX
  }

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"

