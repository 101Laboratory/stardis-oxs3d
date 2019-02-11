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
  struct sXd(hit) hit0, hit1, hit2, hit3;
  struct sXd(hit)* hit;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* solid_front = NULL;
  struct sdis_medium* solid_back = NULL;
  struct sdis_medium* mdm;
  double lambda_front, lambda_back;
  double delta_front, delta_back;
  double delta_boundary_front, delta_boundary_back;
  double delta_boundary;
  double reinject_dst_front, reinject_dst_back;
  double reinject_dst;
  double proba;
  double tmp;
  double r;
  double power;
  float range0[2], range1[2];
  float dir0[DIM], dir1[DIM], dir2[DIM], dir3[DIM];
  float* dir;
  float pos[DIM];
  int dim = DIM;
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
  delta_back  = solid_get_delta(solid_back, &rwalk->vtx);
  delta_boundary_front = delta_front*sqrt(DIM);
  delta_boundary_back  = delta_back *sqrt(DIM);

  /* Sample a reinjection direction and reflect it around the normal. Then
   * reflect them on the back side of the interface. */
  XD(sample_reinjection_dir)(rwalk, rng, dir0);
  XD(reflect)(dir2, dir0, rwalk->hit.normal);
  fX(minus)(dir1, dir0);
  fX(minus)(dir3, dir2);

  /* Trace the sampled directions on both sides of the interface to adjust the
   * reinjection distance of the random walk . */
  fX_set_dX(pos, rwalk->vtx.P);
  f2(range0, 0, (float)delta_boundary_front*RAY_RANGE_MAX_SCALE);
  f2(range1, 0, (float)delta_boundary_back *RAY_RANGE_MAX_SCALE);
  SXD(scene_view_trace_ray(scn->sXd(view), pos, dir0, range0, &rwalk->hit, &hit0));
  SXD(scene_view_trace_ray(scn->sXd(view), pos, dir1, range1, &rwalk->hit, &hit1));
  SXD(scene_view_trace_ray(scn->sXd(view), pos, dir2, range0, &rwalk->hit, &hit2));
  SXD(scene_view_trace_ray(scn->sXd(view), pos, dir3, range1, &rwalk->hit, &hit3));

  /* Adjust the reinjection distance */
  reinject_dst_front = MMIN(MMIN(delta_boundary_front, hit0.distance), hit2.distance);
  reinject_dst_back  = MMIN(MMIN(delta_boundary_back,  hit1.distance), hit3.distance);

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
    delta_boundary = delta_boundary_front;
  } else { /* Reinject in back */
    dir = dir1;
    hit = &hit1;
    mdm = solid_back;
    reinject_dst = reinject_dst_back;
    delta_boundary = delta_boundary_back;
  }

  /* Switch in 1D reinjection scheme */
  if(reinject_dst < delta_boundary * REINJECT_DST_MIN_SCALE) {
    if(dir == dir0) {
      fX(set)(dir, rwalk->hit.normal);
    } else {
      fX(minus)(dir, rwalk->hit.normal);
    }

    f2(range0, 0, (float)delta_boundary*RAY_RANGE_MAX_SCALE);
    SXD(scene_view_trace_ray(scn->sXd(view), pos, dir, range0, &rwalk->hit, hit));
    reinject_dst = MMIN(delta_boundary, hit->distance),
    dim = 1;

    /* Hit something in 1D. Arbitrarily move the random walk to 0.5 of the hit
     * distance */
    if(!SXD_HIT_NONE(hit)) {
      reinject_dst *= 0.5;
      *hit = SXD_HIT_NULL;
    }
  }

  /* Handle the volumic power */
  power = solid_get_volumic_power(mdm, &rwalk->vtx);
  if(power != SDIS_VOLUMIC_POWER_NONE) {
    const double delta_in_meter = reinject_dst * fp_to_meter;
    const double lambda = solid_get_thermal_conductivity(mdm, &rwalk->vtx);
    tmp = delta_in_meter * delta_in_meter / (2.0 * dim * lambda);
    T->value += power * tmp;

    if(ctx->green_path) {
      res = green_path_add_power_term(ctx->green_path, mdm, tmp);
      if(res != RES_OK) goto error;
    }
  }

  /* Reinject */
  XD(move_pos)(rwalk->vtx.P, dir, (float)reinject_dst);
  if(eq_epsf(hit->distance, (float)reinject_dst, 1.e-4f)) {
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
  struct sXd(hit) hit0 = SXD_HIT_NULL;
  struct sXd(hit) hit1 = SXD_HIT_NULL;
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
  float pos[DIM];
  float dir0[DIM], dir1[DIM];
  float range[2];
  int dim = DIM;
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

  /* Trace dir0/dir1 to adjust the reinjection distance */
  fX_set_dX(pos, rwalk->vtx.P);
  f2(range, 0, (float)delta_boundary*RAY_RANGE_MAX_SCALE);
  SXD(scene_view_trace_ray(scn->sXd(view), pos, dir0, range, &rwalk->hit, &hit0));
  SXD(scene_view_trace_ray(scn->sXd(view), pos, dir1, range, &rwalk->hit, &hit1));

  /* Adjust the delta boundary to the hit distance */
  tmp = MMIN(MMIN(delta_boundary, hit0.distance), hit1.distance);

  if(tmp >= delta_boundary * REINJECT_DST_MIN_SCALE) {
    delta_boundary = tmp;
    /* Define the orthogonal dst from the reinjection pos to the interface */
    delta = delta_boundary / sqrt(DIM);
  } else { /* Switch in 1D reinjection scheme. */
    fX(set)(dir0, rwalk->hit.normal);
    if(solid == mdm_back) fX(minus)(dir0, dir0);
    f2(range, 0, (float)delta*RAY_RANGE_MAX_SCALE);
    SXD(scene_view_trace_ray(scn->sXd(view), pos, dir0, range, &rwalk->hit, &hit0));
    delta_boundary = MMIN(hit0.distance, delta);

    /* Hit something in 1D. Arbitrarily move the random walk to 0.5 of the hit
     * distance in order to avoid infinite bounces for parallel plane */
    if(!SXD_HIT_NONE(&hit0)) {
      delta_boundary *= 0.5;
      hit0 = SXD_HIT_NULL;
    }

    delta = delta_boundary;
    dim = 1;
  }

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
      const double delta_in_meter = delta_boundary * fp_to_meter;
      tmp = delta_in_meter * delta_in_meter / (2.0 * dim * lambda);
      T->value += power * tmp;

      if(ctx->green_path) {
        res = green_path_add_power_term(ctx->green_path, solid, tmp);
        if(res != RES_OK) goto error;
      }
    }

    /* Reinject */
    XD(move_pos)(rwalk->vtx.P, dir0, (float)delta_boundary);
    if(eq_epsf(hit0.distance, (float)delta_boundary, 1.e-4f)) {
      T->func = XD(boundary_path);
      rwalk->mdm = NULL;
      rwalk->hit = hit0;
      rwalk->hit_side = fX(dot)(hit0.normal, dir0) < 0 ? SDIS_FRONT : SDIS_BACK;
    } else {
      T->func = XD(conductive_path);
      rwalk->mdm = solid;
      rwalk->hit = SXD_HIT_NULL;
      rwalk->hit_side = SDIS_SIDE_NULL__;
    }
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
  struct sXd(hit) hit0;
  struct sXd(hit) hit1;
  float pos[DIM];
  float dir0[DIM];
  float dir1[DIM];
  float range[2];
  int dim = DIM;
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

  /* Trace dir0/dir1 to adjust the reinjection distance wrt the geometry */
  fX_set_dX(pos, rwalk->vtx.P);
  f2(range, 0, (float)delta_boundary*RAY_RANGE_MAX_SCALE);
  SXD(scene_view_trace_ray(scn->sXd(view), pos, dir0, range, &rwalk->hit, &hit0));
  SXD(scene_view_trace_ray(scn->sXd(view), pos, dir1, range, &rwalk->hit, &hit1));

  /* Adjust the delta boundary to the hit distance */
  tmp = MMIN(MMIN(delta_boundary, hit0.distance), hit1.distance);

  if(tmp >= delta_boundary * REINJECT_DST_MIN_SCALE) {
    delta_boundary = tmp;
    /* Define the orthogonal dst from the reinjection pos to the interface */
    delta = delta_boundary / sqrt(DIM);
  } else { /* Switch in 1D reinjection scheme. */
    fX(set)(dir0, rwalk->hit.normal);
    if(frag->side == SDIS_BACK) fX(minus)(dir0, dir0);
    f2(range, 0, (float)delta*RAY_RANGE_MAX_SCALE);
    SXD(scene_view_trace_ray(scn->sXd(view), pos, dir0, range, &rwalk->hit, &hit0));
    delta_boundary = MMIN(hit0.distance, delta_boundary);

    /* Hit something in 1D. Arbitrarily move the random walk to 0.5 of the hit
     * distance in order to avoid infinite bounces for parallel plane */
    if(!SXD_HIT_NONE(&hit0)) {
      delta_boundary *= 0.5;
      hit0 = SXD_HIT_NULL;
    }

    delta = delta_boundary;
    dim = 1;
  }

  /* Handle the flux */
  delta_in_meter = delta*fp_to_meter;
  tmp = delta_in_meter / lambda;
  T->value += phi * tmp;
  if(ctx->green_path) {
    res = green_path_add_flux_term(ctx->green_path, interf, tmp);
    if(res != RES_OK) goto error;
  }

  /* Handle the volumic power */
  power = solid_get_volumic_power(mdm, &rwalk->vtx);
  if(power != SDIS_VOLUMIC_POWER_NONE) {
    delta_in_meter = delta_boundary * fp_to_meter;
    tmp = delta_in_meter * delta_in_meter / (2.0 * dim * lambda);
    T->value += power * tmp;
    if(ctx->green_path) {
      res = green_path_add_power_term(ctx->green_path, mdm, tmp);
      if(res != RES_OK) goto error;
    }
  }

  /* Reinject into the solid */
  XD(move_pos)(rwalk->vtx.P, dir0, (float)delta_boundary);
  if(eq_epsf(hit0.distance, (float)delta_boundary, 1.e-4f)) {
    T->func = XD(boundary_path);
    rwalk->mdm = NULL;
    rwalk->hit = hit0;
    rwalk->hit_side = fX(dot)(hit0.normal, dir0) < 0 ? SDIS_FRONT : SDIS_BACK;
  } else {
    T->func = XD(conductive_path);
    rwalk->mdm = mdm;
    rwalk->hit = SXD_HIT_NULL;
    rwalk->hit_side = SDIS_SIDE_NULL__;
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
      double pos[3] = {0,0,0};
      dX(set)(pos, rwalk->vtx.P);
      res = green_path_set_interface_limit_vertex
        (ctx->green_path, interf, pos, rwalk->vtx.time);
      if(res != RES_OK) goto error;
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

