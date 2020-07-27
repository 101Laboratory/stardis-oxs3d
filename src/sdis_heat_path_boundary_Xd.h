/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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

#include "sdis_device_c.h"
#include "sdis_green.h"
#include "sdis_heat_path.h"
#include "sdis_interface_c.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/* Emperical scale factor applied to the challenged reinjection distance. If
 * the distance to reinject is less than this adjusted value, the solver will
 * try to discard the reinjection distance if possible in order to avoid
 * numerical issues. */
#define REINJECT_DST_MIN_SCALE 0.125f

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
XD(sample_reinjection_dir)
  (const struct XD(rwalk)* rwalk, struct ssp_rng* rng, float dir[DIM])
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
  ASSERT(rwalk && dir);
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
  (struct XD(rwalk)* rwalk,
   const double delta)
{
  struct sXd(attrib) attr;
  float pos[DIM];
  float dir[DIM];
  float len;
  const float st = 0.5f;
  ASSERT(rwalk && !SXD_HIT_NONE(&rwalk->hit) && delta > 0);

  SXD(primitive_get_attrib(&rwalk->hit.prim, SXD_POSITION, st, &attr));

  fX_set_dX(pos, rwalk->vtx.P);
  fX(sub)(dir, attr.value, pos);
  len = fX(normalize)(dir, dir);
  len = MMIN(len, (float)(delta*0.1));

  XD(move_pos)(rwalk->vtx.P, dir, len);
}
#else
/* Move the random walk away from the primitive boundaries to avoid numerical
 * issues leading to inconsistent random walks. */
static void
XD(move_away_primitive_boundaries)
  (struct XD(rwalk)* rwalk,
   const double delta)
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

  fX_set_dX(P, rwalk->vtx.P);

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
  XD(move_pos)(rwalk->vtx.P, E[imin], len);
}
#endif

static res_T
XD(select_reinjection_dir)
  (const struct sdis_scene* scn,
   const struct sdis_medium* mdm, /* Medium into which the reinjection occurs */
   struct XD(rwalk)* rwalk, /* Current random walk state */
   const float dir0[DIM], /* Challenged direction */
   const float dir1[DIM], /* Challanged direction */
   const double delta, /* Max reinjection distance */
   float reinject_dir[DIM], /* Selected direction */
   float* reinject_dst, /* Effective reinjection distance */
   int can_move, /* Define of the random wal pos can be moved or not */
   int* move_pos, /* Define if the current random walk was moved. May be NULL */
   struct sXd(hit)* reinject_hit) /* Hit along the reinjection dir */
{
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
  const double delta_adjusted = delta * RAY_RANGE_MAX_SCALE;
  const float* dir;
  const float reinject_threshold = (float)delta * REINJECT_DST_MIN_SCALE;
  float org[DIM];
  float range[2];
  enum sdis_side side;
  int iattempt = 0;
  const int MAX_ATTEMPTS = can_move ? 2 : 1;
  res_T res = RES_OK;
  ASSERT(scn && mdm && rwalk && dir0 && dir1 && delta > 0);
  ASSERT(reinject_dir && reinject_dst && reinject_hit);

  if(move_pos) *move_pos = 0;

  do {
    f2(range, 0, FLT_MAX);
    fX_set_dX(org, rwalk->vtx.P);
    filter_data.XD(hit) = rwalk->hit;
    filter_data.epsilon = delta * 0.01;
    SXD(scene_view_trace_ray(scn->sXd(view), org, dir0, range, &filter_data, &hit0));
    SXD(scene_view_trace_ray(scn->sXd(view), org, dir1, range, &filter_data, &hit1));

    /* Retrieve the medium at the reinjection pos along dir0 */
    if(SXD_HIT_NONE(&hit0)) {
      XD(move_pos)(dX(set)(tmp, rwalk->vtx.P), dir0, (float)delta);
      res = scene_get_medium_in_closed_boundaries(scn, tmp, &mdm0);
      if(res == RES_BAD_OP) { mdm0 = NULL; res = RES_OK; }
      if(res != RES_OK) goto error;
    } else {
      interf = scene_get_interface(scn, hit0.prim.prim_id);
      side = fX(dot)(dir0, hit0.normal) < 0 ? SDIS_FRONT : SDIS_BACK;
      mdm0 = interface_get_medium(interf, side);
    }

    /* Retrieve the medium at the reinjection pos along dir1 */
    if(SXD_HIT_NONE(&hit1)) {
      XD(move_pos)(dX(set)(tmp, rwalk->vtx.P), dir1, (float)delta);
      res = scene_get_medium_in_closed_boundaries(scn, tmp, &mdm1);
      if(res == RES_BAD_OP) { mdm1 = NULL; res = RES_OK; }
      if(res != RES_OK) goto error;
    } else {
      interf = scene_get_interface(scn, hit1.prim.prim_id);
      side = fX(dot)(dir1, hit1.normal) < 0 ? SDIS_FRONT : SDIS_BACK;
      mdm1 = interface_get_medium(interf, side);
    }

    dst0 = dst1 = -1;
    if(mdm0 == mdm) { /* Check reinjection consistency */
      if(hit0.distance <= delta_adjusted) {
        dst0 = hit0.distance;
      } else {
        dst0 = delta;
        hit0 = SXD_HIT_NULL;
      }
    }
    if(mdm1 == mdm) {/* Check reinjection consistency */
      if(hit1.distance <= delta_adjusted) {
        dst1 = hit1.distance;
      } else {
        dst1 = delta;
        hit1 = SXD_HIT_NULL;
      }
    }

    /* No valid reinjection. Maybe the random walk is near a sharp corner and
     * thus the ray-tracing misses the enclosure geometry. Another possibility
     * is that the random walk lies roughly on an edge. In this case, sampled
     * reinjecton dirs can intersect the primitive on the other side of the
     * edge. Normally, this primitive should be filtered by the "hit_filter"
     * function but this may be not the case due to a "threshold effect". In
     * both situations, try to slightly move away from the primitive boundaries
     * and retry to find a valid reinjection. */
    if(dst0 == -1 && dst1 == -1) {
      XD(move_away_primitive_boundaries)(rwalk, delta);
      if(move_pos) *move_pos = 1;
    }
  } while(dst0 == -1 && dst1 == -1 && ++iattempt < MAX_ATTEMPTS);

  if(dst0 == -1 && dst1 == -1) { /* No valid reinjection */
   log_warn(scn->dev, "%s: no valid reinjection direction at {%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  if(dst0 == -1) {
    /* Invalid dir0 -> move along dir1 */
    dir = dir1;
    dst = dst1;
    hit = hit1;
  } else if(dst1 == -1) {
    /* Invalid dir1 -> move along dir0 */
    dir = dir0;
    dst = dst0;
    hit = hit0;
  } else if(dst0 < reinject_threshold && dst1 < reinject_threshold) {
    /* The displacement along dir0 and dir1 are both below the reinjection
     * threshold that defines a distance under which the temperature gradients
     * are ignored. Move along the direction that allows the maximum
     * displacement. */
    if(dst0 > dst1) {
      dir = dir0;
      dst = dst0;
      hit = hit0;
    } else {
      dir = dir1;
      dst = dst1;
      hit = hit1;
    }
  } else if(dst0 < reinject_threshold) {
    /* Ingore dir0 that is bellow the reinject threshold */
    dir = dir1;
    dst = dst1;
    hit = hit1;
  } else if(dst1 < reinject_threshold) {
    /* Ingore dir1 that is bellow the reinject threshold */
    dir = dir0;
    dst = dst0;
    hit = hit0;
  } else {
    /* All reinjection directions are valid. Choose the first 1 that was
     * randomly selected by the sample_reinjection_dir procedure and adjust
     * the displacement distance. */
    dir = dir0;

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

  /* Setup output variable */
  fX(set)(reinject_dir, dir);
  *reinject_dst = (float)dst;
  *reinject_hit = hit;

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(select_reinjection_dir_and_check_validity)
  (const struct sdis_scene* scn,
   const struct sdis_medium* mdm, /* Medium into which the reinjection occurs */
   struct XD(rwalk)* rwalk, /* Current random walk state */
   const float dir0[DIM], /* Challenged direction */
   const float dir1[DIM], /* Challanged direction */
   const double delta, /* Max reinjection distance */
   float out_reinject_dir[DIM], /* Selected direction */
   float* out_reinject_dst, /* Effective reinjection distance */
   int can_move, /* Define of the random wal pos can be moved or not */
   int* move_pos, /* Define if the current random walk was moved. May be NULL */
   int* is_valid, /* Define if the reinjection defines a valid pos */
   struct sXd(hit)* out_reinject_hit) /* Hit along the reinjection dir */
{
  double pos[DIM];
  struct sdis_medium* reinject_mdm;
  struct sXd(hit) reinject_hit;
  float reinject_dir[DIM];
  float reinject_dst;
  res_T res = RES_OK;
  ASSERT(is_valid && out_reinject_dir && out_reinject_dst && out_reinject_hit);

  /* Select a reinjection direction */
  res = XD(select_reinjection_dir)(scn, mdm, rwalk, dir0, dir1, delta,
    reinject_dir, &reinject_dst, can_move, move_pos, &reinject_hit);
  if(res != RES_OK) goto error;

  if(!SXD_HIT_NONE(&reinject_hit)) {
    *is_valid = 1;
  } else {
    /* Check medium consistency at the reinjection position */
    XD(move_pos)(dX(set)(pos, rwalk->vtx.P), reinject_dir, reinject_dst);
    res = scene_get_medium_in_closed_boundaries
      (scn, pos, &reinject_mdm);
    if(res == RES_BAD_OP) { reinject_mdm = NULL; res = RES_OK; }
    if(res != RES_OK) goto error;

    *is_valid = reinject_mdm == mdm;
  }

  if(*is_valid) {
    fX(set)(out_reinject_dir, reinject_dir);
    *out_reinject_dst = reinject_dst;
    *out_reinject_hit = reinject_hit;
  }

exit:
  return res;
error:
  goto exit;
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

static res_T
XD(solid_solid_boundary_path)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
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
  ASSERT(scn && fp_to_meter > 0 && ctx && frag && rwalk && rng && T);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag));
  (void)frag, (void)ctx;

  /* Retrieve the current boundary media */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  solid_front = interface_get_medium(interf, SDIS_FRONT);
  solid_back = interface_get_medium(interf, SDIS_BACK);
  ASSERT(solid_front->type == SDIS_SOLID);
  ASSERT(solid_back->type == SDIS_SOLID);

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

  /* Define the reinjection side. Note that the proba should be :
   *    Lf/Df' / (Lf/Df' + Lb/Db')
   *
   * with L<f|b> the lambda of the <front|back> side and D<f|b>' the adjusted
   * delta of the <front|back> side, i.e. :
   *    D<f|b>' = reinject_dst_<front|back> / sqrt(DIM)
   *
   * Anyway, one can avoid to compute the adjusted delta by directly using the
   * adjusted reinjection distance since the resulting proba is strictly the
   * same; sqrt(DIM) can be simplified. */
  r = ssp_rng_canonical(rng);
  proba = (lambda_front/reinject_dst_front)
    / (lambda_front/reinject_dst_front + lambda_back/reinject_dst_back);
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
    const double delta_in_meter = reinject_dst * fp_to_meter;
    const double lambda = solid_get_thermal_conductivity(mdm, &rwalk->vtx);
    tmp = delta_in_meter * delta_in_meter / (2.0 * DIM * lambda);
    T->value += power * tmp;

    if(ctx->green_path) {
      res = green_path_add_power_term(ctx->green_path, mdm, &rwalk->vtx, tmp);
      if(res != RES_OK) goto error;
    }
  }

  /* Time rewind */
  res = XD(time_rewind)(mdm, rng, reinject_dst, fp_to_meter, ctx, rwalk, T);
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

static res_T
XD(solid_fluid_boundary_path)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
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
  ASSERT(scn && fp_to_meter > 0 && rwalk && rng && T && ctx);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag));

    /* Retrieve the solid and the fluid split by the boundary */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  mdm_front = interface_get_medium(interf, SDIS_FRONT);
  mdm_back = interface_get_medium(interf, SDIS_BACK);
  ASSERT(mdm_front->type != mdm_back->type);

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

  /* Fetch the boundary properties */
  epsilon = interface_side_get_emissivity(interf, &frag_fluid);
  hc = interface_get_convection_coef(interf, frag);

  /* Compute the radiative coefficient */
  hr = 4.0 * BOLTZMANN_CONSTANT * ctx->Tref3 * epsilon;

  /* Compute the probas to switch in solid, fluid or radiative random walk */
  tmp = lambda / (delta*fp_to_meter);
  fluid_proba = hc  / (tmp + hr + hc);
  radia_proba = hr  / (tmp + hr + hc);
  /*solid_proba = tmp / (tmp + hr + hc);*/

  r = ssp_rng_canonical(rng);
  if(r < radia_proba) { /* Switch in radiative random walk */
    T->func = XD(radiative_path);
    rwalk->mdm = fluid;
    rwalk->hit_side = rwalk->mdm == mdm_front ? SDIS_FRONT : SDIS_BACK;
  } else if(r < fluid_proba + radia_proba) { /* Switch to convective random walk */
    T->func = XD(convective_path);
    rwalk->mdm = fluid;
    rwalk->hit_side = rwalk->mdm == mdm_front ? SDIS_FRONT : SDIS_BACK;
  } else { /* Solid random walk */
    /* Handle the volumic power */
    const double power = solid_get_volumic_power(solid, &rwalk->vtx);
    if(power != SDIS_VOLUMIC_POWER_NONE) {
      const double delta_in_meter = reinject_dst * fp_to_meter;
      tmp = delta_in_meter * delta_in_meter / (2.0 * DIM * lambda);
      T->value += power * tmp;

      if(ctx->green_path) {
        res = green_path_add_power_term(ctx->green_path, solid, &rwalk->vtx, tmp);
        if(res != RES_OK) goto error;
      }
    }

    /* Time rewind */
    res = XD(time_rewind)(solid, rng, reinject_dst, fp_to_meter, ctx, rwalk, T);
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
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(solid_boundary_with_flux_path)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
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

  /* Compute the reinjection distance.  It MUST ensure that the orthogonal
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
  delta_in_meter = delta*fp_to_meter;
  tmp = delta_in_meter / lambda;
  T->value += phi * tmp;
  if(ctx->green_path) {
    res = green_path_add_flux_term(ctx->green_path, interf, frag, tmp);
    if(res != RES_OK) goto error;
  }

  /* Handle the volumic power */
  power = solid_get_volumic_power(mdm, &rwalk->vtx);
  if(power != SDIS_VOLUMIC_POWER_NONE) {
    delta_in_meter = reinject_dst * fp_to_meter;
    tmp = delta_in_meter * delta_in_meter / (2.0 * DIM * lambda);
    T->value += power * tmp;
    if(ctx->green_path) {
      res = green_path_add_power_term(ctx->green_path, mdm, &rwalk->vtx, tmp);
      if(res != RES_OK) goto error;
    }
  }

  /* Time rewind */
  res = XD(time_rewind)(mdm, rng, reinject_dst, fp_to_meter, ctx, rwalk, T);
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

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(boundary_path)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* mdm_front = NULL;
  struct sdis_medium* mdm_back = NULL;
  struct sdis_medium* mdm = NULL;
  double tmp;
  res_T res = RES_OK;
  ASSERT(scn && fp_to_meter > 0 && ctx && rwalk && rng && T);
  ASSERT(rwalk->mdm == NULL);
  ASSERT(!SXD_HIT_NONE(&rwalk->hit));

  XD(setup_interface_fragment)(&frag, &rwalk->vtx, &rwalk->hit, rwalk->hit_side);

  fX(normalize)(rwalk->hit.normal, rwalk->hit.normal);

  /* Retrieve the current interface */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);

  /* Check if the boundary temperature is known */
  tmp = interface_side_get_temperature(interf, &frag);
  if(tmp >= 0) {
    T->value += tmp;
    T->done = 1;

    if(ctx->green_path) {
      res = green_path_set_limit_interface_fragment
        (ctx->green_path, interf, &frag);
      if(res != RES_OK) goto error;
    }
    if(ctx->heat_path) {
      heat_path_get_last_vertex(ctx->heat_path)->weight = T->value;
    }
    goto exit;
  }

  /* Check if the boundary flux is known. Note that currently, only solid media
   * can have a flux as limit condition */
  mdm = interface_get_medium(interf, frag.side);
  if(sdis_medium_get_type(mdm) == SDIS_SOLID ) {
    const double phi = interface_side_get_flux(interf, &frag);
    if(phi != SDIS_FLUX_NONE) {
      res = XD(solid_boundary_with_flux_path)
        (scn, fp_to_meter, ctx, &frag, phi, rwalk, rng, T);
      if(res != RES_OK) goto error;

      goto exit;
    }
  }

  mdm_front = interface_get_medium(interf, SDIS_FRONT);
  mdm_back = interface_get_medium(interf, SDIS_BACK);

  if(mdm_front->type == mdm_back->type) {
    res = XD(solid_solid_boundary_path)
      (scn, fp_to_meter, ctx, &frag, rwalk, rng, T);
  } else {
    res = XD(solid_fluid_boundary_path)
      (scn, fp_to_meter, ctx, &frag, rwalk, rng, T);
  }
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"

