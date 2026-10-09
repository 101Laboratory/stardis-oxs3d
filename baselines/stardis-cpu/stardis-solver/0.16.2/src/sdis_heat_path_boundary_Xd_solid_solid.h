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
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  /* Input/output arguments of the function used to sample a reinjection */
  struct sample_reinjection_step_args samp_reinject_step_frt_args =
    SAMPLE_REINJECTION_STEP_ARGS_NULL;
  struct sample_reinjection_step_args samp_reinject_step_bck_args =
    SAMPLE_REINJECTION_STEP_ARGS_NULL;
  struct reinjection_step reinject_step_frt = REINJECTION_STEP_NULL;
  struct reinjection_step reinject_step_bck = REINJECTION_STEP_NULL;
  struct reinjection_step* reinject_step = NULL;

  /* Reinjection arguments */
  struct solid_reinjection_args solid_reinject_args =
    SOLID_REINJECTION_ARGS_NULL;

  /* Data attached to the boundary */
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
  struct sdis_interface* interf = NULL;
  struct sdis_medium* solid_frt = NULL;
  struct sdis_medium* solid_bck = NULL;
  unsigned solid_enc_id = ENCLOSURE_ID_NULL; /* Solid to re-inject */

  double lambda_frt;
  double lambda_bck;
  double delta_boundary_frt;
  double delta_boundary_bck;

  double proba;
  double r;
  double tcr;

  res_T res = RES_OK;
  ASSERT(scn && ctx && frag && rwalk && rng && T);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag) == RES_OK);
  (void)frag, (void)ctx;

  /* Retrieve the two enclosures and associated media split by the boundary */
  scene_get_enclosure_ids(scn, rwalk->XD(hit).prim.prim_id, enc_ids);
  interf = scene_get_interface(scn, rwalk->XD(hit).prim.prim_id);
  solid_frt = interface_get_medium(interf, SDIS_FRONT);
  solid_bck = interface_get_medium(interf, SDIS_BACK);
  ASSERT(solid_frt->type == SDIS_SOLID);
  ASSERT(solid_bck->type == SDIS_SOLID);

  /* Retrieve the thermal contact resistance */
  tcr = interface_get_thermal_contact_resistance(interf, frag);

  /* Fetch the properties of the media */
  lambda_frt = solid_get_thermal_conductivity(solid_frt, &rwalk->vtx);
  lambda_bck = solid_get_thermal_conductivity(solid_bck, &rwalk->vtx);

  /* Note that the reinjection distance is *FIXED*. It MUST ensure that the
   * orthogonal distance from the boundary to the reinjection point is at most
   * equal to delta. */
  delta_boundary_frt = solid_get_delta(solid_frt, &rwalk->vtx) * sqrt(DIM);
  delta_boundary_bck = solid_get_delta(solid_bck, &rwalk->vtx) * sqrt(DIM);

  /* Sample a front/back reinjection steps */
  samp_reinject_step_frt_args.rng = rng;
  samp_reinject_step_bck_args.rng = rng;
  samp_reinject_step_frt_args.solid_enc_id = enc_ids[SDIS_FRONT];
  samp_reinject_step_bck_args.solid_enc_id = enc_ids[SDIS_BACK];
  samp_reinject_step_frt_args.rwalk = rwalk;
  samp_reinject_step_bck_args.rwalk = rwalk;
  samp_reinject_step_frt_args.distance = delta_boundary_frt;
  samp_reinject_step_bck_args.distance = delta_boundary_bck;
  samp_reinject_step_frt_args.side = SDIS_FRONT;
  samp_reinject_step_bck_args.side = SDIS_BACK;
  res = XD(sample_reinjection_step_solid_solid)
    (scn,
     &samp_reinject_step_frt_args,
     &samp_reinject_step_bck_args,
     &reinject_step_frt,
     &reinject_step_bck);
  if(res != RES_OK) goto error;

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
    const double tmp_frt = lambda_frt / reinject_step_frt.distance;
    const double tmp_bck = lambda_bck / reinject_step_bck.distance;
    proba = tmp_frt / (tmp_frt + tmp_bck);
  } else {
    const double delta_frt = reinject_step_frt.distance/sqrt(DIM);
    const double delta_bck = reinject_step_bck.distance/sqrt(DIM);
    const double tmp_frt = lambda_frt/delta_frt;
    const double tmp_bck = lambda_bck/delta_bck;
    const double tmp_tcr = tcr*tmp_frt*tmp_bck;
    switch(rwalk->hit_side) {
      case SDIS_BACK:
        /* When coming from the BACK side, the probability to be reinjected on
         * the FRONT side depends on the thermal contact resistance: it
         * decreases when the TCR increases (and tends to 0 when TCR -> +inf) */
        proba = tmp_frt / (tmp_frt + tmp_bck + tmp_tcr);
        break;
      case SDIS_FRONT:
        /* Same thing when coming from the FRONT side: the probability of
         * reinjection on the FRONT side depends on the thermal contact
         * resistance: it increases when the TCR increases (and tends to 1 when
         * the TCR -> +inf) */
        proba = (tmp_frt + tmp_tcr) / (tmp_frt + tmp_bck + tmp_tcr);
        break;
      default: FATAL("Unreachable code.\n"); break;
    }
  }

  if(r < proba) { /* Reinject in front */
    reinject_step = &reinject_step_frt;
    solid_enc_id = enc_ids[SDIS_FRONT];
  } else { /* Reinject in back */
    reinject_step = &reinject_step_bck;
    solid_enc_id = enc_ids[SDIS_BACK];
  }

  /* Perform the reinjection into the solid */
  solid_reinject_args.reinjection = reinject_step;
  solid_reinject_args.rng = rng;
  solid_reinject_args.rwalk = rwalk;
  solid_reinject_args.rwalk_ctx = ctx;
  solid_reinject_args.T = T;
  solid_reinject_args.fp_to_meter = scn->fp_to_meter;
  res = XD(solid_reinjection)(scn, solid_enc_id, &solid_reinject_args);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
