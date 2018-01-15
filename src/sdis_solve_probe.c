/* Copyright (C) |Meso|Star> 2016-2018 (contact@meso-star.com)
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

#include "sdis.h"
#include "sdis_device_c.h"
#include "sdis_estimator_c.h"
#include "sdis_interface_c.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/float3.h>
#include <rsys/stretchy_array.h>

#include <star/s3d.h>
#include <star/ssp.h>

/* Emperical scale factor to apply to the upper bound of the ray range in order
 * to handle numerical imprecisions */
#define RAY_RANGE_MAX_SCALE 1.0001f

/* Current state of the random walk */
struct rwalk {
  struct sdis_rwalk_vertex vtx; /* Position and time of the Random walk */
  const struct sdis_medium* mdm; /* Medium in which the random walk lies */
  struct s3d_hit hit; /* Hit of the random walk */
};
static const struct rwalk RWALK_NULL = {
  SDIS_RWALK_VERTEX_NULL__, NULL, S3D_HIT_NULL__
};

struct temperature {
  res_T (*func)/* Next function to invoke in order to compute the temperature */
    (const struct sdis_scene* scn,
     const double fp_to_meter,
     struct rwalk* rwalk,
     struct ssp_rng* rng,
     struct temperature* temp);
  double value; /* Current value of the temperature */
  int done;
};
static const struct temperature TEMPERATURE_NULL = { NULL, 0, 0 };

static res_T
boundary_temperature
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

static res_T
solid_temperature
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

static res_T
fluid_temperature
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static FINLINE void
move_pos(double pos[3], const float dir[3], const float delta)
{
  ASSERT(pos && dir);
  pos[0] += dir[0] * delta;
  pos[1] += dir[1] * delta;
  pos[2] += dir[2] * delta;
}

/* Check that the interface fragment is consistent with the current state of
 * the random walk */
static INLINE int
check_rwalk_fragment_consistency
  (const struct rwalk* rwalk,
   const struct sdis_interface_fragment* frag)
{
  double N[3];
  double uv[2];
  ASSERT(rwalk && frag);
  d3_normalize(N, d3_set_f3(N, rwalk->hit.normal));
  d2_set_f2(uv, rwalk->hit.uv);
  return !S3D_HIT_NONE(&rwalk->hit)
      && d3_eq_eps(rwalk->vtx.P, frag->P, 1.e-6)
      && d3_eq_eps(N, frag->Ng, 1.e-6)
      && d2_eq_eps(uv, frag->uv, 1.e-6)
      && ( (IS_INF(rwalk->vtx.time) && IS_INF(frag->time))
         || eq_eps(rwalk->vtx.time, frag->time,  1.e-6));
}

res_T
fluid_temperature
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  double tmp;
  (void)rng, (void)fp_to_meter;
  ASSERT(scn && fp_to_meter > 0 && rwalk && rng && T);
  ASSERT(rwalk->mdm->type == SDIS_MEDIUM_FLUID);

  tmp = fluid_get_temperature(rwalk->mdm, &rwalk->vtx);
  if(tmp < 0) {
    log_err(scn->dev, "%s: unknown fluid temperature is not supported.\n",
      FUNC_NAME);
    return RES_BAD_OP;
  }
  T->value += tmp;
  T->done = 1;
  return RES_OK;
}

static void
solid_solid_boundary_temperature
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  const struct sdis_interface* interf = NULL;
  const struct sdis_medium* solid_front = NULL;
  const struct sdis_medium* solid_back = NULL;
  double lambda_front, lambda_back;
  double delta_front_boundary, delta_back_boundary;
  double delta_front_boundary_meter, delta_back_boundary_meter;
  double delta_boundary;
  double proba;
  double tmp;
  double r;
  float pos[3], dir[3], range[2];
  ASSERT(scn && fp_to_meter > 0 && frag && rwalk && rng && T);
  ASSERT(check_rwalk_fragment_consistency(rwalk, frag));
  (void)frag;

  /* Retrieve the current boundary media */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  solid_front = interface_get_medium(interf, SDIS_FRONT);
  solid_back = interface_get_medium(interf, SDIS_BACK);
  ASSERT(solid_front->type == SDIS_MEDIUM_SOLID);
  ASSERT(solid_back->type == SDIS_MEDIUM_SOLID);

  /* Fetch the properties of the media */
  lambda_front = solid_get_thermal_conductivity(solid_front, &rwalk->vtx);
  lambda_back = solid_get_thermal_conductivity(solid_back, &rwalk->vtx);
  delta_front_boundary = solid_get_delta_boundary(solid_front, &rwalk->vtx);
  delta_back_boundary = solid_get_delta_boundary(solid_back, &rwalk->vtx);

  /* Convert the delta boundary from floating point units to meters */
  delta_front_boundary_meter = fp_to_meter * delta_front_boundary;
  delta_back_boundary_meter = fp_to_meter * delta_back_boundary;

  /* Compute the proba to switch in solid0 or in solid1 */
  tmp = lambda_front / delta_front_boundary_meter;
  proba = tmp / (tmp + lambda_back / delta_back_boundary_meter);

  r = ssp_rng_canonical(rng);
  f3_normalize(dir, rwalk->hit.normal);
  if(r < proba) { /* Reinject in front */
    delta_boundary = delta_front_boundary;
    rwalk->mdm = solid_front;
  } else { /* Reinject in back */
    delta_boundary = delta_back_boundary;
    f3_minus(dir, dir);
    rwalk->mdm = solid_back;
  }

  /* "Reinject" the path into the solid along the surface normal. */
  f3_set_d3(pos, rwalk->vtx.P);
  range[0] = 0, range[1] = (float)delta_boundary*RAY_RANGE_MAX_SCALE;
  S3D(scene_view_trace_ray
    (scn->s3d_view, pos, dir, range, &rwalk->hit, &rwalk->hit));
  if(!S3D_HIT_NONE(&rwalk->hit)) delta_boundary = rwalk->hit.distance * 0.5;
  move_pos(rwalk->vtx.P, dir, (float)delta_boundary);

  /* Switch in solid random walk */
  T->func = solid_temperature;
}

static void
solid_fluid_boundary_temperature
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  const struct sdis_interface* interf = NULL;
  const struct sdis_medium* mdm_front = NULL;
  const struct sdis_medium* mdm_back = NULL;
  const struct sdis_medium* solid = NULL;
  const struct sdis_medium* fluid = NULL;
  double hc;
  double lambda;
  double fluid_proba;
  double delta_boundary;
  double r;
  double tmp;
  float dir[3], pos[3], range[2];

  ASSERT(scn && fp_to_meter > 0 && rwalk && rng && T);
  ASSERT(check_rwalk_fragment_consistency(rwalk, frag));

  /* Retrieve the solid and the fluid split by the boundary */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  mdm_front = interface_get_medium(interf, SDIS_FRONT);
  mdm_back = interface_get_medium(interf, SDIS_BACK);
  ASSERT(mdm_front->type != mdm_back->type);
  if(mdm_front->type == SDIS_MEDIUM_SOLID) {
    solid = mdm_front;
    fluid = mdm_back;
  } else {
    solid = mdm_back;
    fluid = mdm_front;
  }

  /* Fetch the solid properties */
  lambda = solid_get_thermal_conductivity(solid, &rwalk->vtx);
  delta_boundary = solid_get_delta_boundary(solid, &rwalk->vtx);
  hc = interface_get_convection_coef(interf, frag);

  /* Compute the probas to switch in solid or fluid random walk */
  tmp = lambda / (delta_boundary*fp_to_meter);
  fluid_proba = hc / (tmp + hc);

  r = ssp_rng_canonical(rng);
  if(r < fluid_proba) { /* Switch to fluid random walk */
    rwalk->mdm = fluid;
    T->func = fluid_temperature;
  } else { /* Solid random walk */
    rwalk->mdm = solid;
    f3_normalize(dir, rwalk->hit.normal);
    if(solid == mdm_back) f3_minus(dir, dir);

    /* "Reinject" the random walk into the solid */
    f3_set_d3(pos, rwalk->vtx.P);
    range[0] = 0, range[1] = (float)delta_boundary*RAY_RANGE_MAX_SCALE;
    S3D(scene_view_trace_ray
      (scn->s3d_view, pos, dir, range, &rwalk->hit, &rwalk->hit));
    if(!S3D_HIT_NONE(&rwalk->hit)) delta_boundary = rwalk->hit.distance * 0.5;
    move_pos(rwalk->vtx.P, dir, (float)delta_boundary);

    /* Switch in solid random walk */
    T->func = solid_temperature;
  }
}

res_T
boundary_temperature
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  const struct sdis_interface* interf = NULL;
  const struct sdis_medium* mdm_front = NULL;
  const struct sdis_medium* mdm_back = NULL;
  double tmp;
  ASSERT(scn && fp_to_meter > 0 && rwalk && rng && T);
  ASSERT(!S3D_HIT_NONE(&rwalk->hit));

  setup_interface_fragment(&frag, &rwalk->vtx, &rwalk->hit);

  /* Retrieve the current interface */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);

  /* Check if the boundary condition is known */
  tmp = interface_get_temperature(interf, &frag);
  if(tmp >= 0) {
    T->value += tmp;
    T->done = 1;
    return RES_OK;
  }

  mdm_front = interface_get_medium(interf, SDIS_FRONT);
  mdm_back = interface_get_medium(interf, SDIS_BACK);

  if(mdm_front->type == mdm_back->type) {
    solid_solid_boundary_temperature(scn, fp_to_meter, &frag, rwalk, rng, T);
  } else {
    solid_fluid_boundary_temperature(scn, fp_to_meter, &frag, rwalk, rng, T);
  }
  return RES_OK;
}

res_T
solid_temperature
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  double position_start[3];
  const struct sdis_medium* mdm;
  ASSERT(scn && fp_to_meter > 0 && rwalk && rng && T);
  ASSERT(rwalk->mdm->type == SDIS_MEDIUM_SOLID);

  /* Check the random walk consistency */
  CHK(scene_get_medium(scn, rwalk->vtx.P, &mdm) == RES_OK);
  if(mdm != rwalk->mdm) {
    log_err(scn->dev, "%s: invalid solid random walk. "
      "Unexpected medium at {%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(rwalk->vtx.P));
    return RES_BAD_ARG;
  }
  /* Save the submitted position */
  d3_set(position_start, rwalk->vtx.P);

  do { /* Solid random walk */
    struct s3d_hit hit0, hit1;
    double lambda; /* Thermal conductivity */
    double rho; /* Volumic mass */
    double cp; /* Calorific capacity */
    double tau, mu;
    double tmp;
    float delta, delta_solid; /* Random walk numerical parameter */
    float range[2];
    float dir0[3], dir1[3];
    float org[3];

    /* Check the limit condition */
    tmp = solid_get_temperature(mdm, &rwalk->vtx);
    if(tmp >= 0) {
      T->value += tmp;
      T->done = 1;
      return RES_OK;
    }

    /* Fetch solid properties */
    delta_solid = (float)solid_get_delta(mdm, &rwalk->vtx);
    lambda = solid_get_thermal_conductivity(mdm, &rwalk->vtx);
    rho = solid_get_volumic_mass(mdm, &rwalk->vtx);
    cp = solid_get_calorific_capacity(mdm, &rwalk->vtx);

    /* Sample a direction around 4PI */
    ssp_ran_sphere_uniform_float(rng, dir0, NULL);

    /* Trace a ray along the sampled direction and its opposite to check if a
     * surface is hit in [0, delta_solid]. */
    f3_set_d3(org, rwalk->vtx.P);
    f3_minus(dir1, dir0);
    hit0 = hit1 = S3D_HIT_NULL;
    range[0] = 0.f, range[1] = delta_solid*RAY_RANGE_MAX_SCALE;
    S3D(scene_view_trace_ray(scn->s3d_view, org, dir0, range, NULL, &hit0));
    S3D(scene_view_trace_ray(scn->s3d_view, org, dir1, range, NULL, &hit1));

    if(S3D_HIT_NONE(&hit0) && S3D_HIT_NONE(&hit1)) {
      /* Hit nothing: move along dir0 of the original delta */
      delta = delta_solid;
    } else {
      /* Hit something: move along dir0 of the minimum hit distance */
      delta = MMIN(hit0.distance, hit1.distance);
    }

    /* Sample the time */
    mu = (6 * lambda) / (rho * cp * delta * fp_to_meter * delta * fp_to_meter );
    tau = ssp_ran_exp(rng, mu);
    rwalk->vtx.time -= tau;

    /* Check the initial condition */
    tmp = solid_get_temperature(mdm, &rwalk->vtx);
    if(tmp >= 0) {
      T->value += tmp;
      T->done = 1;
      return RES_OK;
    }

    /* Define if the random walk hits something along dir0 */
    rwalk->hit = hit0.distance > delta ? S3D_HIT_NULL : hit0;

    /* Update the random walk position */
    move_pos(rwalk->vtx.P, dir0, delta);

    /* Fetch the current medium */
    if(S3D_HIT_NONE(&rwalk->hit)) {
      CHK(scene_get_medium(scn, rwalk->vtx.P, &mdm) == RES_OK);
    } else {
      const struct sdis_interface* interf;
      interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
      mdm = interface_get_medium
        (interf,
         f3_dot(rwalk->hit.normal, dir0) < 0 ? SDIS_FRONT : SDIS_BACK);
    }

    /* Check random walk consistency */
    if(mdm != rwalk->mdm) {
      log_err(scn->dev,
        "%s: inconsistent medium during the solid random walk.\n"
        "  start position: %g %g %g; current position: %g %g %g\n",
        FUNC_NAME, SPLIT3(position_start), SPLIT3(rwalk->vtx.P));
      return RES_BAD_OP;
    }

  /* Keep going while the solid random walk does not hit an interface */
  } while(S3D_HIT_NONE(&rwalk->hit));

  T->func = boundary_temperature;
  return RES_OK;
}

static res_T
compute_temperature
  (struct sdis_scene* scn,
   const double fp_to_meter,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
#ifndef NDEBUG
  struct temperature* stack = NULL;
  size_t istack = 0;
#endif
  res_T res = RES_OK;
  ASSERT(scn && fp_to_meter && rwalk && rng && T);

  do {
    res = T->func(scn, fp_to_meter, rwalk, rng, T);
    if(res != RES_OK) goto error;

#ifndef NDEBUG
    sa_push(stack, *T);
    ++istack;
#endif
  } while(!T->done);

exit:
#ifndef NDEBUG
  sa_release(stack);
#endif
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_solve_probe
  (struct sdis_scene* scn,
   const size_t nrealisations,
   const double position[3],
   const double time,
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   struct sdis_estimator** out_estimator)
{
  const struct sdis_medium* medium = NULL;
  struct sdis_estimator* estimator = NULL;
  struct ssp_rng* rng = NULL;
  double weight = 0;
  double sqr_weight = 0;
  size_t irealisation = 0;
  res_T res = RES_OK;

  if(!scn || !nrealisations || !position || time < 0 || fp_to_meter <= 0
  || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(scene_is_2d(scn)) {
    log_err(scn->dev, "%s: 2D scene are not supported yet.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  res = scene_get_medium(scn, position, &medium);
  if(res != RES_OK) goto error;

  res = ssp_rng_create(scn->dev->allocator, &ssp_rng_mt19937_64, &rng);
  if(res != RES_OK) goto error;
  res = estimator_create(scn->dev, &estimator);
  if(res != RES_OK) goto error;

  FOR_EACH(irealisation, 0, nrealisations) {
    struct rwalk rwalk = RWALK_NULL;
    struct temperature T = TEMPERATURE_NULL;

    switch(medium->type) {
      case SDIS_MEDIUM_FLUID: T.func = fluid_temperature; break;
      case SDIS_MEDIUM_SOLID: T.func = solid_temperature; break;
      default: FATAL("Unreachable code\n"); break;
    }

    rwalk.vtx.P[0] = position[0];
    rwalk.vtx.P[1] = position[1];
    rwalk.vtx.P[2] = position[2];
    rwalk.vtx.time = time;
    rwalk.hit = S3D_HIT_NULL;
    rwalk.mdm = medium;

    res = compute_temperature(scn, fp_to_meter, &rwalk, rng, &T);
    if(res != RES_OK) {
      if(res == RES_BAD_OP) {
        ++estimator->nfailures;
      } else {
        goto error;
      }
    } else {
      weight += T.value;
      sqr_weight += T.value*T.value;
      ++estimator->nrealisations;
    }
  }

  estimator->temperature.E = weight / (double)estimator->nrealisations;
  estimator->temperature.V =
    sqr_weight / (double)estimator->nrealisations
  - estimator->temperature.E * estimator->temperature.E;
  estimator->temperature.SE =
    sqrt(estimator->temperature.V / (double)estimator->nrealisations);

exit:
  if(rng) SSP(rng_ref_put(rng));
  if(out_estimator) *out_estimator = estimator;
  return res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}
