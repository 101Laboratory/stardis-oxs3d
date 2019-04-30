/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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
 * the distance to reinject is less than this adjusted value, the solver
 * switches from 2D reinjection scheme to the 1D reinjection scheme in order to
 * avoid numerical issues. */
#define REINJECT_DST_MIN_SCALE 0.125f

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static FINLINE void
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

static FINLINE res_T
XD(select_reinjection_dir)
  (const struct sdis_scene* scn,
   const struct sdis_medium* mdm,
   struct XD(rwalk)* rwalk,
   const float dir0[DIM],
   const float dir1[DIM],
   const double delta,
   float reinject_dir[DIM],
   float* reinject_dst,
   struct sXd(hit)* reinject_hit)
{
  struct sdis_interface* interf;
  struct sdis_medium* mdm0;
  struct sdis_medium* mdm1;
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
  res_T res = RES_OK;
  ASSERT(scn && mdm && rwalk && dir0 && dir1 && delta > 0);
  ASSERT(reinject_dir && reinject_dst && reinject_hit);

  f2(range, 0, FLT_MAX);
  fX_set_dX(org, rwalk->vtx.P);
  SXD(scene_view_trace_ray(scn->sXd(view), org, dir0, range, &rwalk->hit, &hit0));
  SXD(scene_view_trace_ray(scn->sXd(view), org, dir1, range, &rwalk->hit, &hit1));

  /* Retrieve the medium at the reinjection pos along dir0 */
  if(SXD_HIT_NONE(&hit0)) {
    XD(move_pos)(dX(set)(tmp, rwalk->vtx.P), dir0, (float)delta);
    res = scene_get_medium(scn, tmp, NULL, &mdm0);
    if(res != RES_OK) goto error;
  } else {
    interf = scene_get_interface(scn, hit0.prim.prim_id);
    side = fX(dot)(dir0, hit0.normal) < 0 ? SDIS_FRONT : SDIS_BACK;
    mdm0 = interface_get_medium(interf, side);
  }

  /* Retrieve the medium at the reinjection pos along dir1 */
  if(SXD_HIT_NONE(&hit1)) {
    XD(move_pos)(dX(set)(tmp, rwalk->vtx.P), dir1, (float)delta);
    res = scene_get_medium(scn, tmp, NULL, &mdm1);
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

  if(dst0 == -1 && dst1 == -1) { /* No valid reinjection */
    log_err(scn->dev, "%s: no valid reinjection direction at {%g, %g, %g}.\n",
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
    if(dst0 < dst1) {
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
  float* dir;
  float reinject_dst_front, reinject_dst_back;
  float reinject_dst;
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

  /* Sample a reinjection direction and reflect it around the normal. Then
   * reflect them on the back side of the interface. */
  XD(sample_reinjection_dir)(rwalk, rng, dir0);
  XD(reflect)(dir2, dir0, rwalk->hit.normal);
  fX(minus)(dir1, dir0);
  fX(minus)(dir3, dir2);

  /* Select the reinjection direction and distance for the front side */
  res = XD(select_reinjection_dir)(scn, solid_front, rwalk, dir0, dir2,
    delta_boundary_front, dir0, &reinject_dst_front, &hit0);
  if(res != RES_OK) goto error;

  /* Select the reinjection direction and distance for the back side */
  res = XD(select_reinjection_dir)(scn, solid_back, rwalk, dir1, dir3,
    delta_boundary_back, dir1, &reinject_dst_back, &hit1);
  if(res != RES_OK) goto error;

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
    dir = dir0;
    hit = &hit0;
    mdm = solid_front;
    reinject_dst = reinject_dst_front;
  } else { /* Reinject in back */
    dir = dir1;
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

  /* Sample a reinjection direction */
  XD(sample_reinjection_dir)(rwalk, rng, dir0);

  /* Reflect the sampled direction around the normal */
  XD(reflect)(dir1, dir0, rwalk->hit.normal);

  if(solid == mdm_back) {
    fX(minus)(dir0, dir0);
    fX(minus)(dir1, dir1);
  }

  /* Select the solid reinjection direction and distance */
  res = XD(select_reinjection_dir)(scn, solid, rwalk, dir0, dir1,
    delta_boundary, dir0, &reinject_dst, &hit);
  if(res != RES_OK) goto error;

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

  /* Sample a reinjection direction */
  XD(sample_reinjection_dir)(rwalk, rng, dir0);

  /* Reflect the sampled direction around the normal */
  XD(reflect)(dir1, dir0, rwalk->hit.normal);

  if(frag->side == SDIS_BACK) {
    fX(minus)(dir0, dir0);
    fX(minus)(dir1, dir1);
  }

  /* Select the reinjection direction and distance */
  res = XD(select_reinjection_dir)(scn, mdm, rwalk, dir0, dir1,
    delta_boundary, dir0, &reinject_dst, &hit);
  if(res != RES_OK) goto error;

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

