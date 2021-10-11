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
 * Boundary path between a solid and a fluid
 ******************************************************************************/
res_T
XD(solid_solid_boundary_path)
  (const struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  struct sXd(hit) hit0, hit1;
  struct sXd(hit)* hit;
  struct XD(rwalk) rwalk_saved;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* solid_front = NULL;
  struct sdis_medium* solid_back = NULL;
  struct sdis_medium* mdm;
  double lambda_front, lambda_back;
  double delta_front, delta_back;
  double delta_boundary_front, delta_boundary_back;
  double proba;
  double tmp;
  double r;
  double power;
  double tcr;
  float dir0[DIM], dir1[DIM], dir2[DIM], dir3[DIM];
  float dir_front[DIM], dir_back[DIM];
  float* dir;
  float reinject_dst_front = 0, reinject_dst_back = 0;
  float reinject_dst;
  /* In 2D it is useless to try to resample a reinjection direction since there
   * is only one possible direction */
  const int MAX_ATTEMPTS = DIM == 2 ? 1 : 10;
  int iattempt;
  int move;
  int reinjection_is_valid;
  res_T res = RES_OK;
  ASSERT(scn && ctx && frag && rwalk && rng && T);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag));
  (void)frag, (void)ctx;

  /* Retrieve the current boundary media */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  solid_front = interface_get_medium(interf, SDIS_FRONT);
  solid_back = interface_get_medium(interf, SDIS_BACK);
  ASSERT(solid_front->type == SDIS_SOLID);
  ASSERT(solid_back->type == SDIS_SOLID);

  /* Retrieve the thermal contact resistance */
  tcr = interface_get_thermal_contact_resistance(interf, frag);

  /* Fetch the properties of the media */
  lambda_front = solid_get_thermal_conductivity(solid_front, &rwalk->vtx);
  lambda_back = solid_get_thermal_conductivity(solid_back, &rwalk->vtx);

  /* Note that reinjection distance is *FIXED*. It MUST ensure that the orthogonal
   * distance from the boundary to the point to challenge is equal to delta. */
  delta_front = solid_get_delta(solid_front, &rwalk->vtx);
  delta_back = solid_get_delta(solid_back, &rwalk->vtx);
  delta_boundary_front = delta_front*sqrt(DIM);
  delta_boundary_back = delta_back *sqrt(DIM);

  rwalk_saved = *rwalk;
  reinjection_is_valid = 0;
  iattempt = 0;
  do {
    if(iattempt != 0) *rwalk = rwalk_saved;

    /* Sample a reinjection direction and reflect it around the normal. Then
     * reflect them on the back side of the interface. */
    XD(sample_reinjection_dir)(rwalk, rng, dir0);
    XD(reflect)(dir2, dir0, rwalk->hit.normal);
    fX(minus)(dir1, dir0);
    fX(minus)(dir3, dir2);

    /* Select the reinjection direction and distance for the front side */
    res = XD(select_reinjection_dir_and_check_validity)(scn, solid_front, rwalk,
      dir0, dir2, delta_boundary_front, dir_front, &reinject_dst_front, 1, &move,
      &reinjection_is_valid, &hit0);
    if(res != RES_OK) goto error;
    if(!reinjection_is_valid) continue;

    /* Select the reinjection direction and distance for the back side */
    res = XD(select_reinjection_dir_and_check_validity)(scn, solid_back, rwalk,
      dir1, dir3, delta_boundary_back, dir_back, &reinject_dst_back, 1, &move,
      &reinjection_is_valid, &hit1);
    if(res != RES_OK) goto error;
    if(!reinjection_is_valid) continue;

    /* If random walk was moved by the select_reinjection_dir on back side, one
     * has to rerun the select_reinjection_dir on front side at the new pos */
    if(move) {
      res = XD(select_reinjection_dir_and_check_validity)(scn, solid_front,
        rwalk, dir0, dir2, delta_boundary_front, dir_front, &reinject_dst_front,
        0, NULL, &reinjection_is_valid, &hit0);
      if(res != RES_OK) goto error;
      if(!reinjection_is_valid) continue;
    }
  } while(!reinjection_is_valid && ++iattempt < MAX_ATTEMPTS);

  /* Could not find a valid reinjection */
  if(iattempt >= MAX_ATTEMPTS) {
    *rwalk = rwalk_saved;
    log_warn(scn->dev,
      "%s: could not find a valid solid/solid reinjection at {%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  r = ssp_rng_canonical(rng);
  if(tcr == 0) { /* No thermal contact resistance */
    /* Define the reinjection side. Note that the proba should be : Lf/Df' /
     * (Lf/Df' + Lb/Db')
     *
     * with L<f|b> the lambda of the <front|back> side and D<f|b>' the adjusted
     * delta of the <front|back> side, i.e. : D<f|b>' =
     * reinject_dst_<front|back> / sqrt(DIM)
     *
     * Anyway, one can avoid to compute the adjusted delta by directly using the
     * adjusted reinjection distance since the resulting proba is strictly the
     * same; sqrt(DIM) can be simplified. */
    proba = (lambda_front/reinject_dst_front)
      / (lambda_front/reinject_dst_front + lambda_back/reinject_dst_back);
  } else {
    const double df = reinject_dst_front/sqrt(DIM);
    const double db = reinject_dst_back/sqrt(DIM);
    const double tmp_front = lambda_front/df;
    const double tmp_back = lambda_back/db;
    const double tmp_r = tcr*tmp_front*tmp_back;
    switch(rwalk->hit_side) {
      case SDIS_BACK:
        /* When coming from the BACK side, the probability to be reinjected on
         * the FRONT side depends on the thermal contact resistance: it
         * decreases when the TCR increases (and tends to 0 when TCR -> +inf) */
        proba = (tmp_front) / (tmp_front + tmp_back + tmp_r);
        break;
      case SDIS_FRONT:
        /* Same thing when coming from the FRONT side: the probability of
         * reinjection on the FRONT side depends on the thermal contact
         * resistance: it increases when the TCR increases (and tends to 1 when
         * the TCR -> +inf) */
        proba = (tmp_front + tmp_r) / (tmp_front + tmp_back + tmp_r);
        break;
      default: FATAL("Unreachable code.\n"); break;
    }
  }

  if(r < proba) { /* Reinject in front */
    dir = dir_front;
    hit = &hit0;
    mdm = solid_front;
    reinject_dst = reinject_dst_front;
  } else { /* Reinject in back */
    dir = dir_back;
    hit = &hit1;
    mdm = solid_back;
    reinject_dst = reinject_dst_back;
  }

  /* Handle the volumic power */
  power = solid_get_volumic_power(mdm, &rwalk->vtx);
  if(power != SDIS_VOLUMIC_POWER_NONE) {
    const double delta_in_meter = reinject_dst * scn->fp_to_meter;
    const double lambda = solid_get_thermal_conductivity(mdm, &rwalk->vtx);
    tmp = delta_in_meter * delta_in_meter / (2.0 * DIM * lambda);
    T->value += power * tmp;

    if(ctx->green_path) {
      res = green_path_add_power_term(ctx->green_path, mdm, &rwalk->vtx, tmp);
      if(res != RES_OK) goto error;
    }
  }

  /* Time rewind */
  res = XD(time_rewind)(mdm, rng, reinject_dst * scn->fp_to_meter, ctx, rwalk, T);
  if(res != RES_OK) goto error;
  if(T->done) goto exit; /* Limit condition was reached */

  /* Perform reinjection. */
  XD(move_pos)(rwalk->vtx.P, dir, (float)reinject_dst);
  if(hit->distance == reinject_dst) {
    T->func = XD(boundary_path);
    rwalk->mdm = NULL;
    rwalk->hit = *hit;
    rwalk->hit_side = fX(dot)(hit->normal, dir) < 0 ? SDIS_FRONT : SDIS_BACK;
  } else {
    T->func = XD(conductive_path);
    rwalk->mdm = mdm;
    rwalk->hit = SXD_HIT_NULL;
    rwalk->hit_side = SDIS_SIDE_NULL__;
  }

  /* Register the new vertex against the heat path */
  res = register_heat_vertex
    (ctx->heat_path, &rwalk->vtx, T->value, SDIS_HEAT_VERTEX_CONDUCTION);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
