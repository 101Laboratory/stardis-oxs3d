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

struct XD(find_reinjection_ray_args) {
  const struct sdis_medium* solid; /* Medium into which the reinjection occurs */
  const struct XD(rwalk)* rwalk; /* Current random walk state */
  float dir0[DIM]; /* Challenged ray direction */
  float dir1[DIM]; /* Challenged ray direction */
  double distance; /* Maximum reinjection distance */

  /* Define if the random walk position can be moved or not to find a valid
   * reinjection direction */
  int can_move;
};
static const struct XD(find_reinjection_ray_args)
XD(FIND_REINJECTION_RAY_ARGS_NULL) = { NULL, NULL, {0}, {0}, 0, 0 };

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
static INLINE int
XD(check_find_reinjection_ray_args)
  (const struct XD(find_reinjection_ray_args)* args)
{
  return args
      && args->solid
      && args->rwalk
      && args->distance > 0
      && fX(is_normalized)(args->dir0)
      && fX(is_normalized)(args->dir1);
}

static INLINE int
XD(check_sample_reinjection_step_args)
  (const struct XD(sample_reinjection_step_args)* args)
{
  return args
      && args->rng
      && args->solid
      && args->solid->type == SDIS_SOLID
      && args->rwalk
      && args->distance > 0
      && (unsigned)args->side < SDIS_SIDE_NULL__;
}

static INLINE int
XD(check_reinjection_step)(const struct XD(reinjection_step)* step)
{
  return step
      && fX(is_normalized)(step->direction)
      && step->distance > 0;
}

static INLINE int
XD(check_solid_reinjection_args)(const struct XD(solid_reinjection_args)* args)
{
  return args
      && XD(check_reinjection_step)(args->reinjection)
      && args->rng
      && args->rwalk
      && args->rwalk_ctx
      && args->T
      && args->fp_to_meter > 0;
}

/* Check that the interface fragment is consistent with the current state of
 * the random walk */
static INLINE int
XD(check_rwalk_fragment_consistency)
  (const struct XD(rwalk)* rwalk,
   const struct sdis_interface_fragment* frag)
{
  double N[DIM];
  double uv[2] = {0, 0};
  ASSERT(rwalk && frag);
  dX(normalize)(N, dX_set_fX(N, rwalk->hit.normal));
  if( SXD_HIT_NONE(&rwalk->hit)
  || !dX(eq_eps)(rwalk->vtx.P, frag->P, 1.e-6)
  || !dX(eq_eps)(N, frag->Ng, 1.e-6)
  || !(  (IS_INF(rwalk->vtx.time) && IS_INF(frag->time))
      || eq_eps(rwalk->vtx.time, frag->time,  1.e-6))) {
    return 0;
  }
#if (SDIS_XD_DIMENSION == 2)
  uv[0] = rwalk->hit.u;
#else
  d2_set_f2(uv, rwalk->hit.uv);
#endif
  return d2_eq_eps(uv, frag->uv, 1.e-6);
}

static void
XD(sample_reinjection_dir)
  (const struct XD(rwalk)* rwalk,
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
  ASSERT(!SXD_HIT_NONE(&rwalk->hit));
  ASSERT(!rwalk->mdm);

  if(r) {
    dir[0] = rwalk->hit.normal[0] - rwalk->hit.normal[1];
    dir[1] = rwalk->hit.normal[0] + rwalk->hit.normal[1];
  } else {
    dir[0] = rwalk->hit.normal[0] + rwalk->hit.normal[1];
    dir[1] =-rwalk->hit.normal[0] + rwalk->hit.normal[1];
  }
  f2_normalize(dir, dir);
#else
  /* Sample a random direction around the normal whose cosine is 1/sqrt(3). To
   * do so we sample a position onto a cone whose height is 1/sqrt(2) and the
   * radius of its base is 1. */
  float frame[9];
  ASSERT(rwalk && rng && dir);
  ASSERT(!SXD_HIT_NONE(&rwalk->hit));
  ASSERT(!rwalk->mdm);
  ASSERT(fX(is_normalized)(rwalk->hit.normal));

  ssp_ran_circle_uniform_float(rng, dir, NULL);
  dir[2]  = (float)(1.0/sqrt(2));

  f33_basis(frame, rwalk->hit.normal);
  f33_mulf3(dir, frame, dir);
  f3_normalize(dir, dir);
  ASSERT(eq_epsf(f3_dot(dir, rwalk->hit.normal), (float)(1.0/sqrt(3)), 1.e-4f));
#endif
}


#if DIM == 2
static void
XD(move_away_primitive_boundaries)
  (const struct XD(rwalk)* rwalk,
   const double delta,
   double position[DIM]) /* Position to move */
{
  struct sXd(attrib) attr;
  float pos[DIM];
  float dir[DIM];
  float len;
  const float st = 0.5f;
  ASSERT(rwalk && !SXD_HIT_NONE(&rwalk->hit) && delta > 0);

  SXD(primitive_get_attrib(&rwalk->hit.prim, SXD_POSITION, st, &attr));

  fX_set_dX(pos, position);
  fX(sub)(dir, attr.value, pos);
  len = fX(normalize)(dir, dir);
  len = MMIN(len, (float)(delta*0.1));

  XD(move_pos)(position, dir, len);
}
#else
/* Move the submitted position away from the primitive boundaries to avoid
 * numerical issues leading to inconsistent random walks. */
static void
XD(move_away_primitive_boundaries)
  (const struct XD(rwalk)* rwalk,
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
  float cos_a1, cos_a2;
  float len;
  int imax = 0;
  int imin = 0;
  int imid = 0;
  int i;
  ASSERT(rwalk && delta > 0 && !S3D_HIT_NONE(&rwalk->hit));

  fX_set_dX(P, position);

  /* Fetch triangle vertices */
  S3D(triangle_get_vertex_attrib(&rwalk->hit.prim, 0, S3D_POSITION, &v0));
  S3D(triangle_get_vertex_attrib(&rwalk->hit.prim, 1, S3D_POSITION, &v1));
  S3D(triangle_get_vertex_attrib(&rwalk->hit.prim, 2, S3D_POSITION, &v2));

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

  /* TODO if the current position is near a vertex, one should move toward the
   * farthest edge along its normal to avoid too small displacement */

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
  cos_a1 = f3_dot(E[imin], f3_minus(tmp, E[imid]));
  cos_a2 = f3_dot(E[imin], f3_minus(tmp, E[imax]));
  dst[imid] = cos_a1 > 0 ? dst[imid] / cos_a1 : FLT_MAX;
  dst[imax] = cos_a2 > 0 ? dst[imax] / cos_a2 : FLT_MAX;

  /* Compute the maximum displacement distance into the triangle along the
   * normal of the edge from which the random walk is the nearest */
  len = MMIN(dst[imid], dst[imax]);
  ASSERT(len != FLT_MAX);

  /* Define the displacement distance as the minimum between 10 percent of
   * delta and len / 2. */
  len = MMIN(len*0.5f, (float)(delta*0.1));
  XD(move_pos)(position, E[imin], len);
}
#endif

static res_T
XD(find_reinjection_ray)
  (const struct sdis_scene* scn,
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

  /* Physical properties */
  struct sdis_interface* interf;
  struct sdis_medium* mdm0;
  struct sdis_medium* mdm1;

  struct hit_filter_data filter_data;
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
  ASSERT(XD(check_find_reinjection_ray_args)(args));

  *ray = XD(REINJECTION_RAY_NULL);
  MAX_ATTEMPTS = args->can_move ? 2 : 1;

  dst_adjusted = args->distance * RAY_RANGE_MAX_SCALE;
  reinject_threshold = (float)args->distance * REINJECT_DST_MIN_SCALE;

  dX(set)(ray->org, args->rwalk->vtx.P);

  do {
    fX_set_dX(org, ray->org);
    filter_data.XD(hit) = args->rwalk->hit;
    filter_data.epsilon = args->distance * 0.01;
    SXD(scene_view_trace_ray
      (scn->sXd(view), org, args->dir0, range, &filter_data, &hit0));
    SXD(scene_view_trace_ray
      (scn->sXd(view), org, args->dir1, range, &filter_data, &hit1));

    /* Retrieve the medium at the reinjection pos along dir0 */
    if(SXD_HIT_NONE(&hit0)) {
      XD(move_pos)(dX(set)(tmp, ray->org), args->dir0, (float)args->distance);
      res = scene_get_medium_in_closed_boundaries(scn, tmp, &mdm0);
      if(res == RES_BAD_OP) { mdm0 = NULL; res = RES_OK; }
      if(res != RES_OK) goto error;
    } else {
      interf = scene_get_interface(scn, hit0.prim.prim_id);
      side = fX(dot)(args->dir0, hit0.normal) < 0 ? SDIS_FRONT : SDIS_BACK;
      mdm0 = interface_get_medium(interf, side);
    }

    /* Retrieve the medium at the reinjection pos along dir1 */
    if(SXD_HIT_NONE(&hit1)) {
      XD(move_pos)(dX(set)(tmp, ray->org), args->dir1, (float)args->distance);
      res = scene_get_medium_in_closed_boundaries(scn, tmp, &mdm1);
      if(res == RES_BAD_OP) { mdm1 = NULL; res = RES_OK; }
      if(res != RES_OK) goto error;
    } else {
      interf = scene_get_interface(scn, hit1.prim.prim_id);
      side = fX(dot)(args->dir1, hit1.normal) < 0 ? SDIS_FRONT : SDIS_BACK;
      mdm1 = interface_get_medium(interf, side);
    }

    dst0 = dst1 = -1;
    if(mdm0 == args->solid) { /* Check reinjection consistency */
      if(hit0.distance <= dst_adjusted) {
        dst0 = hit0.distance;
      } else {
        dst0 = args->distance;
        hit0 = SXD_HIT_NULL;
      }
    }
    if(mdm1 == args->solid) { /* Check reinjection consistency */
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
    if(dst0 == -1 && dst1 == -1) {
      XD(move_away_primitive_boundaries)(args->rwalk, args->distance, ray->org);
      ray->position_was_moved = 1;
    }
  } while(dst0 == -1 && dst1 == -1 && ++iattempt < MAX_ATTEMPTS);

  if(dst0 == -1 && dst1 == -1) { /* No valid reinjection */
#if DIM == 2
    log_warn(scn->dev, "%s: no valid reinjection direction at {%g, %g}.\n",
      FUNC_NAME, SPLIT2(ray->org));
#else
   log_warn(scn->dev, "%s: no valid reinjection direction at {%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(ray->org));
#endif
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
  (const struct sdis_scene* scn,
   const struct XD(find_reinjection_ray_args)* args,
   struct XD(reinjection_ray)* ray)
{
  double pos[DIM];
  struct sdis_medium* reinject_mdm;
  res_T res = RES_OK;

  ASSERT(scn && args && ray);
  ASSERT(XD(check_find_reinjection_ray_args)(args));

  /* Select a reinjection direction */
  res = XD(find_reinjection_ray)(scn, args, ray);
  if(res != RES_OK) goto error;

  if(SXD_HIT_NONE(&ray->hit)) {
    /* Check medium consistency at the reinjection position */
    XD(move_pos)(dX(set)(pos, ray->org), ray->dir, (float)ray->dst);
    res = scene_get_medium_in_closed_boundaries(scn, pos, &reinject_mdm);
    if(res != RES_OK) goto error;

    if(reinject_mdm != args->solid) {
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
   struct XD(rwalk)* rwalk,
   const double reinject_dst_m,
   struct XD(temperature)* T)
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
  (const struct sdis_scene* scn,
   const struct XD(sample_reinjection_step_args)* args,
   struct XD(reinjection_step)* step)
{
  /* Input/output data of the function finding a valid reinjection ray */
  struct XD(find_reinjection_ray_args) find_reinject_ray_args =
    XD(FIND_REINJECTION_RAY_ARGS_NULL);
  struct XD(reinjection_ray) ray = XD(REINJECTION_RAY_NULL);

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
  ASSERT(XD(check_sample_reinjection_step_args)(args));

  iattempt = 0;
  do {
    /* Sample a reinjection direction */
    XD(sample_reinjection_dir)(args->rwalk, args->rng, dir0);

    /* Reflect the sampled direction around the normal */
    XD(reflect)(dir1, dir0, args->rwalk->hit.normal);

    /* Flip the sampled directions if one wants to reinject to back side */
    if(args->side == SDIS_BACK) {
      fX(minus)(dir0, dir0);
      fX(minus)(dir1, dir1);
    }

    /* Find the reinjection step */
    find_reinject_ray_args.solid = args->solid;
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
    log_warn(scn->dev,
      "%s: could not find a valid reinjection step at `%g %g %g'.\n",
      FUNC_NAME, SPLIT3(args->rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  /* Setup the reinjection step */
  step->hit = ray.hit;
  step->distance = ray.dst;
  fX(set)(step->direction, ray.dir);

  /* Update the random walk position if necessary */
  if(ray.position_was_moved) {
    dX(set)(args->rwalk->vtx.P, ray.org);
  }

  /* Post-conditions */
  ASSERT(dX(eq)(args->rwalk->vtx.P, ray.org));
  ASSERT(XD(check_reinjection_step)(step));

exit:
  return res;
error:
  goto exit;
}

res_T
XD(sample_reinjection_step_solid_solid)
  (const struct sdis_scene* scn,
   const struct XD(sample_reinjection_step_args)* args_frt,
   const struct XD(sample_reinjection_step_args)* args_bck,
   struct XD(reinjection_step)* step_frt,
   struct XD(reinjection_step)* step_bck)
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
  struct XD(rwalk)* rwalk = NULL;
  struct ssp_rng* rng = NULL;

  /* In 2D it is useless to try to resample a reinjection direction since there
   * is only one possible direction */
  const int MAX_ATTEMPTS = DIM == 2 ? 1 : 10;

  float dir_frt_samp[DIM]; /* Sampled direction */
  float dir_frt_refl[DIM]; /* Sampled direction reflected */
  float dir_bck_samp[DIM]; /* Negated sampled direction */
  float dir_bck_refl[DIM]; /* Negated sampled direction reflected */
  int iattempt = 0; /* #attempts to find a reinjection dir */
  res_T res = RES_OK;

  /* Pre-conditions */
  ASSERT(scn && args_frt && args_bck && step_frt && step_bck);
  ASSERT(XD(check_sample_reinjection_step_args)(args_frt));
  ASSERT(XD(check_sample_reinjection_step_args)(args_bck));
  ASSERT(args_frt->side == SDIS_FRONT);
  ASSERT(args_bck->side == SDIS_BACK);

  rng = args_frt->rng;
  rwalk = args_frt->rwalk;
  ASSERT(args_bck->rng == rng);
  ASSERT(args_bck->rwalk == rwalk);

  dX(set)(rwalk_pos_backup, rwalk->vtx.P);
  iattempt = 0;
  do {
    /* Restore random walk pos */
    if(iattempt != 0) dX(set)(rwalk->vtx.P, rwalk_pos_backup);

    /* Sample a reinjection direction and reflect it around the normal. Then
     * reflect them on the back side of the interface. */
    XD(sample_reinjection_dir)(rwalk, rng, dir_frt_samp);
    XD(reflect)(dir_frt_refl, dir_frt_samp, rwalk->hit.normal);
    fX(minus)(dir_bck_samp, dir_frt_samp);
    fX(minus)(dir_bck_refl, dir_frt_refl);

    /* Find the reinjection ray for the front side */
    find_reinject_ray_frt_args.solid = args_frt->solid;
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

    /* Select the reinjection direction and distance for the back side */
    find_reinject_ray_bck_args.solid = args_bck->solid;
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
     * one has to find a valid reinjection ob front side from the new pos */
    if(ray_bck.position_was_moved) {
      find_reinject_ray_frt_args.can_move = 0;
      res = XD(find_reinjection_ray_and_check_validity)
        (scn, &find_reinject_ray_frt_args, &ray_frt);
      if(res == RES_BAD_OP) continue;
      if(res != RES_OK) goto error;

      /* Update the random walk position if necessary */
      if(ray_frt.position_was_moved) dX(set)(rwalk->vtx.P, ray_frt.org);
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
  step_frt->hit = ray_frt.hit;
  step_bck->hit = ray_bck.hit;
  step_frt->distance = ray_frt.dst;
  step_bck->distance = ray_bck.dst;
  fX(set)(step_frt->direction, ray_frt.dir);
  fX(set)(step_bck->direction, ray_bck.dir);

  /* Post-conditions */
  ASSERT(XD(check_reinjection_step)(step_frt));
  ASSERT(XD(check_reinjection_step)(step_bck));

exit:
  return res;
error:
  goto exit;
}

res_T
XD(solid_reinjection)
  (struct sdis_medium* solid,
   struct XD(solid_reinjection_args)* args)
{
  double reinject_dst_m; /* Reinjection distance in meters */
  res_T res = RES_OK;
  ASSERT(solid && XD(check_solid_reinjection_args)(args));

  reinject_dst_m = args->reinjection->distance * args->fp_to_meter;

  /* Manage the volumic power */
  res = XD(handle_volumic_power)
    (solid, args->rwalk_ctx, args->rwalk, reinject_dst_m, args->T);
  if(res != RES_OK) goto error;

  /* Time rewind */
  res = XD(time_rewind)
    (solid, args->rng, reinject_dst_m, args->rwalk_ctx, args->rwalk, args->T);
  if(res != RES_OK) goto error;

  /* Test if a limit condition was reached */
  if(args->T->done) goto exit;

  /* Move the random walk to the reinjection position */
  XD(move_pos)
    (args->rwalk->vtx.P,
     args->reinjection->direction,
     args->reinjection->distance);

  /* The random walk is in the solid */
  if(args->reinjection->hit.distance != args->reinjection->distance) {
    args->T->func = XD(conductive_path);
    args->rwalk->mdm = solid;
    args->rwalk->hit = SXD_HIT_NULL;
    args->rwalk->hit_side = SDIS_SIDE_NULL__;

  /* The random walk is at a boundary */
  } else {
    args->T->func = XD(boundary_path);
    args->rwalk->mdm = NULL;
    args->rwalk->hit = args->reinjection->hit;
    if(fX(dot)(args->reinjection->hit.normal, args->reinjection->direction) < 0) {
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

#include "sdis_Xd_end.h"
