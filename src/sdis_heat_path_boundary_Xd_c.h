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

struct XD(find_reinjection_ray_args) {
  const struct rwalk* rwalk; /* Current random walk state */
  float dir0[DIM]; /* Challenged ray direction */
  float dir1[DIM]; /* Challenged ray direction */
  double distance; /* Maximum reinjection distance */
  unsigned solid_enc_id; /* Enclosure id into which the reinjection occurs */

  /* Define if the random walk position can be moved or not to find a valid
   * reinjection direction */
  int can_move;
};
static const struct XD(find_reinjection_ray_args)
XD(FIND_REINJECTION_RAY_ARGS_NULL) = { NULL, {0}, {0}, 0, ENCLOSURE_ID_NULL, 0 };

struct XD(reinjection_ray) {
  double org[DIM]; /* Origin of the reinjection */
  float dir[DIM]; /* Direction of the reinjection */
  float dst; /* Reinjection distance along dir */
  struct sXd(hit) hit; /* Hit along the reinjection dir */

  /* Define whether or not the random walk was moved to find this reinjection
   * ray */
  int position_was_moved;
};
static const struct XD(reinjection_ray)
XD(REINJECTION_RAY_NULL) = { {0}, {0}, 0, SXD_HIT_NULL__, 0 };

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE res_T
XD(check_find_reinjection_ray_args)
  (struct sdis_scene* scn,
   const struct XD(find_reinjection_ray_args)* args)
{
  const struct enclosure* enc = NULL;
  struct sdis_medium* mdm = NULL;
  res_T res = RES_OK;
  ASSERT(scn);

  /* Check pointers */
  if(!args || !args->rwalk) return RES_BAD_ARG;

  /* Check distance */
  if(args->distance <= 0) return RES_BAD_ARG;

  /* Check directions */
  if(!fX(is_normalized)(args->dir0) || !fX(is_normalized)(args->dir1)) {
    return RES_BAD_ARG;
  }

  /* Check enclosure id  */
  if(args->solid_enc_id == ENCLOSURE_ID_NULL) {
    return RES_BAD_ARG;
  }
  enc = scene_get_enclosure(scn, args->solid_enc_id);

  /* Check the enclosure */
  enc = scene_get_enclosure(scn, args->solid_enc_id);
  if(enc->medium_id != MEDIUM_ID_MULTI) {
    if((res = scene_get_enclosure_medium(scn, enc, &mdm)) != RES_OK) return res;
    if(sdis_medium_get_type(mdm) != SDIS_SOLID) {
      res = RES_BAD_ARG;
    }
  }

  return RES_OK;
}

static INLINE res_T
XD(check_sample_reinjection_step_args)
  (struct sdis_scene* scn,
   const struct sample_reinjection_step_args* args)
{
  const struct enclosure* enc = NULL;
  struct sdis_medium* mdm = NULL;
  res_T res = RES_OK;
  ASSERT(scn);

  /* Check pointers */
  if(!args || !args->rng || !args->rwalk) return RES_BAD_ARG;

  /* Check distance */
  if(args->distance <= 0) return RES_BAD_ARG;

  /* Check side */
  if((unsigned)args->side >= SDIS_SIDE_NULL__) return RES_BAD_ARG;

  /* Check enclosure id  */
  if(args->solid_enc_id == ENCLOSURE_ID_NULL) {
    return RES_BAD_ARG;
  }

  /* Check the enclosure */
  enc = scene_get_enclosure(scn, args->solid_enc_id);
  if(enc->medium_id != MEDIUM_ID_MULTI) {
    if((res = scene_get_enclosure_medium(scn, enc, &mdm)) != RES_OK) return res;
    if(sdis_medium_get_type(mdm) != SDIS_SOLID) {
      return RES_BAD_ARG;
    }
  }

  return RES_OK;
}

static INLINE res_T
XD(check_reinjection_step)(const struct reinjection_step* step)
{
  /* Check pointer */
  if(!step) return RES_BAD_ARG;

  /* Check direction */
  if(!fX(is_normalized)(step->direction)) return RES_BAD_ARG;

  /* Check distance */
  if(step->distance <= 0) return RES_BAD_ARG;

  return RES_OK;
}

static INLINE res_T
XD(check_solid_reinjection_args)(const struct solid_reinjection_args* args)
{
  /* Check pointers */
  if(!args || !args->rng || !args->rwalk || !args->rwalk_ctx || !args->T)
    return RES_BAD_ARG;

  /* Check unit */
  if(args->fp_to_meter <= 0) return RES_BAD_ARG;

  return XD(check_reinjection_step)(args->reinjection);
}

/* Check that the interface fragment is consistent with the current state of
 * the random walk */
static INLINE res_T
XD(check_rwalk_fragment_consistency)
  (const struct rwalk* rwalk,
   const struct sdis_interface_fragment* frag)
{
  double N[DIM];
  double uv[2] = {0, 0};
  ASSERT(rwalk && frag);

  /* Check intersection */
  if(SXD_HIT_NONE(&rwalk->XD(hit))) return RES_BAD_ARG;

  /* Check positions */
  if(!dX(eq_eps)(rwalk->vtx.P, frag->P, 1.e-6)) return RES_BAD_ARG;

  /* Check normals */
  dX(normalize)(N, dX_set_fX(N, rwalk->XD(hit).normal));
  if(!dX(eq_eps)(N, frag->Ng, 1.e-6)) return RES_BAD_ARG;

  /* Check time */
  if(!eq_eps(rwalk->vtx.time, frag->time,  1.e-6)
  && !(IS_INF(rwalk->vtx.time) && IS_INF(frag->time))) {
    return RES_BAD_ARG;
  }

  /* Check parametric coordinates */
#if (SDIS_XD_DIMENSION == 2)
  uv[0] = rwalk->XD(hit).u;
#else
  d2_set_f2(uv, rwalk->XD(hit).uv);
#endif
  if(!d2_eq_eps(uv, frag->uv, 1.e-6)) return RES_BAD_ARG;

  return RES_OK;
}

static void
XD(sample_reinjection_dir)
  (const struct rwalk* rwalk,
   struct ssp_rng* rng,
   float dir[DIM])
{
#if DIM == 2
  /* The sampled directions is defined by rotating the normal around the Z axis
   * of an angle of PI/4 or -PI/4. Let the rotation matrix defined as
   *    | cos(a) -sin(a) |
   *    | sin(a)  cos(a) |
   * with a = PI/4, dir = sqrt(2)/2 * | 1 -1 | . N
   *                                  | 1  1 |
   * with a =-PI/4, dir = sqrt(2)/2 * | 1  1 | . N
   *                                  |-1  1 |
   * Note that since the sampled direction is finally normalized, we can
   * discard the sqrt(2)/2 constant. */
  const uint64_t r = ssp_rng_uniform_uint64(rng, 0, 1);
  ASSERT(rwalk && rng && dir);
  ASSERT(!SXD_HIT_NONE(&rwalk->XD(hit)));
  ASSERT(rwalk->enc_id == ENCLOSURE_ID_NULL);

  if(r) {
    dir[0] = rwalk->XD(hit).normal[0] - rwalk->XD(hit).normal[1];
    dir[1] = rwalk->XD(hit).normal[0] + rwalk->XD(hit).normal[1];
  } else {
    dir[0] = rwalk->XD(hit).normal[0] + rwalk->XD(hit).normal[1];
    dir[1] =-rwalk->XD(hit).normal[0] + rwalk->XD(hit).normal[1];
  }
  f2_normalize(dir, dir);
#else
  /* Sample a random direction around the normal whose cosine is 1/sqrt(3). To
   * do so we sample a position onto a cone whose height is 1/sqrt(2) and the
   * radius of its base is 1. */
  float frame[9];
  ASSERT(rwalk && rng && dir);
  ASSERT(!SXD_HIT_NONE(&rwalk->XD(hit)));
  ASSERT(rwalk->enc_id == ENCLOSURE_ID_NULL);
  ASSERT(fX(is_normalized)(rwalk->XD(hit).normal));

  ssp_ran_circle_uniform_float(rng, dir, NULL);
  dir[2]  = (float)(1.0/sqrt(2));

  f33_basis(frame, rwalk->XD(hit).normal);
  f33_mulf3(dir, frame, dir);
  f3_normalize(dir, dir);
  ASSERT(eq_epsf(f3_dot(dir, rwalk->XD(hit).normal), (float)(1.0/sqrt(3)), 1.e-4f));
#endif
}

static res_T
XD(find_reinjection_ray)
  (struct sdis_scene* scn,
   const struct XD(find_reinjection_ray_args)* args,
   struct XD(reinjection_ray)* ray)
{
  /* Emperical scale factor applied to the challenged reinjection distance. If
   * the distance to reinject is less than this adjusted value, the solver will
   * try to discard the reinjection distance if possible in order to avoid
   * numerical issues. */
  const float REINJECT_DST_MIN_SCALE = 0.125f;

  /* # attempts to find a ray direction */
  int MAX_ATTEMPTS = 1;

  /* Enclosures */
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
  unsigned enc0_id = ENCLOSURE_ID_NULL;
  unsigned enc1_id = ENCLOSURE_ID_NULL;

  struct hit_filter_data filter_data = HIT_FILTER_DATA_NULL;
  struct sXd(hit) hit;
  struct sXd(hit) hit0;
  struct sXd(hit) hit1;
  double tmp[DIM];
  double dst;
  double dst0;
  double dst1;
  const float* dir;
  float reinject_threshold;
  double dst_adjusted;
  float org[DIM];
  const float range[2] = {0, FLT_MAX};
  enum sdis_side side;
  int iattempt = 0;
  res_T res = RES_OK;

  ASSERT(scn && args && ray);
  ASSERT(XD(check_find_reinjection_ray_args)(scn, args) == RES_OK);

  *ray = XD(REINJECTION_RAY_NULL);
  MAX_ATTEMPTS = args->can_move ? 20 : 1;

  dst_adjusted = args->distance * RAY_RANGE_MAX_SCALE;
  reinject_threshold = (float)args->distance * REINJECT_DST_MIN_SCALE;

  dX(set)(ray->org, args->rwalk->vtx.P);

  do {
    fX_set_dX(org, ray->org);
    filter_data.XD(hit) = args->rwalk->XD(hit);

    /* Limit the epsilon to 1.e-6, as Star-3D's single-precision floating-point
     * representation will inevitably present numerical accuracy problems below
     * this threshold. There's no point in going any lower */
    /*filter_data.epsilon = MMAX(args->distance * 0.01, 1e-6);*/
    filter_data.epsilon = args->distance * 0.01;

    SXD(scene_view_trace_ray
      (scn->sXd(view), org, args->dir0, range, &filter_data, &hit0));
    SXD(scene_view_trace_ray
      (scn->sXd(view), org, args->dir1, range, &filter_data, &hit1));

    /* Retrieve the enclosure at the reinjection pos along dir0 */
    if(!SXD_HIT_NONE(&hit0)) {
      scene_get_enclosure_ids(scn, hit0.prim.prim_id, enc_ids);
      side = fX(dot)(args->dir0, hit0.normal) < 0 ? SDIS_FRONT : SDIS_BACK;
      enc0_id = enc_ids[side];
    } else {
      XD(move_pos)(dX(set)(tmp, ray->org), args->dir0, (float)args->distance);
      res = scene_get_enclosure_id_in_closed_boundaries(scn, tmp, &enc0_id);
      if(res == RES_BAD_OP) { enc0_id = ENCLOSURE_ID_NULL; res = RES_OK; }
      if(res != RES_OK) goto error;
    }

    /* Retrieve the enclosure at the reinjection pos along dir1 */
    if(!SXD_HIT_NONE(&hit1)) {
      scene_get_enclosure_ids(scn, hit1.prim.prim_id, enc_ids);
      side = fX(dot)(args->dir1, hit1.normal) < 0 ? SDIS_FRONT : SDIS_BACK;
      enc1_id = enc_ids[side];
    } else {
      XD(move_pos)(dX(set)(tmp, ray->org), args->dir1, (float)args->distance);
      res = scene_get_enclosure_id_in_closed_boundaries(scn, tmp, &enc1_id);
      if(res == RES_BAD_OP) { enc1_id = ENCLOSURE_ID_NULL; res = RES_OK; }
      if(res != RES_OK) goto error;
    }

    dst0 = dst1 = -1;
    if(enc0_id == args->solid_enc_id) { /* Check reinjection consistency */
      if(hit0.distance <= dst_adjusted) {
        dst0 = hit0.distance;
      } else {
        dst0 = args->distance;
        hit0 = SXD_HIT_NULL;
      }
    }
    if(enc1_id == args->solid_enc_id) { /* Check reinjection consistency */
      if(hit1.distance <= dst_adjusted) {
        dst1 = hit1.distance;
      } else {
        dst1 = args->distance;
        hit1 = SXD_HIT_NULL;
      }
    }

    /* No valid reinjection. Maybe the random walk is near a sharp corner and
     * thus the ray-tracing misses the enclosure geometry. Another possibility
     * is that the random walk lies roughly on an edge. In this case, sampled
     * reinjection dirs can intersect the primitive on the other side of the
     * edge. Normally, this primitive should be filtered by the "hit_filter"
     * function but this may be not the case due to a "threshold effect". In
     * both situations, try to slightly move away from the primitive boundaries
     * and retry to find a valid reinjection. */
    if(dst0 == -1 && dst1 == -1
    && iattempt < MAX_ATTEMPTS - 1) { /* Is there still a trial to be done? */
      XD(move_away_primitive_boundaries)
        (&args->rwalk->XD(hit), args->distance, ray->org);
      ray->position_was_moved = 1;
    }
  } while(dst0 == -1 && dst1 == -1 && ++iattempt < MAX_ATTEMPTS);

  if(dst0 == -1 && dst1 == -1) { /* No valid reinjection */
    log_err(scn->dev, "%s: no valid reinjection direction at {"FORMAT_VECX"}.\n",
      FUNC_NAME, SPLITX(ray->org));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  if(dst0 == -1) {
    /* Invalid dir0 -> move along dir1 */
    dir = args->dir1;
    dst = dst1;
    hit = hit1;
  } else if(dst1 == -1) {
    /* Invalid dir1 -> move along dir0 */
    dir = args->dir0;
    dst = dst0;
    hit = hit0;
  } else if(dst0 < reinject_threshold && dst1 < reinject_threshold) {
    /* The displacement along dir0 and dir1 are both below the reinjection
     * threshold that defines a distance under which the temperature gradients
     * are ignored. Move along the direction that allows the maximum
     * displacement. */
    if(dst0 > dst1) {
      dir = args->dir0;
      dst = dst0;
      hit = hit0;
    } else {
      dir = args->dir1;
      dst = dst1;
      hit = hit1;
    }
  } else if(dst0 < reinject_threshold) {
    /* Ingore dir0 that is bellow the reinject threshold */
    dir = args->dir1;
    dst = dst1;
    hit = hit1;
  } else if(dst1 < reinject_threshold) {
    /* Ingore dir1 that is bellow the reinject threshold */
    dir = args->dir0;
    dst = dst0;
    hit = hit0;
  } else {
    /* All reinjection directions are valid. Choose the first 1 that was
     * randomly selected by the sample_reinjection_dir procedure and adjust
     * the displacement distance. */
    dir = args->dir0;

    /* Define the reinjection distance along dir0 and its corresponding hit  */
    if(dst0 <= dst1) {
      dst = dst0;
      hit = hit0;
    } else {
      dst = dst1;
      hit = SXD_HIT_NULL;
    }

    /* If the displacement distance is too close of a boundary, move to the
     * boundary in order to avoid numerical uncertainty. */
    if(!SXD_HIT_NONE(&hit0)
    && dst0 != dst
    && eq_eps(dst0, dst, dst0*0.1)) {
      dst = dst0;
      hit = hit0;
    }
  }

  /* Setup the ray */
  fX(set)(ray->dir, dir);
  ray->dst = (float)dst;
  ray->hit = hit;

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(find_reinjection_ray_and_check_validity)
  (struct sdis_scene* scn,
   const struct XD(find_reinjection_ray_args)* args,
   struct XD(reinjection_ray)* ray)
{
  double pos[DIM];
  res_T res = RES_OK;

  ASSERT(scn && args && ray);
  ASSERT(XD(check_find_reinjection_ray_args)(scn, args) == RES_OK);

  /* Select a reinjection direction */
  res = XD(find_reinjection_ray)(scn, args, ray);
  if(res != RES_OK) goto error;

  if(SXD_HIT_NONE(&ray->hit)) {
    unsigned enc_id = ENCLOSURE_ID_NULL;

    /* Obtain the enclosure in which the reinjection position lies */
    XD(move_pos)(dX(set)(pos, ray->org), ray->dir, (float)ray->dst);
    res = scene_get_enclosure_id_in_closed_boundaries(scn, pos, &enc_id);
    if(res != RES_OK) goto error;

    /* Check enclosure consistency at the reinjection position */
    if(enc_id != args->solid_enc_id) {
      res = RES_BAD_OP;
      goto error;
    }
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(handle_volumic_power)
  (struct sdis_medium* solid,
   struct rwalk_context* rwalk_ctx,
   struct rwalk* rwalk,
   const double reinject_dst_m,
   struct temperature* T)
{
  double power;
  double lambda;
  double power_term;
  size_t picard_order;
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(solid && rwalk_ctx && rwalk && T && reinject_dst_m > 0);

  /* Fetch the volumic power */
  power = solid_get_volumic_power(solid, &rwalk->vtx);
  if(power == SDIS_VOLUMIC_POWER_NONE) goto exit; /* Do nothing */

  /* Currently, the power term can be correctly taken into account only when
   * the radiative temperature is linearized, i.e. when the picard order is
   * equal to 1 */
  picard_order = get_picard_order(rwalk_ctx);
  if(picard_order > 1) {
    log_err(solid->dev,
     "%s: invalid not null volumic power '%g' kg/m^3. Could not manage a "
     "volumic power when the picard order is not equal to 1; Picard order is "
     "currently set to %lu.\n",
     FUNC_NAME, power, (unsigned long)picard_order);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Fetch the conductivity */
  lambda = solid_get_thermal_conductivity(solid, &rwalk->vtx);

  /* Compute the power term and handle the volumic power */
  power_term = (reinject_dst_m * reinject_dst_m)/ (2.0 * DIM * lambda);
  T->value += power * power_term;

  /* Update the green path with the power term */
  if(rwalk_ctx->green_path) {
    res = green_path_add_power_term
      (rwalk_ctx->green_path, solid, &rwalk->vtx, power_term);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(sample_reinjection_step_solid_fluid)
  (struct sdis_scene* scn,
   const struct sample_reinjection_step_args* args,
   struct reinjection_step* step)
{
  /* Input/output data of the function finding a valid reinjection ray */
  struct XD(find_reinjection_ray_args) find_reinject_ray_args =
    XD(FIND_REINJECTION_RAY_ARGS_NULL);
  struct XD(reinjection_ray) ray = XD(REINJECTION_RAY_NULL);

  /* Enclosures */
  const struct enclosure* solid_enc = NULL;
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};

  /* In 2D it is useless to try to resample a reinjection direction since there
   * is only one possible direction */
  const int MAX_ATTEMPTS = DIM == 2 ? 1 : 10;

  /* Miscellaneous variables */
  float dir0[DIM]; /* Sampled direction */
  float dir1[DIM]; /* Sampled direction reflected */
  int iattempt = 0; /* #attempts to find a reinjection dir */
  res_T res = RES_OK;

  /* Pre-conditions */
  ASSERT(scn && args && step);
  ASSERT(XD(check_sample_reinjection_step_args)(scn, args) == RES_OK);

  /* Initialise the reinjection step */
  *step = REINJECTION_STEP_NULL;

  /* Check whether the solid enclosure is part of the system, or whether it is
   * only there to describe boundary conditions. In the latter case, the
   * enclosure has no geometrical existence and it is sufficient to return a
   * valid reinjection step which will be used to select the next step. Note
   * that if the trajectory passes through the solid enclosure, it will stop,
   * i.e.  the temperature of the solid should be fixed. If it doesn't, it's a
   * error */
  scene_get_enclosure_ids(scn, args->rwalk->XD(hit).prim.prim_id, enc_ids);
  solid_enc = scene_get_enclosure(scn, enc_ids[args->side]);
  if(solid_enc->medium_id == MEDIUM_ID_MULTI) {
    step->XD(hit) = SXD_HIT_NULL;
    fX(normalize)(step->direction, args->rwalk->XD(hit).normal);
    if(args->side == SDIS_BACK) fX(minus)(step->direction, step->direction);
    step->distance = (float)args->distance;
    goto exit; /* That's all folks! */
  }

  iattempt = 0;
  do {
    /* Sample a reinjection direction */
    XD(sample_reinjection_dir)(args->rwalk, args->rng, dir0);

    /* Reflect the sampled direction around the normal */
    XD(reflect)(dir1, dir0, args->rwalk->XD(hit).normal);

    /* Flip the sampled directions if one wants to reinject to back side */
    if(args->side == SDIS_BACK) {
      fX(minus)(dir0, dir0);
      fX(minus)(dir1, dir1);
    }

    /* Find the reinjection step */
    find_reinject_ray_args.solid_enc_id = args->solid_enc_id;
    find_reinject_ray_args.rwalk = args->rwalk;
    find_reinject_ray_args.distance = args->distance;
    find_reinject_ray_args.can_move = 1;
    fX(set)(find_reinject_ray_args.dir0, dir0);
    fX(set)(find_reinject_ray_args.dir1, dir1);
    res = XD(find_reinjection_ray_and_check_validity)
      (scn, &find_reinject_ray_args, &ray);
    if(res == RES_BAD_OP) continue; /* Cannot find a valid reinjection ray. Retry */
    if(res != RES_OK) goto error;

  } while(res != RES_OK && ++iattempt < MAX_ATTEMPTS);

  /* Could not find a valid reinjecton step */
  if(iattempt >= MAX_ATTEMPTS) {
    log_err(scn->dev,
      "%s: could not find a valid reinjection step at `%g %g %g'.\n",
      FUNC_NAME, SPLIT3(args->rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  /* Setup the reinjection step */
  step->XD(hit) = ray.hit;
  step->distance = ray.dst;
  fX(set)(step->direction, ray.dir);

  /* Update the random walk position if necessary */
  if(ray.position_was_moved) {
    dX(set)(args->rwalk->vtx.P, ray.org);
  }

  /* Post-conditions */
  ASSERT(dX(eq)(args->rwalk->vtx.P, ray.org));
  ASSERT(XD(check_reinjection_step)(step) == RES_OK);

exit:
  return res;
error:
  goto exit;
}

res_T
XD(sample_reinjection_step_solid_solid)
  (struct sdis_scene* scn,
   const struct sample_reinjection_step_args* args_frt,
   const struct sample_reinjection_step_args* args_bck,
   struct reinjection_step* step_frt,
   struct reinjection_step* step_bck)
{
  /* Input/output data of the function finding a valid reinjection ray */
  struct XD(find_reinjection_ray_args) find_reinject_ray_frt_args =
    XD(FIND_REINJECTION_RAY_ARGS_NULL);
  struct XD(find_reinjection_ray_args) find_reinject_ray_bck_args =
    XD(FIND_REINJECTION_RAY_ARGS_NULL);
  struct XD(reinjection_ray) ray_frt = XD(REINJECTION_RAY_NULL);
  struct XD(reinjection_ray) ray_bck = XD(REINJECTION_RAY_NULL);

  /* Initial random walk position used as a backup */
  double rwalk_pos_backup[DIM];

  /* Variables shared by the two side */
  struct rwalk* rwalk = NULL;
  struct ssp_rng* rng = NULL;

  /* In 2D it is useless to try to resample a reinjection direction since there
   * is only one possible direction */
  const int MAX_ATTEMPTS = DIM == 2 ? 1 : 10;

  /* Enclosure */
  unsigned enc_ids[2];
  const struct enclosure* enc_frt = NULL;
  const struct enclosure* enc_bck = NULL;

  float dir_frt_samp[DIM]; /* Sampled direction */
  float dir_frt_refl[DIM]; /* Sampled direction reflected */
  float dir_bck_samp[DIM]; /* Negated sampled direction */
  float dir_bck_refl[DIM]; /* Negated sampled direction reflected */
  int multi_frt = 0;
  int multi_bck = 0;
  int iattempt = 0; /* #attempts to find a reinjection dir */
  res_T res = RES_OK;

  /* Pre-conditions */
  ASSERT(scn && args_frt && args_bck && step_frt && step_bck);
  ASSERT(XD(check_sample_reinjection_step_args)(scn, args_frt) == RES_OK);
  ASSERT(XD(check_sample_reinjection_step_args)(scn, args_bck) == RES_OK);
  ASSERT(args_frt->side == SDIS_FRONT);
  ASSERT(args_bck->side == SDIS_BACK);
  ASSERT(SXD_PRIMITIVE_EQ(&args_frt->rwalk->XD(hit).prim, &args_bck->rwalk->XD(hit).prim));

  rng = args_frt->rng;
  rwalk = args_frt->rwalk;
  ASSERT(args_bck->rng == rng);
  ASSERT(args_bck->rwalk == rwalk);

  /* Initialise the reinjection steps */
  *step_frt = REINJECTION_STEP_NULL;
  *step_bck = REINJECTION_STEP_NULL;

  /* Check whether the solid enclosure is part of the system, or whether it is
   * only there to describe boundary conditions. In the latter case, the
   * enclosure has no geometrical existence and it is sufficient to return a
   * valid reinjection step which will be used to select the next step. Note
   * that if the trajectory passes through the solid enclosure, it will stop,
   * i.e.  the temperature of the solid should be fixed. If it doesn't, it's an
   * error */
  scene_get_enclosure_ids(scn, args_frt->rwalk->XD(hit).prim.prim_id, enc_ids);
  enc_frt = scene_get_enclosure(scn, enc_ids[SDIS_FRONT]);
  enc_bck = scene_get_enclosure(scn, enc_ids[SDIS_BACK]);
  if(enc_frt->medium_id == MEDIUM_ID_MULTI) {
    step_frt->XD(hit) = SXD_HIT_NULL;
    fX(normalize)(step_frt->direction, args_frt->rwalk->XD(hit).normal);
    step_frt->distance = (float)args_frt->distance;
    multi_frt = 1;
  }
  if(enc_bck->medium_id == MEDIUM_ID_MULTI) {
    step_bck->XD(hit) = SXD_HIT_NULL;
    fX(normalize)(step_bck->direction, args_bck->rwalk->XD(hit).normal);
    step_bck->distance = (float)args_bck->distance;
    multi_bck = 1;
  }

  if(multi_frt && multi_bck) goto exit; /* That's all folks */

  dX(set)(rwalk_pos_backup, rwalk->vtx.P);
  iattempt = 0;
  do {
    /* Restore random walk pos */
    if(iattempt != 0) dX(set)(rwalk->vtx.P, rwalk_pos_backup);

    /* Sample a reinjection direction and reflect it around the normal. Then
     * reflect them on the back side of the interface. */
    XD(sample_reinjection_dir)(rwalk, rng, dir_frt_samp);
    XD(reflect)(dir_frt_refl, dir_frt_samp, rwalk->XD(hit).normal);
    fX(minus)(dir_bck_samp, dir_frt_samp);
    fX(minus)(dir_bck_refl, dir_frt_refl);

    /* Reject the sampling of the re-injection step if it has already been
     * defined, i.e. if the enclosure is a limit condition */
    if(!multi_frt) {
      /* Find the reinjection ray for the front side */
      find_reinject_ray_frt_args.solid_enc_id = args_frt->solid_enc_id;
      find_reinject_ray_frt_args.rwalk = args_frt->rwalk;
      find_reinject_ray_frt_args.distance = args_frt->distance;
      find_reinject_ray_frt_args.can_move = 1;
      fX(set)(find_reinject_ray_frt_args.dir0, dir_frt_samp);
      fX(set)(find_reinject_ray_frt_args.dir1, dir_frt_refl);
      res = XD(find_reinjection_ray_and_check_validity)
        (scn, &find_reinject_ray_frt_args, &ray_frt);
      if(res == RES_BAD_OP) continue;
      if(res != RES_OK) goto error;

      /* Update the random walk position if necessary */
      if(ray_frt.position_was_moved) dX(set)(rwalk->vtx.P, ray_frt.org);
    }

    /* Reject the sampling of the re-injection step if it has already been
     * defined, i.e. if the enclosure is a limit condition */
    if(!multi_bck) {
      /* Select the reinjection direction and distance for the back side */
      find_reinject_ray_bck_args.solid_enc_id = args_bck->solid_enc_id;
      find_reinject_ray_bck_args.rwalk = args_bck->rwalk;
      find_reinject_ray_bck_args.distance = args_bck->distance;
      find_reinject_ray_bck_args.can_move = 1;
      fX(set)(find_reinject_ray_bck_args.dir0, dir_bck_samp);
      fX(set)(find_reinject_ray_bck_args.dir1, dir_bck_refl);
      res = XD(find_reinjection_ray_and_check_validity)
        (scn, &find_reinject_ray_bck_args, &ray_bck);
      if(res == RES_BAD_OP) continue;
      if(res != RES_OK) goto error;

      /* Update the random walk position if necessary */
      if(ray_bck.position_was_moved) dX(set)(rwalk->vtx.P, ray_bck.org);

      /* If random walk was moved to find a valid rinjection ray on back side,
       * one has to find a valid reinjection on front side from the new pos */
      if(ray_bck.position_was_moved) {
        find_reinject_ray_frt_args.can_move = 0;
        res = XD(find_reinjection_ray_and_check_validity)
          (scn, &find_reinject_ray_frt_args, &ray_frt);
        if(res == RES_BAD_OP) continue;
        if(res != RES_OK) goto error;

        /* Update the random walk position if necessary */
        if(ray_frt.position_was_moved) dX(set)(rwalk->vtx.P, ray_frt.org);
      }
    }
  } while(res != RES_OK && ++iattempt < MAX_ATTEMPTS);

  /* Could not find a valid reinjection */
  if(iattempt >= MAX_ATTEMPTS) {
    dX(set)(rwalk->vtx.P, rwalk_pos_backup); /* Restore random walk pos */
    log_warn(scn->dev,
      "%s: could not find a valid solid/solid reinjection at {%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  /* Setup the front and back reinjection steps */
  if(!multi_frt) {
    step_frt->XD(hit) = ray_frt.hit;
    step_frt->distance = ray_frt.dst;
    fX(set)(step_frt->direction, ray_frt.dir);
    ASSERT(XD(check_reinjection_step)(step_frt) == RES_OK); /* Post-condition */
  }
  if(!multi_bck) {
    step_bck->XD(hit) = ray_bck.hit;
    step_bck->distance = ray_bck.dst;
    fX(set)(step_bck->direction, ray_bck.dir);
    ASSERT(XD(check_reinjection_step)(step_bck) == RES_OK); /* Post-condition */
  }

exit:
  return res;
error:
  goto exit;
}

res_T
XD(solid_reinjection)
  (struct sdis_scene* scn,
   const unsigned solid_enc_id,
   struct solid_reinjection_args* args)
{
  /* Properties */
  struct solid_props props = SOLID_PROPS_NULL;
  struct sdis_medium* solid = NULL;
  const struct enclosure* enc = NULL;

  double reinject_dst_m; /* Reinjection distance in meters */
  double mu;
  res_T res = RES_OK;
  ASSERT(XD(check_solid_reinjection_args)(args) == RES_OK);
  ASSERT(solid_enc_id != ENCLOSURE_ID_NULL);

  reinject_dst_m = args->reinjection->distance * args->fp_to_meter;

  /* Get the enclosure medium properties */
  enc = scene_get_enclosure(scn, solid_enc_id);
  res = scene_get_enclosure_medium(scn, enc, &solid);
  if(res != RES_OK) goto error;
  ASSERT(sdis_medium_get_type(solid) == SDIS_SOLID);
  res = solid_get_properties(solid, &args->rwalk->vtx, &props);
  if(res != RES_OK) goto error;

  /* Manage the volumic power */
  res = XD(handle_volumic_power)
    (solid, args->rwalk_ctx, args->rwalk, reinject_dst_m, args->T);
  if(res != RES_OK) goto error;

  /* Time rewind */
  args->rwalk->enc_id = solid_enc_id; /* Enclosure into which the time is rewind */
  mu = (2*DIM*props.lambda)/(props.rho*props.cp*reinject_dst_m*reinject_dst_m);
  res = time_rewind
    (scn, mu, props.t0, args->rng, args->rwalk, args->rwalk_ctx, args->T);
  if(res != RES_OK) goto error;

  /* Test if a limit condition was reached */
  if(args->T->done) goto exit;

  /* Move the random walk to the reinjection position */
  XD(move_pos)
    (args->rwalk->vtx.P,
     args->reinjection->direction,
     args->reinjection->distance);

  /* The random walk is in the solid */
  if(args->reinjection->XD(hit).distance != args->reinjection->distance) {
    args->T->func = XD(conductive_path);
    args->rwalk->enc_id = solid_enc_id;
    args->rwalk->XD(hit) = SXD_HIT_NULL;
    args->rwalk->hit_side = SDIS_SIDE_NULL__;

  /* The random walk is at a boundary */
  } else {
    args->T->func = XD(boundary_path);
    args->rwalk->enc_id = ENCLOSURE_ID_NULL;
    args->rwalk->XD(hit) = args->reinjection->XD(hit);
    if(fX(dot)(args->reinjection->XD(hit).normal, args->reinjection->direction) < 0) {
      args->rwalk->hit_side = SDIS_FRONT;
    } else {
      args->rwalk->hit_side = SDIS_BACK;
    }
  }

  /* Register the new vertex against the heat path */
  res = register_heat_vertex
    (args->rwalk_ctx->heat_path,
     &args->rwalk->vtx,
     args->T->value,
     SDIS_HEAT_VERTEX_CONDUCTION,
     (int)args->rwalk_ctx->nbranchings);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

res_T
XD(handle_net_flux)
  (const struct sdis_scene* scn,
   const struct handle_net_flux_args* args,
   struct temperature* T)
{
  double flux_term;
  double phi;
  res_T res = RES_OK;
  CHK(scn && T);
  CHK(args->interf && args->frag);
  CHK(args->h_cond >= 0);
  CHK(args->h_conv >= 0);
  CHK(args->h_radi >= 0);
  CHK(args->h_cond + args->h_conv + args->h_radi > 0);

  phi = interface_side_get_flux(args->interf, args->frag);
  if(phi == SDIS_FLUX_NONE) goto exit; /* No flux. Do nothing */

  if(args->picard_order > 1 && phi != 0) {
    log_err(scn->dev,
      "%s: invalid flux '%g' W/m^2. Could not manage a flux != 0 when the "
      "picard order is not equal to 1; Picard order is currently set to %lu.\n",
      FUNC_NAME, phi, (unsigned long)args->picard_order);
    res = RES_BAD_ARG;
    goto error;
  }

  flux_term = 1.0 / (args->h_cond + args->h_conv + args->h_radi);
  T->value += phi * flux_term;

  /* Register the net flux term */
  if(args->green_path) {
    res = green_path_add_flux_term
      (args->green_path, args->interf, args->frag, flux_term);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
XD(check_Tref)
  (const struct sdis_scene* scn,
   const double pos[DIM],
   const double Tref,
   const char* func_name)
{
  ASSERT(scn && pos && func_name);

  #define CHECK_TBOUND(Bound, Name) {                                          \
    if(SDIS_TEMPERATURE_IS_UNKNOWN(Bound)) {                                   \
      log_err(scn->dev,                                                        \
        "%s: the "Name" temperature cannot be unknown "                        \
        "to sampling a radiative path.\n",                                     \
        func_name);                                                            \
      return RES_BAD_OP_IRRECOVERABLE;                                         \
    }                                                                          \
                                                                               \
    if((Bound) < 0) {                                                          \
      log_err(scn->dev,                                                        \
        "%s: the "Name" temperature cannot be negative "                       \
        "to sample a radiative path -- T"Name" = %g K\n",                      \
        func_name, (Bound));                                                   \
      return RES_BAD_OP_IRRECOVERABLE;                                         \
    }                                                                          \
  } (void) 0
  CHECK_TBOUND(scn->tmin, "min");
  CHECK_TBOUND(scn->tmax, "max");
  #undef CHECK_TBOUND

  if(scn->tmin > scn->tmax) {
    log_err(scn->dev,
      "%s: the temperature range cannot be degenerated to sample a radiative "
      "path (Tmin = %g K; Tmax = %g K).\n",
      func_name, scn->tmin, scn->tmax);
    return RES_BAD_OP_IRRECOVERABLE;
  }

  if(SDIS_TEMPERATURE_IS_UNKNOWN(Tref)) {
    log_err(scn->dev,
      "%s: the reference temperature is unknown at ("FORMAT_VECX"). "
      "Sampling a radiative path requires a valid reference temperature field.\n",
      func_name, SPLITX(pos));
    return RES_BAD_OP_IRRECOVERABLE;
  }

  if(Tref < 0) {
    log_err(scn->dev,
      "%s: the reference temperature is negative at ("FORMAT_VECX") and Tref = %g K. "
      "Sampling a radiative path requires a known, positive reference "
      "temperature field.\n",
      func_name, SPLITX(pos), Tref);
    return RES_BAD_OP_IRRECOVERABLE;
  }

  if(Tref < scn->tmin || scn->tmax < Tref) {
    log_err(scn->dev,
      "%s: invalid reference temperature at ("FORMAT_VECX") and Tref=%g K. "
      "It must be included in the provided temperature range "
      "(Tmin = %g K; Tmax = %g K)\n",
      func_name, SPLITX(pos), Tref, scn->tmin, scn->tmax);
    return RES_BAD_OP_IRRECOVERABLE;
  }

  return RES_OK;
}

/* This function checks whether the random walk is on a boundary and, if so,
 * verifies that the temperature of the medium attached to the interface is
 * known. This medium can be different from the medium of the enclosure. Indeed,
 * the enclosure can contain several media used to set the temperatures of
 * several boundary conditions. Hence this function, which queries the medium on
 * the path coming from a boundary */
res_T
XD(query_medium_temperature_from_boundary)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct temperature* T)
{
  struct sdis_interface* interf = NULL;
  struct sdis_medium* mdm = NULL;
  double temperature = SDIS_TEMPERATURE_NONE;
  res_T res = RES_OK;
  ASSERT(scn && ctx && rwalk && T);

  /* Not at an interface */
  if(SXD_HIT_NONE(&rwalk->XD(hit))) return RES_OK; /* Nothing to do */

  interf = scene_get_interface(scn, rwalk->XD(hit).prim.prim_id);
  mdm = rwalk->hit_side==SDIS_FRONT ? interf->medium_front: interf->medium_back;

  temperature = medium_get_temperature(mdm, &rwalk->vtx);

  /* Check if the temperature is known */
  if(SDIS_TEMPERATURE_IS_UNKNOWN(temperature)) goto exit;

  T->value += temperature;
  T->done = 1;

  if(ctx->green_path) {
    res = green_path_set_limit_vertex
      (ctx->green_path, mdm, &rwalk->vtx, rwalk->elapsed_time);
    if(res != RES_OK) goto error;
  }

  if(ctx->heat_path) {
    heat_path_get_last_vertex(ctx->heat_path)->weight = T->value;
  }

exit:
  return res;
error:
  goto exit;
}

#if DIM == 2
void
XD(move_away_primitive_boundaries)
  (const struct sXd(hit)* hit,
   const double delta,
   double position[DIM]) /* Position to move */
{
  struct sXd(attrib) attr;
  float pos[DIM];
  float dir[DIM];
  float len;
  const float st = 0.5f;
  ASSERT(!SXD_HIT_NONE(hit) && delta > 0);

  SXD(primitive_get_attrib(&hit->prim, SXD_POSITION, st, &attr));

  fX_set_dX(pos, position);
  fX(sub)(dir, attr.value, pos);
  len = fX(normalize)(dir, dir);
  len = MMIN(len, (float)(delta*0.1));

  XD(move_pos)(position, dir, len);
}
#else
/* Move the submitted position away from the primitive boundaries to avoid
 * numerical issues leading to inconsistent random walks. */
void
XD(move_away_primitive_boundaries)
  (const struct sXd(hit)* hit,
   const double delta,
   double position[DIM])
{
  struct s3d_attrib v0, v1, v2; /* Triangle vertices */
  float E[3][4]; /* 3D edge equations */
  float dst[3]; /* Distance from current position to edge equation */
  float N[3]; /* Triangle normal */
  float P[3]; /* Random walk position */
  float tmp[3];
  float min_dst, max_dst;
  float len;
  int imax = 0;
  int imin = 0;
  int imid = 0;
  int i;
  ASSERT(delta > 0 && !S3D_HIT_NONE(hit));

  fX_set_dX(P, position);

  /* Fetch triangle vertices */
  S3D(triangle_get_vertex_attrib(&hit->prim, 0, S3D_POSITION, &v0));
  S3D(triangle_get_vertex_attrib(&hit->prim, 1, S3D_POSITION, &v1));
  S3D(triangle_get_vertex_attrib(&hit->prim, 2, S3D_POSITION, &v2));

  /* Compute the edge vector */
  f3_sub(E[0], v1.value, v0.value);
  f3_sub(E[1], v2.value, v1.value);
  f3_sub(E[2], v0.value, v2.value);

  /* Compute the triangle normal */
  f3_cross(N, E[1], E[0]);

  /* Compute the 3D edge equation */
  f3_normalize(E[0], f3_cross(E[0], E[0], N));
  f3_normalize(E[1], f3_cross(E[1], E[1], N));
  f3_normalize(E[2], f3_cross(E[2], E[2], N));
  E[0][3] = -f3_dot(E[0], v0.value);
  E[1][3] = -f3_dot(E[1], v1.value);
  E[2][3] = -f3_dot(E[2], v2.value);

  /* Compute the distance from current position to the edges */
  dst[0] = f3_dot(E[0], P) + E[0][3];
  dst[1] = f3_dot(E[1], P) + E[1][3];
  dst[2] = f3_dot(E[2], P) + E[2][3];

  /* Retrieve the min and max distance from random walk position to triangle
   * edges */
  min_dst = MMIN(MMIN(dst[0], dst[1]), dst[2]);
  max_dst = MMAX(MMAX(dst[0], dst[1]), dst[2]);

  /* Sort the edges with respect to their distance to the random walk position */
  FOR_EACH(i, 0, 3) {
    if(dst[i] == min_dst) {
      imin = i;
    } else if(dst[i] == max_dst) {
      imax = i;
    } else {
      imid = i;
    }
  }
  (void)imax;

  if(eq_eps(dst[imin], 0, delta*1e-3) && eq_eps(dst[imid], 0, delta*1e-3)) {
    /* The random position is in a corner, meaning that its distance to the two
     * nearest edges is approximately equal to 0. Move it toward the farthest
     * edge along its normal to avoid moving too little. */
    len = MMIN(dst[imax]*0.5f, (float)delta*0.1f);
    XD(move_pos)(position, f3_minus(tmp, E[imax]), len);

  } else {
    /* Compute the distance `dst' from the current position to the edges to move
     * to, along the normal of the edge from which the random walk is the nearest
     *
     *           +.                 cos(a) = d / dst => dst = d / cos_a
     *          /  `*.
     *         /    | `*.
     *        /  dst| a /`*.
     *       /      |  /    `*.
     *      /       | / d      `*.
     *     /        |/            `*.
     *    +---------o----------------+  */
    const float cos_a1 = f3_dot(E[imin], f3_minus(tmp, E[imid]));
    const float cos_a2 = f3_dot(E[imin], f3_minus(tmp, E[imax]));
    dst[imid] = cos_a1 > 0 ? dst[imid] / cos_a1 : FLT_MAX;
    dst[imax] = cos_a2 > 0 ? dst[imax] / cos_a2 : FLT_MAX;
    len = MMIN(dst[imid], dst[imax]);
    ASSERT(len != FLT_MAX);

    /* Define the displacement distance as the minimum between 10 percent of
     * delta and len / 2. */
    len = MMIN(len*0.5f, (float)(delta*0.1));
    XD(move_pos)(position, E[imin], len);
  }

}
#endif

#include "sdis_Xd_end.h"
