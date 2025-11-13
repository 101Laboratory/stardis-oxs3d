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
#include "sdis_radiative_env_c.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE res_T
XD(rwalk_get_Tref)
  (const struct sdis_scene* scn,
   const struct rwalk* rwalk,
   const struct temperature* T,
   double* out_Tref)
{
  double Tref = SDIS_TEMPERATURE_NONE;
  res_T res = RES_OK;
  ASSERT(rwalk && T && out_Tref);

  if(T->done) {
    /* The path reaches a limit condition, i.e. it goes to the infinity and
     * fetches the ambient radiative temperature. We do not use the limit
     * conditions as the reference temperature to make the sampled paths
     * independant of them. */
    struct sdis_radiative_ray ray = SDIS_RADIATIVE_RAY_NULL;
    ray.dir[0] = rwalk->dir[0];
    ray.dir[1] = rwalk->dir[1];
    ray.dir[2] = rwalk->dir[2];
    ray.time = rwalk->vtx.time;
    Tref = radiative_env_get_reference_temperature(scn->radenv, &ray);
  } else {
    struct sdis_interface_fragment frag;
    struct sdis_interface* interf = NULL;
    ASSERT(!SXD_HIT_NONE(&rwalk->XD(hit)));

    /* Fetch the interface where the random walk ends */
    interf = scene_get_interface(scn, rwalk->XD(hit).prim.prim_id);
    ASSERT(rwalk->hit_side!=SDIS_FRONT || interf->medium_front->type==SDIS_FLUID);
    ASSERT(rwalk->hit_side!=SDIS_BACK || interf->medium_back->type==SDIS_FLUID);

    /* Fragment on the fluid side of the boundary onto which the rwalk ends */
    XD(setup_interface_fragment)
      (&frag, &rwalk->vtx, &rwalk->XD(hit), rwalk->hit_side);

    Tref = interface_side_get_reference_temperature(interf, &frag);
  }

  res = XD(check_Tref)(scn, rwalk->vtx.P, Tref, FUNC_NAME);
  if(res != RES_OK) goto error;

exit:
  *out_Tref = Tref;
  return res;
error:
  Tref = -1;
  goto exit;
}

/*******************************************************************************
 * Boundary path between a solid and a fluid
 ******************************************************************************/
res_T
XD(solid_fluid_boundary_picard1_path)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  /* Input argument used to handle the net flux */
  struct handle_net_flux_args handle_net_flux_args = HANDLE_NET_FLUX_ARGS_NULL;

  /* Input argument used to handle the external net flux */
  struct handle_external_net_flux_args handle_external_net_flux_args =
    HANDLE_EXTERNAL_NET_FLUX_ARGS_NULL;

  /* Input/output arguments of the function used to sample a reinjection */
  struct sample_reinjection_step_args samp_reinject_step_args =
    SAMPLE_REINJECTION_STEP_ARGS_NULL;
  struct reinjection_step reinject_step = REINJECTION_STEP_NULL;

  /* Temperature and random walk state of the sampled radiative path */
  struct temperature T_s;
  struct rwalk rwalk_s;

  /* Fragment on the fluid side of the boundary */
  struct sdis_interface_fragment frag_fluid;

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

  double epsilon; /* Interface emissivity */
  double Tref; /* Reference temperature */
  double Tref_s; /* Reference temperature of the sampled radiative path */
  double lambda; /* Solid conductivity */
  double delta_boundary; /* Orthogonal reinjection dst at the boundary */
  double delta; /* Orthogonal fitted reinjection dst at the boundary */
  double delta_m; /* delta in meter */

  double r;
  struct sdis_heat_vertex hvtx = SDIS_HEAT_VERTEX_NULL;
  enum sdis_side solid_side = SDIS_SIDE_NULL__;
  enum sdis_side fluid_side = SDIS_SIDE_NULL__;
  res_T res = RES_OK;

  ASSERT(scn && rwalk && rng && T && ctx);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag) == RES_OK);

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

  /* Setup a fragment for the fluid side */
  frag_fluid = *frag;
  frag_fluid.side = fluid_side;

  /* Fetch the solid properties */
  lambda = solid_get_thermal_conductivity(solid, &rwalk->vtx);
  delta = solid_get_delta(solid, &rwalk->vtx);

  /* Fetch the boundary emissivity */
  epsilon = interface_side_get_emissivity
    (interf, SDIS_INTERN_SOURCE_ID, &frag_fluid);

  if(epsilon <= 0) {
    Tref = 0;
  } else {
    /* Check the Tref */
    Tref = interface_side_get_reference_temperature(interf, &frag_fluid);
    res = XD(check_Tref)(scn, frag_fluid.P, Tref, FUNC_NAME);
    if(res != RES_OK) goto error;
  }

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
  delta_m = delta * scn->fp_to_meter;

  /* Compute the convective, conductive and the upper bound radiative coef */
  h_conv = interface_get_convection_coef(interf, frag);
  h_cond = lambda / delta_m;
  if(epsilon <= 0) {
    h_radi_hat = 0; /* No radiative transfert */
  } else {
    res = scene_check_temperature_range(scn);
    if(res != RES_OK) { res = RES_BAD_OP_IRRECOVERABLE; goto error; }
    h_radi_hat = 4.0 * BOLTZMANN_CONSTANT * ctx->That3 * epsilon;
  }

  /* Compute a global upper bound coefficient */
  h_hat = h_conv + h_cond + h_radi_hat;

  /* Compute the probas to switch in solid, fluid or radiative random walk */
  p_conv = h_conv / h_hat;
  p_cond = h_cond / h_hat;

  /* Handle the net flux if any */
  handle_net_flux_args.interf = interf;
  handle_net_flux_args.frag = frag;
  handle_net_flux_args.green_path = ctx->green_path;
  handle_net_flux_args.picard_order = get_picard_order(ctx);
  handle_net_flux_args.h_cond = h_cond;
  handle_net_flux_args.h_conv = h_conv;
  handle_net_flux_args.h_radi = h_radi_hat;
  res = XD(handle_net_flux)(scn, &handle_net_flux_args, T);
  if(res != RES_OK) goto error;

  /* Handle the external net flux if any */
  handle_external_net_flux_args.interf = interf;
  handle_external_net_flux_args.frag = frag;
  handle_external_net_flux_args.XD(hit) = &rwalk->XD(hit);
  handle_external_net_flux_args.green_path = ctx->green_path;
  handle_external_net_flux_args.picard_order = get_picard_order(ctx);
  handle_external_net_flux_args.h_cond = h_cond;
  handle_external_net_flux_args.h_conv = h_conv;
  handle_external_net_flux_args.h_radi = h_radi_hat;
  res = XD(handle_external_net_flux)(scn, rng, &handle_external_net_flux_args, T);
  if(res != RES_OK) goto error;

  /* Fetch the last registered heat path vertex */
  if(ctx->heat_path) hvtx = *heat_path_get_last_vertex(ctx->heat_path);

  /* Null collision */
  for(;;) {
    double h_radi; /* Radiative coefficient */
    double p_radi; /* Radiative proba */

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

    /* From there, we know the path is either a radiative path or a
     * null-collision */

    if(ctx->heat_path) {
      /* Fetch the index of the first vertex of the radiative path that is
       * going to be traced i.e. the last registered vertex */
      ihvtx_radi_begin = heat_path_get_vertices_count(ctx->heat_path) - 1;
    }

    /* Sample a radiative path and get the Tref at its end. */
    T_s = *T;
    rwalk_s = *rwalk;
    rwalk_s.enc_id = enc_ids[fluid_side];
    rwalk_s.hit_side = fluid_side;
    res = XD(radiative_path)(scn, ctx, &rwalk_s, rng, &T_s);
    if(res != RES_OK) goto error;

    /* Get the Tref at the end of the candidate radiative path */
    res = XD(rwalk_get_Tref)(scn, &rwalk_s, &T_s, &Tref_s);
    if(res != RES_OK) goto error;

    /* The reference temperatures must be known, as this is a radiative path.
     * If this is not the case, an error should be reported before this point.
     * Hence these assertions to detect unexpected behavior */
    ASSERT(SDIS_TEMPERATURE_IS_KNOWN(Tref));
    ASSERT(SDIS_TEMPERATURE_IS_KNOWN(Tref_s));

    h_radi = BOLTZMANN_CONSTANT * epsilon *
      ( Tref*Tref*Tref
      + Tref*Tref * Tref_s
      + Tref * Tref_s*Tref_s
      + Tref_s*Tref_s*Tref_s);

    p_radi = h_radi / h_hat;
    if(r < p_conv + p_cond + p_radi) { /* Radiative path */
      *rwalk = rwalk_s;
      *T = T_s;
      break;

    /* Null collision: the sampled path is rejected. */
    } else {

      if(ctx->green_path) {
        /* The limit condition of the green path could be set by the rejected
         * sampled radiative path. Reset this limit condition. */
        green_path_reset_limit(ctx->green_path);
      }

      if(ctx->heat_path) {
        /* Set the sampled radiative path as a branch of the current path */
        ihvtx_radi_end = heat_path_get_vertices_count(ctx->heat_path);
        heat_path_increment_sub_path_branch_id
          (ctx->heat_path, ihvtx_radi_begin, ihvtx_radi_end);

        /* Add a break into the heat path geometry and restart it from the
         * position of the input random walk. */
        res = heat_path_restart(ctx->heat_path, &hvtx);
        if(res != RES_OK) goto error;
      }
    }

    /* Null-collision, looping at the beginning */
  }

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
