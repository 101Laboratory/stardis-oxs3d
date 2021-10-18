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
XD(solid_fluid_boundary_picard1_path)
  (const struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  struct sdis_interface* interf = NULL;
  struct sdis_medium* mdm_front = NULL;
  struct sdis_medium* mdm_back = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct XD(rwalk) rwalk_saved;
  struct sXd(hit) hit = SXD_HIT_NULL;
  struct sdis_interface_fragment frag_fluid;
  double hc;
  double hr;
  double epsilon; /* Interface emissivity */
  double lambda;
  double fluid_proba;
  double radia_proba;
  double delta;
  double delta_boundary;
  double r;
  double tmp;
  float dir0[DIM], dir1[DIM];
  float reinject_dst;
  /* In 2D it is useless to try to resample a reinjection direction since there
   * is only one possible direction */
  const int MAX_ATTEMPTS = DIM == 2 ? 1 : 10;
  int iattempt;
  int reinjection_is_valid = 0;
  res_T res = RES_OK;
  ASSERT(scn && rwalk && rng && T && ctx);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag));

  /* Retrieve the solid and the fluid split by the boundary */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  mdm_front = interface_get_medium(interf, SDIS_FRONT);
  mdm_back = interface_get_medium(interf, SDIS_BACK);
  ASSERT(mdm_front->type != mdm_back->type);

  /* Setup the fluid side fragment */
  frag_fluid = *frag;
  if(mdm_front->type == SDIS_SOLID) {
    solid = mdm_front;
    fluid = mdm_back;
    frag_fluid.side = SDIS_BACK;
  } else {
    solid = mdm_back;
    fluid = mdm_front;
    frag_fluid.side = SDIS_FRONT;
  }

  /* Fetch the boundary properties */
  epsilon = interface_side_get_emissivity(interf, &frag_fluid);
  Tref = interface_side_get_reference_temperature(interf, &frag_fluid);

  /* Fetch the solid properties */
  lambda = solid_get_thermal_conductivity(solid, &rwalk->vtx);
  delta = solid_get_delta(solid, &rwalk->vtx);

  /* Note that the reinjection distance is *FIXED*. It MUST ensure that the
   * orthogonal distance from the boundary to the point to chalenge is equal to
   * delta. */
  delta_boundary = sqrt(DIM) * delta;

  rwalk_saved = *rwalk;
  reinjection_is_valid = 0;
  iattempt = 0;
  do {
    if(iattempt != 0) *rwalk = rwalk_saved;

    /* Sample a reinjection direction */
    XD(sample_reinjection_dir)(rwalk, rng, dir0);

    /* Reflect the sampled direction around the normal */
    XD(reflect)(dir1, dir0, rwalk->hit.normal);

    if(solid == mdm_back) {
      fX(minus)(dir0, dir0);
      fX(minus)(dir1, dir1);
    }

    /* Select the solid reinjection direction and distance */
    res = XD(select_reinjection_dir_and_check_validity)(scn, solid, rwalk,
      dir0, dir1, delta_boundary, dir0, &reinject_dst, 1, NULL,
      &reinjection_is_valid, &hit);
    if(res != RES_OK) goto error;

  } while(!reinjection_is_valid && ++iattempt < MAX_ATTEMPTS);

  /* Could not find a valid reinjecton */
  if(iattempt >= MAX_ATTEMPTS) {
    *rwalk = rwalk_saved;
    log_warn(scn->dev,
      "%s: could not find a valid solid/fluid reinjection at {%g, %g %g}.\n",
      FUNC_NAME, SPLIT3(rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  /* Define the orthogonal dst from the reinjection pos to the interface */
  delta = reinject_dst / sqrt(DIM);

  /* Compute the convective, conductive and the upper bound radiative coef */
  h_conv = interface_get_convection_coef(interf, frag);
  h_cond = lambda / (delta * scn->fp_to_meter);
  h_rad_hat = 4.0 * BOLTZMANN_CONSTANT * ctx->That3 * epsilon;
  
  /* Compute a global upper bound coefficient */
  h_hat = h_conv + h_cond + h_rad_hat;

  /* Compute the probas to switch in solid, fluid or radiative random walk */
  p_conv = h_conv / h_hat;
  p_cond = h_cond / h_hat;

  /* Null collision */
  for(;;) {
    r = ssp_rng_canonical(rng); 

    /* Switch in convective path */
    if(r < p_conv) {
      T->func = XD(convective_path);
      rwalk->mdm = fluid;
      rwalk->hit_side = rwalk->mdm == mdm_front ? SDIS_FRONT : SDIS_BACK;
      break;
    }

    /* Switch in conductive path */
    if(r < p_conv + p_cond) {
      /* Handle the volumic power */
      const double power = solid_get_volumic_power(solid, &rwalk->vtx);
      if(power != SDIS_VOLUMIC_POWER_NONE) {
        const double delta_in_meter = reinject_dst * scn->fp_to_meter;
        tmp = delta_in_meter * delta_in_meter / (2.0 * DIM * lambda);
        T->value += power * tmp;

        if(ctx->green_path) {
          res = green_path_add_power_term(ctx->green_path, solid, &rwalk->vtx, tmp);
          if(res != RES_OK) goto error;
        }
      }

      /* Time rewind */
      res = XD(time_rewind)(solid, rng, reinject_dst * scn->fp_to_meter, ctx, rwalk, T);
      if(res != RES_OK) goto error;
      if(T->done) goto exit; /* Limit condition was reached */

      /* Perform solid reinjection */
      XD(move_pos)(rwalk->vtx.P, dir0, reinject_dst);
      if(hit.distance == reinject_dst) {
        T->func = XD(boundary_path);
        rwalk->mdm = NULL;
        rwalk->hit = hit;
        rwalk->hit_side = fX(dot)(hit.normal, dir0) < 0 ? SDIS_FRONT : SDIS_BACK;
      } else {
        T->func = XD(conductive_path);
        rwalk->mdm = solid;
        rwalk->hit = SXD_HIT_NULL;
        rwalk->hit_side = SDIS_SIDE_NULL__;
      }

      /* Register the new vertex against the heat path */
      res = register_heat_vertex
        (ctx->heat_path, &rwalk->vtx, T->value, SDIS_HEAT_VERTEX_CONDUCTION);
      if(res != RES_OK) goto error;
      break;
    }

    /* From there, we know the path is either a radiative path or a
     * null-collision */

    /* Trace a candidate radiative path and get the Tref at its end.
     * TODO handle the registration of the path geometry */
    T_candidate = *T;
    rwalk_candidate = *rwalk;
    res = XD(radiative_path)(scn, ctx, &rwalk_candidate, rng, T_candidate);
    if(res != RES_OK) goto error;

    /* Get the Tref at the end of the candidate radiative path */
    if(T_candidate->done) {
      Tref_candidate = T_candidate->value;
    } else {
      ASSERT(!SXD_HIT_NONE(rwalk_candidate->hit));
      XD(setup_interface_fragment)
        (&frag_candidate, &rwalk_candidate->vtx, &rwalk_candidate->hit,
         rwalk_candidate->hit_side);
      interf_candidate = scene_get_interface
        (scn, rwalk_candidate->hit.prim.prim_id);

      Tref_candidate = interface_side_get_reference_temperature(interf, f&rag);
    }

    if(Tref_candidate < 0) {
      log_err(scn->dev,
        "%s: invalid reference temperature `%gK' at the position `%g %g %g'.\n",
        FUNC_NAME, Tref_candidate, SPLIT3(rwalk_candidate->vtx.P));
      res = RES_BAD_OP_IRRECOVERABLE;
      goto error;
    }

    h_rad = BOLTZMANN_CONSTANT
      * epsilon
      * ( Tref*Tref*Tref
        + Tref*Tref * Tref_candidate
        + Tref* Tref_candidate*Tref_candidate
        + Tref_candidate*Tref_candidate*Tref_candidate);

    p_rad = h_rad / h_hat;
    if(r < p_conv + p_cond + p_rad) { /* Radiative path */
      *rwalk = *rwalk_candidate;
      *T = *T_candidate;
      break;
    }

    /* Null-collision, looping at the beginning */
  }

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
