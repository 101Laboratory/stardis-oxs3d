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
  (const struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   const double phi,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  struct XD(rwalk) rwalk_saved;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* mdm = NULL;
  double lambda;
  double delta;
  double delta_boundary;
  double delta_in_meter;
  double power;
  double tmp;
  struct sXd(hit) hit;
  float dir0[DIM];
  float dir1[DIM];
  float reinject_dst;
  /* In 2D it is useless to try to resample a reinjection direction since there
   * is only one possible direction */
  const int MAX_ATTEMPTS = DIM == 2 ? 1 : 10;
  int iattempt = 0;
  int reinjection_is_valid = 0;
  res_T res = RES_OK;
  ASSERT(frag && phi != SDIS_FLUX_NONE);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag));
  (void)ctx;

  /* Fetch current interface  */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  ASSERT(phi == interface_side_get_flux(interf, frag));

  /* Fetch incoming solid */
  mdm = interface_get_medium(interf, frag->side);
  ASSERT(mdm->type == SDIS_SOLID);

  /* Fetch medium properties */
  lambda = solid_get_thermal_conductivity(mdm, &rwalk->vtx);
  delta = solid_get_delta(mdm, &rwalk->vtx);

  /* Compute the reinjection distance. It MUST ensure that the orthogonal
   * distance from the boundary to the point to chalenge is equal to delta. */
  delta_boundary = delta * sqrt(DIM);

  rwalk_saved = *rwalk;
  reinjection_is_valid = 0;
  iattempt = 0;
  do {
    if(iattempt != 0) *rwalk = rwalk_saved;
    /* Sample a reinjection direction */
    XD(sample_reinjection_dir)(rwalk, rng, dir0);

    /* Reflect the sampled direction around the normal */
    XD(reflect)(dir1, dir0, rwalk->hit.normal);

    if(frag->side == SDIS_BACK) {
      fX(minus)(dir0, dir0);
      fX(minus)(dir1, dir1);
    }

    /* Select the reinjection direction and distance */
    res = XD(select_reinjection_dir_and_check_validity)(scn, mdm, rwalk, dir0,
      dir1, delta_boundary, dir0, &reinject_dst, 1, NULL,
      &reinjection_is_valid, &hit);
    if(res != RES_OK) goto error;

  } while(!reinjection_is_valid && ++iattempt < MAX_ATTEMPTS);

  /* Could not find a valid reinjecton */
  if(iattempt >= MAX_ATTEMPTS) {
    *rwalk = rwalk_saved;
    log_warn(scn->dev,
      "%s: could not find a valid solid/fluid with flux reinjection "
      "at {%g, %g, %g}.\n", FUNC_NAME, SPLIT3(rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  /* Define the orthogonal dst from the reinjection pos to the interface */
  delta = reinject_dst / sqrt(DIM);

  /* Handle the flux */
  delta_in_meter = delta * scn->fp_to_meter;
  tmp = delta_in_meter / lambda;
  T->value += phi * tmp;
  if(ctx->green_path) {
    res = green_path_add_flux_term(ctx->green_path, interf, frag, tmp);
    if(res != RES_OK) goto error;
  }

  /* Handle the volumic power */
  power = solid_get_volumic_power(mdm, &rwalk->vtx);
  if(power != SDIS_VOLUMIC_POWER_NONE) {
    delta_in_meter = reinject_dst * scn->fp_to_meter;
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

  /* Reinject. If the reinjection move the point too close of a boundary,
   * assume that the zone is isotherm and move to the boundary. */
  XD(move_pos)(rwalk->vtx.P, dir0, reinject_dst);
  if(hit.distance == reinject_dst) {
    T->func = XD(boundary_path);
    rwalk->mdm = NULL;
    rwalk->hit = hit;
    rwalk->hit_side = fX(dot)(hit.normal, dir0) < 0 ? SDIS_FRONT : SDIS_BACK;
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
