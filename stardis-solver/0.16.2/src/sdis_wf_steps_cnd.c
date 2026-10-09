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


/* Wavefront steps: conductive -- delta-sphere (M4) + WoS (M9).  Split from sdis_wf_steps.c. */

#include "sdis_wf_steps.h"
#include "sdis.h"

#include <rsys/float33.h>   /* f33_rotation, f33_mulf3, f33_basis (B-4 M1) */
#include <rsys/rsys_math.h> /* PI */
#include "sdis_brdf.h"
#include "sdis_c.h"
#include "sdis_camera.h"
#include "sdis_device_c.h"
#include "sdis_estimator_buffer_c.h"
#include "sdis_green.h"
#include "sdis_heat_path_boundary_c.h"
#include "sdis_heat_path_conductive_c.h"
#include "sdis_interface_c.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_radiative_env_c.h"
#include "sdis_realisation.h"
#include "sdis_scene_c.h"
#include "sdis_source_c.h"
#include "sdis_tile.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <star/swf.h>  /* swf_tabulation_inverse, SWF_QUADRATIC (M9 WoS) */

/*******************************************************************************
 * B-4 M4: Delta-sphere conductive fine-grained step functions
 ******************************************************************************/

/* --- PATH_CND_DS_CHECK_TEMP: finalize init (once) + loop top --------------- */
LOCAL_SYM res_T
step_cnd_ds_check_temp(struct path_state* p, struct path_hot* hot, struct sdis_scene* scn,
                       struct path_enc_data* enc)
{
  res_T res = RES_OK;
  struct solid_props props = SOLID_PROPS_NULL;

  ASSERT(p && scn);

  /* Finalize initialisation from ENC resolve (runs only once per entry) */
  if(!p->locals.cnd_ds.initialized) {
    unsigned enc_id = enc->resolved_enc_id;
    struct sdis_medium* mdm = NULL;

    res = scene_get_enclosure_medium(
      scn, scene_get_enclosure(scn, enc_id), &mdm);
    if(res != RES_OK) goto error;

    /* Must be a solid medium */
    if(sdis_medium_get_type(mdm) != SDIS_SOLID) {
      log_err(scn->dev,
        "wavefront: conductive_path in non-solid medium at "
        "(%g, %g, %g)\n", SPLIT3(p->rwalk.vtx.P));
      res = RES_BAD_OP;
      goto error;
    }

    /* The M1 enc_query (6-ray batched GPU trace) is the authoritative
     * enclosure source within the wavefront pipeline, mirroring CPU's
     * scene_get_enclosure_id_in_closed_boundaries at conductive entry.
     * The initial rwalk.enc_id was set by the entry-level brute-force
     * scene_get_enclosure_id, which may disagree at degenerate positions
     * (e.g. exact centre of a same-material sphere) because it uses a
     * different tracing algorithm.  When the medium is identical, adopt
     * the enc_query result to keep the pipeline self-consistent. */
    if(enc_id != p->rwalk.enc_id) {
      struct sdis_medium* mdm_init = NULL;
      res_T rr = scene_get_enclosure_medium(
        scn, scene_get_enclosure(scn, p->rwalk.enc_id), &mdm_init);
      if(rr == RES_OK && mdm_init == mdm) {
        p->rwalk.enc_id = enc_id;
      } else {
        log_err(scn->dev,
          "wavefront: conductive_path enclosure mismatch at "
          "(%g, %g, %g)\n", SPLIT3(p->rwalk.vtx.P));
        res = RES_BAD_OP_IRRECOVERABLE;
        goto error;
      }
    }

    p->locals.cnd_ds.enc_id = enc_id;
    p->locals.cnd_ds.medium = mdm;
    d3_set(p->locals.cnd_ds.position_start, p->rwalk.vtx.P);
    solid_get_properties(mdm, &p->rwalk.vtx, &p->locals.cnd_ds.props_ref);
    p->locals.cnd_ds.green_power_term = 0;
    p->locals.cnd_ds.initialized = 1;
    p->locals.cnd_ds.robust_attempt = 0;
  }

  /* --- Each loop iteration: fetch properties, check temperature --- */
  res = solid_get_properties(p->locals.cnd_ds.medium, &p->rwalk.vtx, &props);
  if(res != RES_OK) goto error;
  res = check_solid_constant_properties(
    scn->dev, p->ctx.green_path != NULL, 0, &p->locals.cnd_ds.props_ref, &props);
  if(res != RES_OK) goto error;

  /* Temperature known? */
  if(SDIS_TEMPERATURE_IS_KNOWN(props.temperature)) {
    p->T.value += props.temperature;
    p->T.done = 1;
    if(p->ctx.heat_path) {
      heat_path_get_last_vertex(p->ctx.heat_path)->weight = p->T.value;
    }
    hot->phase = (uint8_t)PATH_DONE;
    hot->active = 0;
    p->done_reason = 2; /* temperature known */
    goto exit;
  }

  /* Store delta_solid parameter (retry counter NOT reset here — it is
   * reset in step_cnd_ds_step_advance after the enclosure check passes.
   * Resetting here would defeat the 100-retry limit because enc-mismatch
   * retries also transition to CHECK_TEMP.) */
  p->locals.cnd_ds.delta_solid_param = (float)props.delta;

  /* Emit 2 rays (forward + backward) */
  setup_delta_sphere_rays(p, hot, scn);
  hot->phase = (uint8_t)PATH_CND_DS_STEP_TRACE;

exit:
  return res;
error:
  hot->phase = (uint8_t)PATH_DONE;
  hot->active = 0;
  p->done_reason = -1;
  goto exit;
}

/* --- PATH_CND_DS_STEP_ENC_VERIFY: set up ENC sub-state at pos_next -------- */
LOCAL_SYM void
step_cnd_ds_step_enc_verify(struct path_state* p, struct path_hot* hot,
                            struct path_enc_data* enc)
{
  ASSERT(p);
  /* enc_query.query_pos already set by step_conductive_ds_process */
  step_enc_query_emit(p, hot, enc, enc->query_pos, PATH_CND_DS_STEP_ADVANCE);
}

/* --- PATH_CND_DS_STEP_ADVANCE: enc check + volumic + time + pos + loop ---- */
LOCAL_SYM res_T
step_cnd_ds_step_advance(struct path_state* p, struct path_hot* hot, struct sdis_scene* scn,
                         struct path_enc_data* enc)
{
  res_T res = RES_OK;
  struct solid_props props = SOLID_PROPS_NULL;
  const float delta = p->locals.cnd_ds.delta;
  const float delta_solid = p->locals.cnd_ds.delta_solid_param;
  double delta_m, mu;

  ASSERT(p && scn);

  /* ------ Enclosure verification (handles both direct-hit and ENC sub) --- */
  /* enc_query.resolved_enc_id is set by:
   *   - step_conductive_ds_process (direct from hit primitive), or
   *   - step_enc_query_resolve (from batched 6-ray query).            */
  if(enc->resolved_enc_id != p->locals.cnd_ds.enc_id) {
    /* Enclosure mismatch — retry with new direction */
    p->locals.cnd_ds.robust_attempt++;
    if(p->locals.cnd_ds.robust_attempt >= 100) {
      log_warn(scn->dev,
        "wavefront: conductive delta_sphere robust exceeded 100 attempts "
        "at (%g, %g, %g)\n", SPLIT3(p->rwalk.vtx.P));
      res = RES_BAD_OP;
      goto error;
    }
    /* Cascade will call step_cnd_ds_check_temp which re-emits rays */
    hot->phase = (uint8_t)PATH_CND_DS_CHECK_TEMP;
    hot->needs_ray = 0;
    goto exit;
  }

  /* ------ Robust check passed — proceed with step ------ */
  p->locals.cnd_ds.robust_attempt = 0;  /* reset only on successful enc match */
  res = solid_get_properties(p->locals.cnd_ds.medium, &p->rwalk.vtx, &props);
  if(res != RES_OK) goto error;

  /* Handle volumic power */
  if(props.power != SDIS_VOLUMIC_POWER_NONE) {
    double power_term = 0;
    /* Inline handle_volumic_power logic for 3D */
    if(S3D_HIT_NONE(&p->locals.cnd_ds.hit0) && S3D_HIT_NONE(&p->locals.cnd_ds.hit1)) {
      double dim = (double)delta * scn->fp_to_meter;
      power_term = dim * dim / (2.0 * 3 * props.lambda);
      p->T.value += props.power * power_term;
    } else {
      const double delta_s_adjusted = delta_solid * RAY_RANGE_MAX_SCALE;
      const double delta_s_in_meter = delta_solid * scn->fp_to_meter;
      float N[3] = {0, 0, 0};
      double cos_U_N, h, h_in_meter;

      if(delta == p->locals.cnd_ds.hit0.distance) {
        f3_normalize(N, p->locals.cnd_ds.hit0.normal);
        cos_U_N = f3_dot(p->locals.cnd_ds.dir0, N);
      } else {
        f3_normalize(N, p->locals.cnd_ds.hit1.normal);
        cos_U_N = f3_dot(p->locals.cnd_ds.dir1, N);
      }
      h = delta * fabs(cos_U_N);
      h_in_meter = h * scn->fp_to_meter;

      power_term = h_in_meter * h_in_meter / (2.0 * props.lambda);

      if(h == delta_s_adjusted) {
        power_term += -(delta_s_in_meter * delta_s_in_meter)
                     / (2.0 * 3 * props.lambda);
      } else if(h < delta_s_adjusted) {
        const double sin_a = h / delta_s_adjusted;
        /* 3D correction */
        const double tmp = (sin_a*sin_a*sin_a - sin_a) / (1 - sin_a);
        power_term += (delta_s_in_meter * delta_s_in_meter)
                    / (6.0 * props.lambda) * tmp;
      }
      p->T.value += props.power * power_term;
    }

    if(p->ctx.green_path && props.power != SDIS_VOLUMIC_POWER_NONE) {
      p->locals.cnd_ds.green_power_term += power_term;
    }
  }

  /* Time rewind */
  delta_m = (double)delta * scn->fp_to_meter;
  mu = (2.0 * 3 * props.lambda) / (props.rho * props.cp * delta_m * delta_m);
  res = time_rewind(scn, mu, props.t0, p->rng, &p->rwalk, &p->ctx, &p->T);
  if(res != RES_OK) goto error;
  if(p->T.done) {
    hot->phase = (uint8_t)PATH_DONE;
    hot->active = 0;
    p->done_reason = 4; /* time rewind */
    goto exit;
  }

  /* Update hit info */
  if(S3D_HIT_NONE(&p->locals.cnd_ds.hit0) || p->locals.cnd_ds.hit0.distance > delta) {
    p->rwalk.hit_3d = S3D_HIT_NULL;
    p->rwalk.hit_side = SDIS_SIDE_NULL__;
  } else {
    p->rwalk.hit_3d = p->locals.cnd_ds.hit0;
    p->rwalk.hit_side = f3_dot(p->locals.cnd_ds.hit0.normal, p->locals.cnd_ds.dir0) < 0
                      ? SDIS_FRONT : SDIS_BACK;
  }

  /* Move position */
  move_pos_3d(p->rwalk.vtx.P, p->locals.cnd_ds.dir0, delta);

  /* Register heat path vertex */
  res = register_heat_vertex(p->ctx.heat_path, &p->rwalk.vtx, p->T.value,
    SDIS_HEAT_VERTEX_CONDUCTION, (int)p->ctx.nbranchings);
  if(res != RES_OK) goto error;

  /* Check loop condition: keep walking while no hit */
  if(S3D_HIT_NONE(&p->rwalk.hit_3d)) {
    /* Still inside solid — continue loop via check_temp */
    hot->phase = (uint8_t)PATH_CND_DS_CHECK_TEMP;
    hot->needs_ray = 0;
  } else {
    /* Hit boundary — register green power term and transition */
    if(p->ctx.green_path && p->locals.cnd_ds.props_ref.power != SDIS_VOLUMIC_POWER_NONE) {
      green_path_add_power_term(
        p->ctx.green_path, p->locals.cnd_ds.medium,
        &p->rwalk.vtx, p->locals.cnd_ds.green_power_term);
    }
    p->T.func = boundary_path_3d;
    p->rwalk.enc_id = ENCLOSURE_ID_NULL;
    p->locals.cnd_ds.initialized = 0; /* reset for next conductive entry */
    hot->phase = (uint8_t)PATH_COUPLED_BOUNDARY;
    hot->needs_ray = 0;
  }

exit:
  return res;
error:
  hot->phase = (uint8_t)PATH_DONE;
  hot->active = 0;
  p->done_reason = -1;
  goto exit;
}

/* --- update_green_path (local copy from wos_Xd.h, non-Xd) --------------- */
static res_T
wf_update_green_path
  (struct green_path_handle* green_path,
   struct rwalk* rwalk,
   struct sdis_medium* mdm,
   const struct solid_props* props,
   const double power_term,
   const struct temperature* T)
{
  res_T res = RES_OK;
  ASSERT(mdm && props && T);
  if(!green_path) goto exit;
  if(props->power != SDIS_VOLUMIC_POWER_NONE) {
    res = green_path_add_power_term(green_path, mdm, &rwalk->vtx, power_term);
    if(res != RES_OK) goto error;
  }
  if(T->done) {
    res = green_path_set_limit_vertex
      (green_path, mdm, &rwalk->vtx, rwalk->elapsed_time);
    if(res != RES_OK) goto error;
  }
exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * B-4 M9: Walk on Spheres (WoS) conductive path state machine
 *
 * Replaces the synchronous conductive_path_wos_3d fallback with a
 * fine-grained state machine.  The main geometry query (closest_point)
 * is batched via s3d_batch_cp_context.  Fallback trace_ray is batched
 * via the normal ray trace pipeline.
 *
 * State flow:
 *   step_conductive (WoS branch)
 *     → PATH_CND_WOS_CHECK_TEMP  (init on first entry)
 *     → PATH_CND_WOS_CLOSEST     (submit closest_point query)
 *     → PATH_CND_WOS_CLOSEST_RESULT (process: e-shell / diffuse / fallback)
 *     → PATH_CND_WOS_FALLBACK_TRACE (if check_diffusion fails)
 *     → PATH_CND_WOS_FALLBACK_RESULT
 *     → PATH_CND_WOS_TIME_TRAVEL (time rewind + volumic power + loop)
 *     → PATH_CND_WOS_CHECK_TEMP  (loop back)
 *   Exit: PATH_DONE (temp known / initial condition) or
 *         PATH_COUPLED_BOUNDARY (hit boundary)
 ******************************************************************************/

/* Epsilon shell threshold from delta (matches CPU wos_Xd.h) */
#define WOS_EPSILON_SHELL(Delta) ((Delta)*1e-2)

/* --- WoS helper: compute_hit_side_3d (inlined from wos_Xd.h) ------------ */
static INLINE enum sdis_side
wf_compute_hit_side_3d
  (const struct s3d_hit* hit,
   const double delta,
   const double pos[3])
{
  struct s3d_attrib v0;
  double p0[3] = {0};
  double N[3] = {0};
  double D = 0, dst = 0;

  ASSERT(hit && delta > 0 && pos && !S3D_HIT_NONE(hit));

  if(hit->distance < 1e-4
  && fabs(hit->distance) < WOS_EPSILON_SHELL(delta)) {
    return SDIS_SIDE_NULL__;
  }

  S3D(triangle_get_vertex_attrib(&hit->prim, 0, S3D_POSITION, &v0));
  d3_set_f3(p0, v0.value);
  d3_set_f3(N, hit->normal);
  d3_normalize(N, N);
  D = -d3_dot(N, p0);
  dst = d3_dot(N, pos) + D;
  return dst > 0 ? SDIS_FRONT : SDIS_BACK;
}

/* --- WoS helper: setup_hit_wos (snap to boundary, inlined from wos_Xd.h) - */
static res_T
wf_setup_hit_wos
  (struct sdis_scene* scn,
   const struct s3d_hit* hit,
   const double delta,
   struct rwalk* rwalk)
{
  struct s3d_primitive prim;
  struct s3d_attrib attr;
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
  enum sdis_side side = SDIS_SIDE_NULL__;
  double tgt[3] = {0};
  res_T res = RES_OK;

  ASSERT(rwalk && hit && !S3D_HIT_NONE(hit));

  S3D(scene_view_get_primitive(scn->s3d_view, hit->prim.prim_id, &prim));
  S3D(primitive_get_attrib(&prim, S3D_POSITION, hit->uv, &attr));

  scene_get_enclosure_ids(scn, hit->prim.prim_id, enc_ids);

  d3_set_f3(tgt, attr.value);
  side = wf_compute_hit_side_3d(hit, delta, rwalk->vtx.P);

  if(side == SDIS_SIDE_NULL__) {
    if(rwalk->enc_id == enc_ids[SDIS_FRONT]) side = SDIS_FRONT;
    if(rwalk->enc_id == enc_ids[SDIS_BACK]) side = SDIS_BACK;
  }

  if(side == SDIS_SIDE_NULL__ || enc_ids[side] != rwalk->enc_id) {
    res = RES_BAD_OP_IRRECOVERABLE;
    log_err(scn->dev,
      "wf_setup_hit_wos: invalid interface at (%g,%g,%g), side=%d\n",
      tgt[0], tgt[1], tgt[2], (int)side);
    goto error;
  }

  d3_set(rwalk->vtx.P, tgt);
  rwalk->hit_3d = *hit;
  rwalk->hit_side = side;

exit:
  return res;
error:
  goto exit;
}

/* --- WoS helper: setup_hit_rt (from ray trace, inlined from wos_Xd.h) ---- */
static res_T
wf_setup_hit_rt
  (struct sdis_scene* scn,
   const double pos[3],
   const double dir[3],
   const struct s3d_hit* hit,
   struct rwalk* rwalk)
{
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
  enum sdis_side side = SDIS_SIDE_NULL__;
  double tgt[3] = {0};
  double N[3] = {0};
  res_T res = RES_OK;

  ASSERT(pos && dir && rwalk && hit && !S3D_HIT_NONE(hit));

  d3_muld(tgt, dir, hit->distance);
  d3_add(tgt, tgt, pos);
  d3_set_f3(N, hit->normal);
  d3_normalize(N, N);
  side = d3_dot(N, dir) > 0 ? SDIS_BACK : SDIS_FRONT;

  scene_get_enclosure_ids(scn, hit->prim.prim_id, enc_ids);
  if(enc_ids[side] != rwalk->enc_id) {
    res = RES_BAD_OP;
    goto error;
  }

  d3_set(rwalk->vtx.P, tgt);
  rwalk->hit_3d = *hit;
  rwalk->hit_side = side;

exit:
  return res;
error:
  goto exit;
}

/* --- WoS helper: check_diffusion_position (inlined from wos_Xd.h) -------- */
static res_T
wf_check_diffusion_position
  (struct sdis_scene* scn,
   const unsigned expected_enc_id,
   const double delta,
   const double pos[3])
{
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
  enum sdis_side side = SDIS_SIDE_NULL__;
  struct s3d_hit hit = S3D_HIT_NULL;
  float wos_pos[3] = {0};
  float wos_radius = 0;
  res_T res = RES_OK;

  ASSERT(scn && pos && expected_enc_id != ENCLOSURE_ID_NULL);

  wos_radius = (float)delta;
  f3_set_d3(wos_pos, pos);

  /* Use single-query closest_point with limited radius (fast, CPU) */
  S3D(scene_view_closest_point(scn->s3d_view, wos_pos, wos_radius, NULL, &hit));
  if(S3D_HIT_NONE(&hit)) goto exit; /* no nearby surface → OK */

  scene_get_enclosure_ids(scn, hit.prim.prim_id, enc_ids);
  side = wf_compute_hit_side_3d(&hit, delta, pos);

  if(side != SDIS_SIDE_NULL__ && enc_ids[side] != expected_enc_id) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(side == SDIS_SIDE_NULL__
  && enc_ids[SDIS_FRONT] != expected_enc_id
  && enc_ids[SDIS_BACK]  != expected_enc_id) {
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

/* --- PATH_CND_WOS_CHECK_TEMP ---------------------------------------------- */
LOCAL_SYM res_T
step_cnd_wos_check_temp(struct path_state* p, struct path_hot* hot, struct sdis_scene* scn)
{
  res_T res = RES_OK;
  ASSERT(p && scn);

  /* First-time initialisation (mirrors conductive_path_wos_3d entry) */
  if(!p->locals.cnd_wos.wos_initialized) {
    const struct enclosure* enc = NULL;
    struct sdis_medium* mdm = NULL;

    /* enc_id should already be set by the caller (step_conductive or
     * step_cnd_ds_step_advance → PATH_COUPLED_CONDUCTIVE re-entry).
     * NB: for WoS we use the rwalk.enc_id which was set during
     * boundary dispatch or scene_get_enclosure_id. */
    if(p->rwalk.enc_id == ENCLOSURE_ID_NULL) {
      log_err(scn->dev,
        "step_cnd_wos_check_temp: enc_id is NULL at (%g,%g,%g)\n",
        SPLIT3(p->rwalk.vtx.P));
      res = RES_BAD_OP;
      goto error;
    }

    enc = scene_get_enclosure(scn, p->rwalk.enc_id);
    res = scene_get_enclosure_medium(scn, enc, &mdm);
    if(res != RES_OK) goto error;

    if(sdis_medium_get_type(mdm) != SDIS_SOLID) {
      log_err(scn->dev,
        "step_cnd_wos_check_temp: non-solid medium at (%g,%g,%g)\n",
        SPLIT3(p->rwalk.vtx.P));
      res = RES_BAD_OP;
      goto error;
    }

    p->locals.cnd_wos.enc_id = p->rwalk.enc_id;
    p->locals.cnd_wos.medium = mdm;
    solid_get_properties(mdm, &p->rwalk.vtx, &p->locals.cnd_wos.props_ref);
    p->locals.cnd_wos.props = p->locals.cnd_wos.props_ref;
    p->locals.cnd_wos.alpha =
      p->locals.cnd_wos.props_ref.lambda
      / (p->locals.cnd_wos.props_ref.rho * p->locals.cnd_wos.props_ref.cp);
    p->locals.cnd_wos.green_power_term = 0;
    d3_set(p->locals.cnd_wos.position_start, p->rwalk.vtx.P);

    /* Compute scene AABB diagonal for bounded CP search radius */
    {
      double lower[3], upper[3], diag[3];
      if(sdis_scene_get_aabb(scn, lower, upper) == RES_OK) {
        d3_sub(diag, upper, lower);
        p->locals.cnd_wos.scene_diag = (float)d3_len(diag);
      } else {
        p->locals.cnd_wos.scene_diag = (float)HUGE_VAL;
      }
    }

    p->locals.cnd_wos.wos_initialized = 1;
  } else {
    /* Re-entry: fetch properties at new position */
    res = solid_get_properties(p->locals.cnd_wos.medium,
                               &p->rwalk.vtx,
                               &p->locals.cnd_wos.props);
    if(res != RES_OK) goto error;
    res = check_solid_constant_properties(
      scn->dev, p->ctx.green_path != NULL, 1,
      &p->locals.cnd_wos.props_ref, &p->locals.cnd_wos.props);
    if(res != RES_OK) goto error;
  }

  /* Check temperature known */
  if(SDIS_TEMPERATURE_IS_KNOWN(p->locals.cnd_wos.props.temperature)) {
    /* Register heat path vertex */
    res = register_heat_vertex(p->ctx.heat_path, &p->rwalk.vtx, p->T.value,
      SDIS_HEAT_VERTEX_CONDUCTION, (int)p->ctx.nbranchings);
    if(res != RES_OK) goto error;

    p->T.value += p->locals.cnd_wos.props.temperature;
    p->T.done = 1;

    /* Save green function data */
    if(p->ctx.green_path) {
      wf_update_green_path(p->ctx.green_path, &p->rwalk,
        p->locals.cnd_wos.medium, &p->locals.cnd_wos.props_ref,
        p->locals.cnd_wos.green_power_term, &p->T);
    }

    hot->phase = (uint8_t)PATH_DONE;
    hot->active = 0;
    p->done_reason = 2; /* temperature known */
    goto exit;
  }

  /* Store delta for this step */
  p->locals.cnd_wos.delta = p->locals.cnd_wos.props.delta;

  /* Save position before diffusion step (needed by time_travel) */
  d3_set(p->locals.cnd_wos.position_start, p->rwalk.vtx.P);

  /* Transition to closest_point query */
  step_cnd_wos_closest(p, hot);

exit:
  return res;
error:
  hot->phase = (uint8_t)PATH_DONE;
  hot->active = 0;
  p->done_reason = -1;
  goto exit;
}

/* --- PATH_CND_WOS_CLOSEST: submit closest_point batch query --------------- */
LOCAL_SYM void
step_cnd_wos_closest(struct path_state* p, struct path_hot* hot)
{
  ASSERT(p);

  /* Query position = current rwalk position, radius = INF */
  f3_set_d3(p->locals.cnd_wos.new_pos, p->rwalk.vtx.P);
  p->locals.cnd_wos.query_pos[0] = p->rwalk.vtx.P[0];
  p->locals.cnd_wos.query_pos[1] = p->rwalk.vtx.P[1];
  p->locals.cnd_wos.query_pos[2] = p->rwalk.vtx.P[2];
  p->locals.cnd_wos.query_radius = p->locals.cnd_wos.scene_diag;
  p->locals.cnd_wos.batch_cp_idx = (uint32_t)-1;

  hot->needs_ray = 0;  /* NOT a ray request — closest_point has its own batch */
  hot->phase = (uint8_t)PATH_CND_WOS_CLOSEST;
}

/* --- PATH_CND_WOS_CLOSEST_RESULT: process closest_point result ------------ */
LOCAL_SYM res_T
step_cnd_wos_closest_result(struct path_state* p, struct path_hot* hot, struct sdis_scene* scn)
{
  const struct s3d_hit* hit = &p->locals.cnd_wos.cached_hit;
  double wos_distance = 0;
  double wos_epsilon = 0;
  double dir_d[3] = {0};
  res_T res = RES_OK;

  ASSERT(p && scn);

  if(S3D_HIT_NONE(hit)) {
    log_err(scn->dev,
      "step_cnd_wos_closest_result: closest_point returned NONE at "
      "(%g,%g,%g)\n", SPLIT3(p->rwalk.vtx.P));
    res = RES_BAD_OP;
    goto error;
  }

  wos_distance = hit->distance;
  p->locals.cnd_wos.last_distance = wos_distance;
  wos_epsilon = WOS_EPSILON_SHELL(p->locals.cnd_wos.delta);

  /* Case 1: In epsilon-shell — snap to boundary */
  if(wos_distance <= wos_epsilon) {
    res = wf_setup_hit_wos(scn, hit, p->locals.cnd_wos.delta, &p->rwalk);
    if(res != RES_OK) goto error;
    p->locals.cnd_wos.last_distance = hit->distance;
    hot->phase = (uint8_t)PATH_CND_WOS_TIME_TRAVEL;
    goto exit;
  }

  /* Case 2: Outside epsilon-shell — uniform sphere sampling.
   * Compute candidate new position, then submit a batch closest_point
   * query with limited radius (delta) to validate enclosure membership.
   * This replaces the former synchronous wf_check_diffusion_position(). */
  {
    double pos_new[3] = {0};
    float dir_f[3] = {0};

    ssp_ran_sphere_uniform_float(p->rng, dir_f, NULL);
    /* Save direction for potential fallback trace */
    f3_set(p->locals.cnd_wos.dir, dir_f);

    d3_set_f3(dir_d, dir_f);
    d3_muld(pos_new, dir_d, (double)hit->distance);
    d3_add(pos_new, pos_new, p->rwalk.vtx.P);

    /* Save candidate position and submit batch CP query for validation */
    d3_set(p->locals.cnd_wos.diffusion_pos, pos_new);
    step_cnd_wos_diffusion_check(p, hot);
  }

exit:
  /* O11_SAFETY: one-shot — poison consumed cached_hit to detect double-read */
#ifdef SDIS_DEBUG_CHECKS
  p->locals.cnd_wos.cached_hit.distance = -1e38f;
#endif
  return res;
error:
  hot->phase = (uint8_t)PATH_DONE;
  hot->active = 0;
  p->done_reason = -1;
  goto exit;
}

/* --- PATH_CND_WOS_DIFFUSION_CHECK: submit diffusion validation CP query --- */
LOCAL_SYM void
step_cnd_wos_diffusion_check(struct path_state* p, struct path_hot* hot)
{
  ASSERT(p);

  /* Query position = candidate diffusion position, radius = delta */
  p->locals.cnd_wos.query_pos[0] = p->locals.cnd_wos.diffusion_pos[0];
  p->locals.cnd_wos.query_pos[1] = p->locals.cnd_wos.diffusion_pos[1];
  p->locals.cnd_wos.query_pos[2] = p->locals.cnd_wos.diffusion_pos[2];
  p->locals.cnd_wos.query_radius = (float)p->locals.cnd_wos.delta;
  p->locals.cnd_wos.batch_cp2_idx = (uint32_t)-1;

  hot->needs_ray = 0;  /* closest_point has its own batch */
  hot->phase = (uint8_t)PATH_CND_WOS_DIFFUSION_CHECK;
}

/* --- PATH_CND_WOS_DIFFUSION_CHECK_RESULT: process validation CP result ---- */
LOCAL_SYM res_T
step_cnd_wos_diffusion_check_result(struct path_state* p, struct path_hot* hot, struct sdis_scene* scn)
{
  const struct s3d_hit* hit = &p->locals.cnd_wos.cached_hit;
  const unsigned expected_enc_id = p->rwalk.enc_id;
  const double delta = p->locals.cnd_wos.delta;
  const double* pos = p->locals.cnd_wos.diffusion_pos;
  res_T res = RES_OK;
  int position_valid = 1;

  ASSERT(p && scn);

  /* Replicate wf_check_diffusion_position logic using batch CP result */
  if(!S3D_HIT_NONE(hit)) {
    unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
    enum sdis_side side = SDIS_SIDE_NULL__;

    scene_get_enclosure_ids(scn, hit->prim.prim_id, enc_ids);
    side = wf_compute_hit_side_3d(hit, delta, pos);

    if(side != SDIS_SIDE_NULL__ && enc_ids[side] != expected_enc_id) {
      position_valid = 0;
    }
    if(side == SDIS_SIDE_NULL__
    && enc_ids[SDIS_FRONT] != expected_enc_id
    && enc_ids[SDIS_BACK]  != expected_enc_id) {
      position_valid = 0;
    }
  }
  /* S3D_HIT_NONE → no nearby surface → position OK */

  if(position_valid) {
    /* Position valid — move directly */
    d3_set(p->rwalk.vtx.P, pos);
    p->rwalk.hit_3d = S3D_HIT_NULL;
    /* last_distance already set by step_cnd_wos_closest_result */
    hot->phase = (uint8_t)PATH_CND_WOS_TIME_TRAVEL;
  } else {
    /* Position invalid — need fallback trace_ray */
    step_cnd_wos_fallback_trace(p, hot);
  }

  /* O11_SAFETY: one-shot — poison consumed cached_hit to detect double-read */
#ifdef SDIS_DEBUG_CHECKS
  p->locals.cnd_wos.cached_hit.distance = -1e38f;
#endif

  return res;
}

/* --- PATH_CND_WOS_FALLBACK_TRACE: emit fallback ray ---------------------- */
LOCAL_SYM void
step_cnd_wos_fallback_trace(struct path_state* p, struct path_hot* hot)
{
  ASSERT(p);

  /* Emit trace_ray from current position along the previously sampled
   * direction (stored in locals.cnd_wos.dir by closest_result) */
  f3_set_d3(p->ray_req.origin, p->rwalk.vtx.P);
  f3_set(p->ray_req.direction, p->locals.cnd_wos.dir);
  p->ray_req.range[0] = 0;
  p->ray_req.range[1] = (float)HUGE_VAL;
  p->ray_req.ray_count = 1;

  hot->ray_bucket = (uint8_t)RAY_BUCKET_RADIATIVE;
  hot->ray_count_ext = (uint8_t)0;
  hot->needs_ray = 1;
  hot->phase = (uint8_t)PATH_CND_WOS_FALLBACK_TRACE;
}

/* --- PATH_CND_WOS_FALLBACK_RESULT: process fallback ray result ------------ */
LOCAL_SYM res_T
step_cnd_wos_fallback_result(struct path_state* p, struct path_hot* hot, struct sdis_scene* scn,
                             const struct s3d_hit* hit_rt)
{
  res_T res = RES_OK;
  double dir_d[3] = {0};

  ASSERT(p && scn && hit_rt);

  d3_set_f3(dir_d, p->locals.cnd_wos.dir);

  if(S3D_HIT_NONE(hit_rt)) {
    /* Miss — snap to cached closest_point hit */
    res = wf_setup_hit_wos(scn, &p->locals.cnd_wos.cached_hit,
                           p->locals.cnd_wos.delta, &p->rwalk);
    if(res != RES_OK) goto error;
  } else {
    /* Hit — try setup_hit_rt */
    res = wf_setup_hit_rt(scn, p->rwalk.vtx.P, dir_d, hit_rt, &p->rwalk);
    if(res != RES_OK) {
      /* Enclosure mismatch — fallback to snap */
      res = wf_setup_hit_wos(scn, &p->locals.cnd_wos.cached_hit,
                             p->locals.cnd_wos.delta, &p->rwalk);
      if(res != RES_OK) goto error;
    }
  }

  hot->phase = (uint8_t)PATH_CND_WOS_TIME_TRAVEL;

exit:
  return res;
error:
  hot->phase = (uint8_t)PATH_DONE;
  hot->active = 0;
  p->done_reason = -1;
  goto exit;
}

/* --- PATH_CND_WOS_TIME_TRAVEL: time rewind + volumic power + loop --------- */
LOCAL_SYM res_T
step_cnd_wos_time_travel(struct path_state* p, struct path_hot* hot, struct sdis_scene* scn)
{
  res_T res = RES_OK;
  double dst = p->locals.cnd_wos.last_distance; /* [m/fp_to_meter] */
  double dst_m = 0; /* [m] */
  const double alpha = p->locals.cnd_wos.alpha;
  const double t0 = p->locals.cnd_wos.props.t0;
  const double* pos = p->locals.cnd_wos.position_start;
  struct sdis_medium* mdm = p->locals.cnd_wos.medium;
  double power_term = 0;
  const struct solid_props* props = &p->locals.cnd_wos.props;

  ASSERT(p && scn);

  /* ---- time_travel (inlined from wos_Xd.h) ---- */
  dst_m = dst * scn->fp_to_meter;
  if(dst_m > 0 && alpha > 0) {
    double r, x, tau, time_step;

    r = ssp_rng_canonical(p->rng);
    x = swf_tabulation_inverse(scn->dev->H_3d, SWF_QUADRATIC, r);

    tau = x / alpha * dst_m * dst_m;
    time_step = MMIN(tau, p->rwalk.vtx.time - t0);

    p->rwalk.elapsed_time += time_step;

    if(!IS_INF(p->rwalk.vtx.time)) {
      p->rwalk.vtx.time = MMAX(t0, p->rwalk.vtx.time - tau);

      /* Check if initial condition reached */
      if(p->rwalk.vtx.time <= t0) {
        double new_dst, new_dst_fp;
        double dir_d[3] = {0};
        float dir_f[3] = {0};
        double temperature = 0;

        r = ssp_rng_canonical(p->rng);
        x = swf_tabulation_inverse(scn->dev->H_3d, SWF_QUADRATIC, r);
        new_dst = sqrt(alpha * time_step / x);
        new_dst_fp = new_dst / scn->fp_to_meter;

        ssp_ran_sphere_uniform_float(p->rng, dir_f, NULL);
        d3_set_f3(dir_d, dir_f);
        d3_muld(dir_d, dir_d, new_dst_fp);
        d3_add(p->rwalk.vtx.P, pos, dir_d);

        dst = new_dst_fp;
        p->locals.cnd_wos.last_distance = dst;

        temperature = medium_get_temperature(mdm, &p->rwalk.vtx);
        if(SDIS_TEMPERATURE_IS_UNKNOWN(temperature)) {
          log_err(scn->dev,
            "step_cnd_wos_time_travel: initial condition reached but "
            "temperature unknown at (%g,%g,%g)\n",
            SPLIT3(p->rwalk.vtx.P));
          res = RES_BAD_ARG;
          goto error;
        }

        p->T.value += temperature;
        p->T.done = 1;
      }
    }
  }

  /* ---- handle_volumic_power_wos (inlined) ---- */
  if(props->power != SDIS_VOLUMIC_POWER_NONE && dst > 0) {
    double dst_pw = dst * scn->fp_to_meter;
    power_term = dst_pw * dst_pw / (2.0 * 3 * props->lambda);
    p->T.value += props->power * power_term;
    if(p->ctx.green_path) {
      p->locals.cnd_wos.green_power_term += power_term;
    }
  }

  /* Register heat path vertex */
  res = register_heat_vertex(p->ctx.heat_path, &p->rwalk.vtx, p->T.value,
    SDIS_HEAT_VERTEX_CONDUCTION, (int)p->ctx.nbranchings);
  if(res != RES_OK) goto error;

  /* ---- Loop exit conditions ---- */
  if(p->T.done) {
    /* Initial condition reached */
    p->T.func = NULL;

    /* Save green function data */
    if(p->ctx.green_path) {
      wf_update_green_path(p->ctx.green_path, &p->rwalk, mdm,
        &p->locals.cnd_wos.props_ref,
        p->locals.cnd_wos.green_power_term, &p->T);
    }

    p->locals.cnd_wos.wos_initialized = 0; /* reset for next entry */
    hot->phase = (uint8_t)PATH_DONE;
    hot->active = 0;
    p->done_reason = 4; /* time rewind / initial condition */
    goto exit;
  }

  if(!S3D_HIT_NONE(&p->rwalk.hit_3d)) {
    /* Hit boundary */
    p->T.func = boundary_path_3d;
    p->rwalk.enc_id = ENCLOSURE_ID_NULL;

    /* Save green function data */
    if(p->ctx.green_path) {
      wf_update_green_path(p->ctx.green_path, &p->rwalk, mdm,
        &p->locals.cnd_wos.props_ref,
        p->locals.cnd_wos.green_power_term, &p->T);
    }

    p->locals.cnd_wos.wos_initialized = 0;
    hot->phase = (uint8_t)PATH_COUPLED_BOUNDARY;
    goto exit;
  }

  /* Continue WoS loop */
  hot->phase = (uint8_t)PATH_CND_WOS_CHECK_TEMP;

exit:
  return res;
error:
  hot->phase = (uint8_t)PATH_DONE;
  hot->active = 0;
  p->done_reason = -1;
  goto exit;
}

