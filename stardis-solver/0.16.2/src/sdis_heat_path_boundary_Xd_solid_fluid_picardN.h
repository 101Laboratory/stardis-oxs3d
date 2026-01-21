/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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
#include "sdis_realisation.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Non generic helper functions
 ******************************************************************************/
#ifndef SDIS_HEAT_PATH_BOUNDARY_XD_SOLID_FLUID_PICARD_N_H
#define SDIS_HEAT_PATH_BOUNDARY_XD_SOLID_FLUID_PICARD_N_H

static INLINE res_T
check_net_flux
  (struct sdis_scene* scn,
   const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag,
   const size_t picard_order)
{
  double phi;
  res_T res = RES_OK;
  ASSERT(scn && interf && frag && picard_order > 1);

  phi = interface_side_get_flux(interf, frag);
  if(phi != SDIS_FLUX_NONE && phi != 0) {
    log_err(scn->dev,
      "%s: invalid flux '%g' W/m^2. Could not manage a flux != 0 when the "
      "picard order is not equal to 1; Picard order is currently set to %lu.\n",
      FUNC_NAME, phi, (unsigned long)picard_order);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#endif /* SDIS_HEAT_PATH_BOUNDARY_XD_SOLID_FLUID_PICARD_N_H */

/*******************************************************************************
 * Generic helper functions
 ******************************************************************************/
static INLINE res_T
XD(sample_path)
  (struct sdis_scene* scn,
   const struct rwalk* rwalk_from,
   struct rwalk_context* ctx,
   struct ssp_rng* rng,
   struct temperature* T)
{
  struct rwalk rwalk = RWALK_NULL;
  res_T res = RES_OK;
  ASSERT(rwalk_from && rng && T);

  /* Clean-up the output variable */
  *T = TEMPERATURE_NULL;
  T->func = XD(boundary_path);

  /* Init the random walk */
  rwalk.vtx = rwalk_from->vtx;
  rwalk.enc_id = rwalk_from->enc_id;
  rwalk.XD(hit) = rwalk_from->XD(hit);
  rwalk.hit_side = rwalk_from->hit_side;

  /* Start the registration of a new heat path */
  if(ctx->heat_path) {
    struct sdis_heat_vertex heat_vtx = SDIS_HEAT_VERTEX_NULL;

    heat_vtx.P[0] = rwalk.vtx.P[0];
    heat_vtx.P[1] = rwalk.vtx.P[1];
    heat_vtx.P[2] = rwalk.vtx.P[2];
    heat_vtx.time = rwalk.vtx.time;
    heat_vtx.weight = 0;
    heat_vtx.type = SDIS_HEAT_VERTEX_RADIATIVE;
    heat_vtx.branch_id = (int)ctx->nbranchings + 1;

    res = heat_path_restart(ctx->heat_path, &heat_vtx);
    if(res != RES_OK) goto error;
  }

  /* Sample the path */
  res = XD(sample_coupled_path)(scn, ctx, &rwalk, rng, T);
  if(res != RES_OK) goto error;

  /* Check the returned temperature */
  ASSERT(T->done);
  if(T->value < scn->tmin || scn->tmax < T->value) {
    log_err(scn->dev, "%s: invalid temperature range `[%g, %g]K` regarding the "
      "retrieved temperature %gK.\n", FUNC_NAME, scn->tmin, scn->tmax, T->value);
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Boundary path between a solid and a fluid
 ******************************************************************************/
res_T
XD(solid_fluid_boundary_picardN_path)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  /* Input/output arguments of the function used to sample a reinjection */
  struct sample_reinjection_step_args samp_reinject_step_args =
    SAMPLE_REINJECTION_STEP_ARGS_NULL;
  struct reinjection_step reinject_step = REINJECTION_STEP_NULL;

  /* Fragment on the fluid side of the boundary */
  struct sdis_interface_fragment frag_fluid;

  /* Vertex of the heat path */
  struct sdis_heat_vertex hvtx = SDIS_HEAT_VERTEX_NULL;
  struct sdis_heat_vertex hvtx_s = SDIS_HEAT_VERTEX_NULL;

  /* The enclosures split by the boundary */
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};

  /* Data attached to the boundary */
  struct sdis_interface* interf = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;

  double h_cond; /* Conductive coefficient */
  double h_conv; /* Convective coefficient */
  double h_radi_hat; /* Radiative coefficient with That */
  double h_hat; /* Sum of h_<conv|cond|rad_hat> */
  double p_conv; /* Convective proba */
  double p_cond; /* Conductive proba */

  /* Min/Max Temperatures */
  double Tmin, Tmin2, Tmin3;
  double That, That2, That3;

  double epsilon; /* Interface emissivity */
  double lambda; /* Solid conductivity */
  double delta_boundary; /* Orthogonal reinjection dst at the boundary */
  double delta; /* Orthogonal fitted reinjection dst at the boundary */

  double r;
  enum sdis_side solid_side = SDIS_SIDE_NULL__;
  enum sdis_side fluid_side = SDIS_SIDE_NULL__;
  res_T res = RES_OK;

  ASSERT(scn && rwalk && rng && T && ctx);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag) == RES_OK);

  /* Fetch the Min/max temperature */
  Tmin  = ctx->Tmin;
  Tmin2 = ctx->Tmin2;
  Tmin3 = ctx->Tmin3;
  That  = ctx->That;
  That2 = ctx->That2;
  That3 = ctx->That3;

  /* Retrieve the solid and the fluid split by the boundary */
  interf = scene_get_interface(scn, rwalk->XD(hit).prim.prim_id);
  solid = interface_get_medium(interf, SDIS_FRONT);
  fluid = interface_get_medium(interf, SDIS_BACK);
  solid_side = SDIS_FRONT;
  fluid_side = SDIS_BACK;
  if(solid->type != SDIS_SOLID) {
    SWAP(struct sdis_medium*, solid, fluid);
    SWAP(enum sdis_side, solid_side, fluid_side);
    ASSERT(fluid->type == SDIS_FLUID);
  }

  /* Get the enclosures split by the boundary */
  scene_get_enclosure_ids(scn, rwalk->XD(hit).prim.prim_id, enc_ids);

  /* Check that no net flux is set for this interface since the provided
   * picardN algorithm does not handle it */
  res = check_net_flux(scn, interf, frag, get_picard_order(ctx));
  if(res != RES_OK) goto error;

  /* Setup a fragment for the fluid side */
  frag_fluid = *frag;
  frag_fluid.side = fluid_side;

  /* Fetch the solid properties */
  lambda = solid_get_thermal_conductivity(solid, &rwalk->vtx);
  delta = solid_get_delta(solid, &rwalk->vtx);

  /* Fetch the boundary emissivity */
  epsilon = interface_side_get_emissivity
    (interf, SDIS_INTERN_SOURCE_ID, &frag_fluid);

  /* Note that the reinjection distance is *FIXED*. It MUST ensure that the
   * orthogonal distance from the boundary to the reinjection point is at most
   * equal to delta. */
  delta_boundary = sqrt(DIM) * delta;

  /* Sample a reinjection step */
  samp_reinject_step_args.rng = rng;
  samp_reinject_step_args.solid_enc_id = enc_ids[solid_side];
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
  h_radi_hat = epsilon > 0 ? 4.0 * BOLTZMANN_CONSTANT * That3 * epsilon : 0;

  if(epsilon <= 0) {
    h_radi_hat = 0; /* No radiative transfert */
  } else {
    res = scene_check_temperature_range(scn);
    if(res != RES_OK) { res = RES_BAD_OP_IRRECOVERABLE; goto error; }
    h_radi_hat = 4.0 * BOLTZMANN_CONSTANT * That3 * epsilon;
  }

  /* Compute a global upper bound coefficient */
  h_hat = h_conv + h_cond + h_radi_hat;

  /* Compute the probas to switch in convection or conduction */
  p_conv = h_conv / h_hat;
  p_cond = h_cond / h_hat;

  /* Fetch the last registered heat path vertex */
  if(ctx->heat_path) hvtx = *heat_path_get_last_vertex(ctx->heat_path);

  /* Null collision main loop */
  for(;;) {
    /* Temperature and random walk state of the sampled radiative path */
    struct temperature T_s;
    struct rwalk rwalk_s;

    double h_radi, h_radi_min, h_radi_max; /* Radiative coefficients */
    double p_radi, p_radi_min, p_radi_max; /* Radiative probas */
    double T0, T1, T2, T3, T4, T5; /* Computed temperatures */

    /* Indices of the registered vertex of the sampled radiative path */
    size_t ihvtx_radi_begin = 0;
    size_t ihvtx_radi_end = 0;

    r = ssp_rng_canonical(rng);

    /* Switch in convective path */
    if(r < p_conv) {
      T->func = XD(convective_path);
      rwalk->enc_id = enc_ids[fluid_side];
      rwalk->hit_side = fluid_side;
      break;
    }

    /* Switch in conductive path */
    if(r < p_conv + p_cond) {
      struct solid_reinjection_args solid_reinject_args =
        SOLID_REINJECTION_ARGS_NULL;

      /* Perform the reinjection into the solid */
      solid_reinject_args.reinjection = &reinject_step;
      solid_reinject_args.rwalk_ctx = ctx;
      solid_reinject_args.rwalk = rwalk;
      solid_reinject_args.rng = rng;
      solid_reinject_args.T = T;
      solid_reinject_args.fp_to_meter = scn->fp_to_meter;
      res = XD(solid_reinjection)(scn, enc_ids[solid_side], &solid_reinject_args);
      if(res != RES_OK) goto error;
      break;
    }

    if(ctx->heat_path) {
      /* Fetch the index of the first vertex of the radiative path that is
       * going to be traced i.e. the last registered vertex */
      ihvtx_radi_begin = heat_path_get_vertices_count(ctx->heat_path) - 1;
    }

    /* Sample a radiative path */
    T_s = *T;
    rwalk_s = *rwalk;
    rwalk_s.enc_id = enc_ids[fluid_side];
    rwalk_s.hit_side = fluid_side;
    res = XD(radiative_path)(scn, ctx, &rwalk_s, rng, &T_s);
    if(res != RES_OK) goto error;

    if(ctx->heat_path) {
      /* Fetch the index after the last registered vertex of the sampled
       * radiative path */
      ihvtx_radi_end = heat_path_get_vertices_count(ctx->heat_path);
    }

    /* Fetch the last registered heat path vertex of the radiative path */
    if(ctx->heat_path) hvtx_s = *heat_path_get_last_vertex(ctx->heat_path);

    h_radi_min = 4.0 * BOLTZMANN_CONSTANT * Tmin3 * epsilon;
    p_radi_min = h_radi_min / h_hat;

    /* Switch in radiative path */
    if(r < p_conv + p_cond + p_radi_min) { *rwalk = rwalk_s; *T = T_s; break; }

    /* Define some helper macros */
    #define SWITCH_IN_RADIATIVE {                                              \
      *rwalk = rwalk_s; *T = T_s;                                              \
      res = heat_path_restart(ctx->heat_path, &hvtx_s);                        \
      if(res != RES_OK) goto error;                                            \
    } (void)0

    #define NULL_COLLISION {                                                   \
      res = heat_path_restart(ctx->heat_path, &hvtx);                          \
      if(res != RES_OK) goto error;                                            \
      if(ctx->heat_path) {                                                     \
        heat_path_increment_sub_path_branch_id                                 \
          (ctx->heat_path, ihvtx_radi_begin, ihvtx_radi_end);                  \
      }                                                                        \
    } (void)0

    #define COMPUTE_TEMPERATURE(Result, RWalk, Temp) {                         \
      struct temperature T_p;                                                  \
      if((Temp)->done) { /* Ambient radiative temperature */                   \
        ASSERT(SXD_HIT_NONE(&(RWalk)->XD(hit)));                               \
        T_p = *(Temp);                                                         \
      } else {                                                                 \
        res = XD(sample_path)(scn, RWalk, ctx, rng, &T_p);                     \
        if(res != RES_OK) goto error;                                          \
      }                                                                        \
      Result = T_p.value;                                                      \
    } (void)0

    #define CHECK_PMIN_PMAX {                                                  \
      p_radi_min = h_radi_min*epsilon / h_hat;                                 \
      p_radi_max = h_radi_max*epsilon / h_hat;                                 \
      if(r < p_conv + p_cond + p_radi_min) { SWITCH_IN_RADIATIVE; break; }     \
      if(r > p_conv + p_cond + p_radi_max) { NULL_COLLISION; continue; }       \
    } (void)0

    /* Sample a 1st heat path at the end of the radiative path */
    COMPUTE_TEMPERATURE(T0, &rwalk_s, &T_s);
    h_radi_min = BOLTZMANN_CONSTANT*(Tmin3 + 3*Tmin2*T0);
    h_radi_max = BOLTZMANN_CONSTANT*(That3 + 3*That2*T0);
    CHECK_PMIN_PMAX;

    /* Sample a 2nd heat path at the end of the radiative path */
    COMPUTE_TEMPERATURE(T1, &rwalk_s, &T_s);
    h_radi_min = BOLTZMANN_CONSTANT*(Tmin3 + Tmin2*T0 + 2*Tmin*T0*T1);
    h_radi_max = BOLTZMANN_CONSTANT*(That3 + That2*T0 + 2*That*T0*T1);
    CHECK_PMIN_PMAX;

    /* Sample a 3rd heat path at the end of the radiative path */
    COMPUTE_TEMPERATURE(T2, &rwalk_s, &T_s);
    h_radi_min = BOLTZMANN_CONSTANT*(Tmin3 + Tmin2*T0 + Tmin*T0*T1 + T0*T1*T2);
    h_radi_max = BOLTZMANN_CONSTANT*(That3 + That2*T0 + That*T0*T1 + T0*T1*T2);
    CHECK_PMIN_PMAX;

    /* Sample a 1st heat path at the current position onto the interface */
    COMPUTE_TEMPERATURE(T3, rwalk, T);
    h_radi_min = BOLTZMANN_CONSTANT*(Tmin2*T3 + Tmin*T0*T3 + T0*T1*T3 + T0*T1*T2);
    h_radi_max = BOLTZMANN_CONSTANT*(That2*T3 + That*T0*T3 + T0*T1*T3 + T0*T1*T2);
    CHECK_PMIN_PMAX;

    /* Sample a 2nd heat path at the current position onto the interface */
    COMPUTE_TEMPERATURE(T4, rwalk, T);
    h_radi_min = BOLTZMANN_CONSTANT*(Tmin*T3*T4 + T0*T3*T4 + T0*T1*T3 + T0*T1*T2);
    h_radi_max = BOLTZMANN_CONSTANT*(That*T3*T4 + T0*T3*T4 + T0*T1*T3 + T0*T1*T2);
    CHECK_PMIN_PMAX;

    /* Sample a 3rd heat path at the current position onto the interface */
    COMPUTE_TEMPERATURE(T5, rwalk, T);
    h_radi = BOLTZMANN_CONSTANT*(T3*T4*T5 + T0*T3*T4 + T0*T1*T3 + T0*T1*T2);
    p_radi = h_radi * epsilon / h_hat;

    /* Switch in radiative path */
    if(r < p_cond + p_conv + p_radi) { SWITCH_IN_RADIATIVE; break; }

    /* Null-collision, looping at the beginning */
    NULL_COLLISION;

    #undef SWITCH_IN_RADIATIVE
    #undef NULL_COLLISION
    #undef COMPUTE_TEMPERATURE
    #undef CHECK_PMIN_PMAX
  }

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"

