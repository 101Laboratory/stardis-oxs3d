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

#ifndef SDIS_SOLVE_PROBE_DIMENSION
#ifndef SDIS_SOLVE_PROBE_XD_H
#define SDIS_SOLVE_PROBE_XD_H

#include "sdis_device_c.h"
#include "sdis_interface_c.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <rsys/stretchy_array.h>

#include <star/ssp.h>

/* Emperical scale factor to apply to the upper bound of the ray range in order
 * to handle numerical imprecisions */
#define RAY_RANGE_MAX_SCALE 1.0001f

#endif /* SDIS_SOLVE_PROBE_XD_H */
#else

#if (SDIS_SOLVE_PROBE_DIMENSION == 2)
  #include <rsys/double2.h>
  #include <rsys/float2.h>
  #include <star/s2d.h>
#elif (SDIS_SOLVE_PROBE_DIMENSION == 3)
  #include <rsys/double2.h>
  #include <rsys/double3.h>
  #include <rsys/float3.h>
  #include <star/s3d.h>
#else
  #error "Invalid SDIS_SOLVE_PROBE_DIMENSION value."
#endif

/* Syntactic sugar */
#define DIM SDIS_SOLVE_PROBE_DIMENSION

/* Star-XD macros generic to SDIS_SOLVE_PROBE_DIMENSION */
#define sXd(Name) CONCAT(CONCAT(CONCAT(s, DIM), d_), Name)
#define SXD_HIT_NONE CONCAT(CONCAT(S,DIM), D_HIT_NONE)
#define SXD_HIT_NULL CONCAT(CONCAT(S,DIM), D_HIT_NULL)
#define SXD_HIT_NULL__ CONCAT(CONCAT(S, DIM), D_HIT_NULL__)
#define SXD CONCAT(CONCAT(S, DIM), D)

/* Vector macros generic to SDIS_SOLVE_PROBE_DIMENSION */
#define dX(Func) CONCAT(CONCAT(CONCAT(d, DIM), _), Func)
#define fX(Func) CONCAT(CONCAT(CONCAT(f, DIM), _), Func)
#define fX_set_dX CONCAT(CONCAT(CONCAT(f, DIM), _set_d), DIM)
#define dX_set_fX CONCAT(CONCAT(CONCAT(d, DIM), _set_f), DIM)

/* Macro making generic its subimitted name to SDIS_SOLVE_PROBE_DIMENSION */
#define XD(Name) CONCAT(CONCAT(CONCAT(Name, _), DIM), d)

/* Current state of the random walk */
struct XD(rwalk) {
  struct sdis_rwalk_vertex vtx; /* Position and time of the Random walk */
  const struct sdis_medium* mdm; /* Medium in which the random walk lies */
  struct sXd(hit) hit; /* Hit of the random walk */
};
static const struct XD(rwalk) XD(RWALK_NULL) = {
  SDIS_RWALK_VERTEX_NULL__, NULL, SXD_HIT_NULL__
};

struct XD(temperature) {
  res_T (*func)/* Next function to invoke in order to compute the temperature */
    (const struct sdis_scene* scn,
     const double fp_to_meter,
     struct XD(rwalk)* rwalk,
     struct ssp_rng* rng,
     struct XD(temperature)* temp);
  double value; /* Current value of the temperature */
  int done;
};
static const struct XD(temperature) XD(TEMPERATURE_NULL) = { NULL, 0, 0 };

static res_T
XD(boundary_temperature)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T);

static res_T
XD(solid_temperature)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T);

static res_T
XD(fluid_temperature)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T);

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static FINLINE void
XD(move_pos)(double pos[DIM], const float dir[DIM], const float delta)
{
  ASSERT(pos && dir);
  pos[0] += dir[0] * delta;
  pos[1] += dir[1] * delta;
#if (SDIS_SOLVE_PROBE_DIMENSION == 3)
  pos[2] += dir[2] * delta;
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
#if (SDIS_SOLVE_PROBE_DIMENSION == 2)
  uv[0] = rwalk->hit.u;
#else
  d2_set_f2(uv, rwalk->hit.uv);
#endif
  return d2_eq_eps(uv, frag->uv, 1.e-6);
}

res_T
XD(fluid_temperature)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
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
XD(solid_solid_boundary_temperature)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   const struct sdis_interface_fragment* frag,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
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
  float pos[DIM], dir[DIM], range[2];
  ASSERT(scn && fp_to_meter > 0 && frag && rwalk && rng && T);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag));
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
  fX(normalize)(dir, rwalk->hit.normal);
  if(r < proba) { /* Reinject in front */
    delta_boundary = delta_front_boundary;
    rwalk->mdm = solid_front;
  } else { /* Reinject in back */
    delta_boundary = delta_back_boundary;
    fX(minus)(dir, dir);
    rwalk->mdm = solid_back;
  }

  /* "Reinject" the path into the solid along the surface normal. */
  fX_set_dX(pos, rwalk->vtx.P);
  range[0] = 0, range[1] = (float)delta_boundary*RAY_RANGE_MAX_SCALE;
  SXD(scene_view_trace_ray
    (scn->sXd(view), pos, dir, range, &rwalk->hit, &rwalk->hit));
  if(!SXD_HIT_NONE(&rwalk->hit)) delta_boundary = rwalk->hit.distance * 0.5;
  XD(move_pos)(rwalk->vtx.P, dir, (float)delta_boundary);

  /* Switch in solid random walk */
  T->func = XD(solid_temperature);
}

static void
XD(solid_fluid_boundary_temperature)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   const struct sdis_interface_fragment* frag,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
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
  float dir[DIM], pos[DIM], range[2];

  ASSERT(scn && fp_to_meter > 0 && rwalk && rng && T);
  ASSERT(XD(check_rwalk_fragment_consistency)(rwalk, frag));

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
    T->func = XD(fluid_temperature);
  } else { /* Solid random walk */
    rwalk->mdm = solid;
    fX(normalize)(dir, rwalk->hit.normal);
    if(solid == mdm_back) fX(minus)(dir, dir);

    /* "Reinject" the random walk into the solid */
    fX_set_dX(pos, rwalk->vtx.P);
    range[0] = 0, range[1] = (float)delta_boundary*RAY_RANGE_MAX_SCALE;
    SXD(scene_view_trace_ray
      (scn->sXd(view), pos, dir, range, &rwalk->hit, &rwalk->hit));
    if(!SXD_HIT_NONE(&rwalk->hit)) delta_boundary = rwalk->hit.distance * 0.5;
    XD(move_pos)(rwalk->vtx.P, dir, (float)delta_boundary);

    /* Switch in solid random walk */
    T->func = XD(solid_temperature);
  }
}

res_T
XD(boundary_temperature)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  const struct sdis_interface* interf = NULL;
  const struct sdis_medium* mdm_front = NULL;
  const struct sdis_medium* mdm_back = NULL;
  double tmp;
  ASSERT(scn && fp_to_meter > 0 && rwalk && rng && T);
  ASSERT(!SXD_HIT_NONE(&rwalk->hit));

  XD(setup_interface_fragment)(&frag, &rwalk->vtx, &rwalk->hit);

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
    XD(solid_solid_boundary_temperature)(scn, fp_to_meter, &frag, rwalk, rng, T);
  } else {
    XD(solid_fluid_boundary_temperature)(scn, fp_to_meter, &frag, rwalk, rng, T);
  }
  return RES_OK;
}

res_T
XD(solid_temperature)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  double position_start[DIM];
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
  dX(set)(position_start, rwalk->vtx.P);

  do { /* Solid random walk */
    struct sXd(hit) hit0, hit1;
    double lambda; /* Thermal conductivity */
    double rho; /* Volumic mass */
    double cp; /* Calorific capacity */
    double tau, mu;
    double tmp;
    float delta, delta_solid; /* Random walk numerical parameter */
    float range[2];
    float dir0[DIM], dir1[DIM];
    float org[DIM];

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

#if (SDIS_SOLVE_PROBE_DIMENSION == 2)
    /* Sample a direction around 2PI */
    ssp_ran_circle_uniform_float(rng, dir0, NULL);
#else
    /* Sample a direction around 4PI */
    ssp_ran_sphere_uniform_float(rng, dir0, NULL);
#endif

    /* Trace a ray along the sampled direction and its opposite to check if a
     * surface is hit in [0, delta_solid]. */
    fX_set_dX(org, rwalk->vtx.P);
    fX(minus)(dir1, dir0);
    hit0 = hit1 = SXD_HIT_NULL;
    range[0] = 0.f, range[1] = delta_solid*RAY_RANGE_MAX_SCALE;
    SXD(scene_view_trace_ray(scn->sXd(view), org, dir0, range, NULL, &hit0));
    SXD(scene_view_trace_ray(scn->sXd(view), org, dir1, range, NULL, &hit1));

    if(SXD_HIT_NONE(&hit0) && SXD_HIT_NONE(&hit1)) {
      /* Hit nothing: move along dir0 of the original delta */
      delta = delta_solid;
    } else {
      /* Hit something: move along dir0 of the minimum hit distance */
      delta = MMIN(hit0.distance, hit1.distance);
    }

    /* Sample the time */
    mu = (2*DIM*lambda) / (rho*cp*delta*fp_to_meter*delta*fp_to_meter);
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
    rwalk->hit = hit0.distance > delta ? SXD_HIT_NULL : hit0;

    /* Update the random walk position */
    XD(move_pos)(rwalk->vtx.P, dir0, delta);

    /* Fetch the current medium */
    if(SXD_HIT_NONE(&rwalk->hit)) {
      CHK(scene_get_medium(scn, rwalk->vtx.P, &mdm) == RES_OK);
    } else {
      const struct sdis_interface* interf;
      interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
      mdm = interface_get_medium
        (interf,
         fX(dot(rwalk->hit.normal, dir0)) < 0 ? SDIS_FRONT : SDIS_BACK);
    }

    /* Check random walk consistency */
    if(mdm != rwalk->mdm) {
      log_err(scn->dev,
        "%s: inconsistent medium during the solid random walk.\n", FUNC_NAME);
      if(DIM == 2) {
        log_err(scn->dev,
          "  start position: %g %g; current position: %g %g\n",
          SPLIT2(position_start), SPLIT2(rwalk->vtx.P));
      } else {
        log_err(scn->dev,
          "  start position: %g %g %g; current position: %g %g %g\n",
          SPLIT3(position_start), SPLIT3(rwalk->vtx.P));
      }
      return RES_BAD_OP;
    }

  /* Keep going while the solid random walk does not hit an interface */
  } while(SXD_HIT_NONE(&rwalk->hit));

  T->func = XD(boundary_temperature);
  return RES_OK;
}

static res_T
XD(compute_temperature)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
#ifndef NDEBUG
  struct XD(temperature)* stack = NULL;
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

static res_T
XD(probe_realisation)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct sdis_medium* medium,
   const double position[],
   const double time,
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   double* weight)
{
   struct XD(rwalk) rwalk = XD(RWALK_NULL);
   struct XD(temperature) T = XD(TEMPERATURE_NULL);
   res_T res = RES_OK;
   ASSERT(medium && position && fp_to_meter > 0 && weight && time >= 0);

   switch(medium->type) {
     case SDIS_MEDIUM_FLUID: T.func = XD(fluid_temperature); break;
     case SDIS_MEDIUM_SOLID: T.func = XD(solid_temperature); break;
     default: FATAL("Unreachable code\n"); break;
   }

   dX(set)(rwalk.vtx.P, position);
   rwalk.vtx.time = time;
   rwalk.hit = SXD_HIT_NULL;
   rwalk.mdm = medium;

   res = XD(compute_temperature)(scn, fp_to_meter, &rwalk, rng, &T);
   if(res != RES_OK) return res;

   *weight = T.value;
   return RES_OK;
}


#undef SDIS_SOLVE_PROBE_DIMENSION
#undef DIM
#undef sXd
#undef SXD_HIT_NONE
#undef SXD_HIT_NULL
#undef SXD_HIT_NULL__
#undef SXD
#undef dX
#undef fX
#undef fX_set_dX
#undef XD

#endif /* !SDIS_SOLVE_PROBE_DIMENSION */

