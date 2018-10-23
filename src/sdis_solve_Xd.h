/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_SOLVE_DIMENSION
#ifndef SDIS_SOLVE_XD_H
#define SDIS_SOLVE_XD_H

#include "sdis_device_c.h"
#include "sdis_interface_c.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/stretchy_array.h>

#include <star/ssp.h>
#include <omp.h>

/* Define a new result code from RES_BAD_OP saying that the bad operation is
 * definitive, i.e. in the current state, the realisation will inevitably fail.
 * It is thus unecessary to retry a specific section of the random walk */
#define RES_BAD_OP_IRRECOVERABLE (-RES_BAD_OP)

/* Empirical scale factor to apply to the upper bound of the ray range in order
 * to handle numerical imprecisions */
#define RAY_RANGE_MAX_SCALE 1.001f

/* Emperical scale factor applied to the challenged reinjection distance. If
 * the distance to reinject is less than this adjusted value, the solver
 * switches from 2D reinjection scheme to the 1D reinjection scheme in order to
 * avoid numerical issues. */
#define REINJECT_DST_MIN_SCALE 0.125f

#define BOLTZMANN_CONSTANT 5.6696e-8 /* W/m^2/K^4 */

struct rwalk_context {
  double Tarad; /* Ambient radiative temperature */
  double Tref3; /* Reference temperature ^ 3 */
};

/* Reflect the vector V wrt the normal N. By convention V points outward the
 * surface. */
static INLINE float*
reflect_2d(float res[2], const float V[2], const float N[2])
{
  float tmp[2];
  float cos_V_N;
  ASSERT(res && V && N);
  ASSERT(f2_is_normalized(V) && f2_is_normalized(N));
  cos_V_N = f2_dot(V, N);
  f2_mulf(tmp, N, 2*cos_V_N);
  f2_sub(res, tmp, V);
  return res;
}

/* Reflect the vector V wrt the normal N. By convention V points outward the
 * surface. */
static INLINE float*
reflect_3d(float res[3], const float V[3], const float N[3])
{
  float tmp[3];
  float cos_V_N;
  ASSERT(res && V && N);
  ASSERT(f3_is_normalized(V) && f3_is_normalized(N));
  cos_V_N = f3_dot(V, N);
  f3_mulf(tmp, N, 2*cos_V_N);
  f3_sub(res, tmp, V);
  return res;
}

static INLINE void
setup_estimator
  (struct sdis_estimator* estimator,
   const size_t nrealisations,
   const size_t nsuccesses,
   const double accum_weights,
   const double accum_sqr_weights)
{
  ASSERT(estimator && nrealisations && nsuccesses);
  estimator->nrealisations = nsuccesses;
  estimator->nfailures = nrealisations - nsuccesses;
  estimator->temperature.E = accum_weights / (double)nsuccesses;
  estimator->temperature.V =
    accum_sqr_weights / (double)nsuccesses
  - estimator->temperature.E * estimator->temperature.E;
  estimator->temperature.V = MMAX(estimator->temperature.V, 0);
  estimator->temperature.SE =
    sqrt(estimator->temperature.V / (double)nsuccesses);
}

static INLINE void
setup_estimator_flux
  (struct sdis_estimator* estimator,
   const enum flux_names name,
   const double accum_weights,
   const double accum_sqr_weights)
{
  ASSERT(estimator && (unsigned)name < FLUX_NAMES_COUNT__ && estimator->fluxes);
  ASSERT(estimator->nrealisations);
  estimator->fluxes[name].E = accum_weights / (double)estimator->nrealisations;
  estimator->fluxes[name].V =
    accum_sqr_weights / (double)estimator->nrealisations
    - estimator->fluxes[name].E * estimator->fluxes[name].E;
  estimator->fluxes[name].V = MMAX(estimator->fluxes[name].V, 0);
  estimator->fluxes[name].SE =
    sqrt(estimator->fluxes[name].V / (double)estimator->nrealisations);
}

#endif /* SDIS_SOLVE_XD_H */
#else

#if (SDIS_SOLVE_DIMENSION == 2)
  #include <rsys/double2.h>
  #include <rsys/float2.h>
  #include <star/s2d.h>
#elif (SDIS_SOLVE_DIMENSION == 3)
  #include <rsys/double2.h>
  #include <rsys/double3.h>
  #include <rsys/float3.h>
  #include <star/s3d.h>
#else
  #error "Invalid SDIS_SOLVE_DIMENSION value."
#endif

/* Syntactic sugar */
#define DIM SDIS_SOLVE_DIMENSION

/* Star-XD macros generic to SDIS_SOLVE_DIMENSION */
#define sXd(Name) CONCAT(CONCAT(CONCAT(s, DIM), d_), Name)
#define SXD_HIT_NONE CONCAT(CONCAT(S,DIM), D_HIT_NONE)
#define SXD_HIT_NULL CONCAT(CONCAT(S,DIM), D_HIT_NULL)
#define SXD_HIT_NULL__ CONCAT(CONCAT(S, DIM), D_HIT_NULL__)
#define SXD_POSITION CONCAT(CONCAT(S, DIM), D_POSITION)
#define SXD_GEOMETRY_NORMAL CONCAT(CONCAT(S, DIM), D_GEOMETRY_NORMAL)
#define SXD_GEOMETRY_NORMAL CONCAT(CONCAT(S, DIM), D_GEOMETRY_NORMAL)
#define SXD_VERTEX_DATA_NULL CONCAT(CONCAT(S, DIM), D_VERTEX_DATA_NULL)
#define SXD CONCAT(CONCAT(S, DIM), D)
#define SXD_FLOAT2 CONCAT(CONCAT(S, DIM), D_FLOAT2)
#define SXD_FLOAT3 CONCAT(CONCAT(S, DIM), D_FLOAT3)
#define SXD_SAMPLE CONCAT(CONCAT(S, DIM), D_SAMPLE)
#define sXd_dev CONCAT(CONCAT(s, DIM), d)

/* Vector macros generic to SDIS_SOLVE_DIMENSION */
#define dX(Func) CONCAT(CONCAT(CONCAT(d, DIM), _), Func)
#define fX(Func) CONCAT(CONCAT(CONCAT(f, DIM), _), Func)
#define fX_set_dX CONCAT(CONCAT(CONCAT(f, DIM), _set_d), DIM)
#define dX_set_fX CONCAT(CONCAT(CONCAT(d, DIM), _set_f), DIM)

/* Macro making generic its subimitted name to SDIS_SOLVE_DIMENSION */
#define XD(Name) CONCAT(CONCAT(CONCAT(Name, _), DIM), d)

/* Current state of the random walk */
struct XD(rwalk) {
  struct sdis_rwalk_vertex vtx; /* Position and time of the Random walk */
  const struct sdis_medium* mdm; /* Medium in which the random walk lies */
  struct sXd(hit) hit; /* Hit of the random walk */
  enum sdis_side hit_side;
};
static const struct XD(rwalk) XD(RWALK_NULL) = {
  SDIS_RWALK_VERTEX_NULL__, NULL, SXD_HIT_NULL__, SDIS_SIDE_NULL__
};

struct XD(boundary_context) {
  struct sXd(scene_view)* view;
  const size_t* primitives;
};
static const struct XD(boundary_context) XD(BOUNDARY_CONTEXT_NULL) = {
  NULL, NULL
};

struct XD(temperature) {
  res_T (*func)/* Next function to invoke in order to compute the temperature */
    (struct sdis_scene* scn,
     const double fp_to_meter,
     const struct rwalk_context* ctx,
     struct XD(rwalk)* rwalk,
     struct ssp_rng* rng,
     struct XD(temperature)* temp);
  double value; /* Current value of the temperature */
  int done;
};
static const struct XD(temperature) XD(TEMPERATURE_NULL) = { NULL, 0, 0 };

static res_T
XD(boundary_temperature)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T);

static res_T
XD(solid_temperature)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T);

static res_T
XD(fluid_temperature)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T);

static res_T
XD(radiative_temperature)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T);

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
#ifndef SAMPLE_TIME_DEFINED
static INLINE double
sample_time
  (const double time_range[2],
   struct ssp_rng* rng)
{
  ASSERT(time_range && time_range[0] >= 0 && time_range[1] >= time_range[0]
    && rng);
  if(time_range[0] == time_range[1]) return time_range[0];
  return ssp_rng_uniform_double(rng, time_range[0], time_range[1]);
}
#define SAMPLE_TIME_DEFINED
#endif

static INLINE void
XD(boundary_get_indices)(const unsigned iprim, unsigned ids[DIM], void* context)
{
  unsigned i;
  (void)context;
  ASSERT(ids);
  FOR_EACH(i, 0, DIM) ids[i] = iprim*DIM + i;
}

static INLINE void
XD(boundary_get_position)(const unsigned ivert, float pos[DIM], void* context)
{
  struct XD(boundary_context)* ctx = context;
  struct sXd(primitive) prim;
  struct sXd(attrib) attr;
  const unsigned iprim_id = ivert / DIM;
  const unsigned iprim_vert = ivert % DIM;
  unsigned iprim;
  ASSERT(pos && context);

  iprim = (unsigned)ctx->primitives[iprim_id];
  SXD(scene_view_get_primitive(ctx->view, iprim, &prim));
#if DIM == 2
  s2d_segment_get_vertex_attrib(&prim, iprim_vert, S2D_POSITION, &attr);
  ASSERT(attr.type == S2D_FLOAT2);
#else
  s3d_triangle_get_vertex_attrib(&prim, iprim_vert, S3D_POSITION, &attr);
  ASSERT(attr.type == S3D_FLOAT3);
#endif
  fX(set)(pos, attr.value);
}

static FINLINE void
XD(move_pos)(double pos[DIM], const float dir[DIM], const float delta)
{
  ASSERT(pos && dir);
  pos[0] += dir[0] * delta;
  pos[1] += dir[1] * delta;
#if(SDIS_SOLVE_DIMENSION == 3)
  pos[2] += dir[2] * delta;
#endif
}

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
#if (SDIS_SOLVE_DIMENSION == 2)
  uv[0] = rwalk->hit.u;
#else
  d2_set_f2(uv, rwalk->hit.uv);
#endif
  return d2_eq_eps(uv, frag->uv, 1.e-6);
}

static res_T
XD(trace_radiative_path)
  (struct sdis_scene* scn,
   const float ray_dir[3],
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  /* The radiative random walk is always perform in 3D. In 2D, the geometry are
   * assumed to be extruded to the infinty along the Z dimension. */
  float N[3] = {0, 0, 0};
  float dir[3] = {0, 0, 0};
  res_T res = RES_OK;

  ASSERT(scn && ray_dir && fp_to_meter > 0 && ctx && rwalk && rng && T);
  (void)fp_to_meter;

  f3_set(dir, ray_dir);

  /* Launch the radiative random walk */
  for(;;) {
    const struct sdis_interface* interf = NULL;
    struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
    const struct sdis_medium* chk_mdm = NULL;
    double alpha;
    double epsilon;
    double r;
    float pos[DIM];
    const float range[2] = { 0, FLT_MAX };

    fX_set_dX(pos, rwalk->vtx.P);

    /* Trace the radiative ray */
#if (SDIS_SOLVE_DIMENSION == 2)
    SXD(scene_view_trace_ray_3d
      (scn->sXd(view), pos, dir, range, &rwalk->hit, &rwalk->hit));
#else
    SXD(scene_view_trace_ray
      (scn->sXd(view), pos, dir, range, &rwalk->hit, &rwalk->hit));
#endif
    if(SXD_HIT_NONE(&rwalk->hit)) { /* Fetch the ambient radiative temperature */
      rwalk->hit_side = SDIS_SIDE_NULL__;
      if(ctx->Tarad >= 0) {
        T->value += ctx->Tarad;
        T->done = 1;
        break;
      } else {
        log_err(scn->dev,
          "%s: the random walk reaches an invalid ambient radiative temperature "
          "of `%gK' at position `%g %g %g'. This may be due to numerical "
          "inaccuracies or to inconsistency in the simulated system (eg: "
          "unclosed geometry). For systems where the random walks can reach "
          "such temperature, one has to setup a valid ambient radiative "
          "temperature, i.e. it must be greater or equal to 0.\n",
          FUNC_NAME,
          ctx->Tarad,
          SPLIT3(rwalk->vtx.P));
        res = RES_BAD_OP;
        goto error;
      }
    }

    /* Define the hit side */
    rwalk->hit_side = fX(dot)(dir, rwalk->hit.normal) < 0
      ? SDIS_FRONT : SDIS_BACK;

    /* Move the random walk to the hit position */
    XD(move_pos)(rwalk->vtx.P, dir, rwalk->hit.distance);

    /* Fetch the new interface and setup the hit fragment */
    interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
    XD(setup_interface_fragment)(&frag, &rwalk->vtx, &rwalk->hit, rwalk->hit_side);

    /* Fetch the interface emissivity */
    epsilon = interface_side_get_emissivity(interf, &frag);
    if(epsilon > 1 || epsilon < 0) {
      log_err(scn->dev,
        "%s: invalid overall emissivity `%g' at position `%g %g %g'.\n",
        FUNC_NAME, epsilon, SPLIT3(rwalk->vtx.P));
      res = RES_BAD_ARG;
      goto error;
    }

    /* Switch in boundary temperature ? */
    r = ssp_rng_canonical(rng);
    if(r < epsilon) {
      T->func = XD(boundary_temperature);
      rwalk->mdm = NULL; /* The random walk is at an interface between 2 media */
      break;
    }

    /* Normalize the normal of the interface and ensure that it points toward the
     * current medium */
    fX(normalize)(N, rwalk->hit.normal);
    if(rwalk->hit_side == SDIS_BACK){
      chk_mdm = interf->medium_back;
      fX(minus)(N, N);
    } else {
      chk_mdm = interf->medium_front;
    }

    if(chk_mdm != rwalk->mdm) {
      log_err(scn->dev, "%s: inconsistent medium definition at `%g %g %g'.\n",
        FUNC_NAME, SPLIT3(rwalk->vtx.P));
      res = RES_BAD_OP;
      goto error;
    }
    alpha = interface_side_get_specular_fraction(interf, &frag);
    r = ssp_rng_canonical(rng);
    if(r < alpha) { /* Sample specular part */
      reflect_3d(dir, f3_minus(dir, dir), N);
    } else { /* Sample diffuse part */
      ssp_ran_hemisphere_cos_float(rng, N, dir, NULL);
    }
  }

exit:
  return res;
error:
  goto exit;
}

res_T
XD(radiative_temperature)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  /* The radiative random walk is always perform in 3D. In 2D, the geometry are
   * assumed to be extruded to the infinty along the Z dimension. */
  float N[3] = {0, 0, 0};
  float dir[3] = {0, 0, 0};
  res_T res = RES_OK;

  ASSERT(scn && fp_to_meter > 0 && ctx && rwalk && rng && T);
  ASSERT(!SXD_HIT_NONE(&rwalk->hit));
  (void)fp_to_meter;

  /* Normalize the normal of the interface and ensure that it points toward the
   * current medium */
  fX(normalize(N, rwalk->hit.normal));
  if(rwalk->hit_side == SDIS_BACK) {
    fX(minus(N, N));
  }

  /* Cosine weighted sampling of a direction around the surface normal */
  ssp_ran_hemisphere_cos_float(rng, N, dir, NULL);

  /* Launch the radiative random walk */
  res = XD(trace_radiative_path)(scn, dir, fp_to_meter, ctx, rwalk, rng, T);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

res_T
XD(fluid_temperature)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  struct sXd(attrib) attr_P, attr_N;
  struct sdis_interface_fragment frag;
  const struct sdis_interface* interf;
  const struct enclosure* enc;
  unsigned enc_ids[2];
  unsigned enc_id;
  double rho; /* Volumic mass */
  double hc; /* Convection coef */
  double cp; /* Calorific capacity */
  double mu;
  double tau;
  double tmp;
  double r;
#if DIM == 2
  float st;
#else
  float st[2];
#endif
  (void)rng, (void)fp_to_meter, (void)ctx;
  ASSERT(scn && fp_to_meter > 0 && ctx && rwalk && rng && T);
  ASSERT(rwalk->mdm->type == SDIS_FLUID);

  tmp = fluid_get_temperature(rwalk->mdm, &rwalk->vtx);
  if(tmp >= 0) { /* T is known. */
    T->value += tmp;
    T->done = 1;
    return RES_OK;
  }

  if(SXD_HIT_NONE(&rwalk->hit)) { /* The path begins in the fluid */
    const float range[2] = {0, FLT_MAX};
    float dir[DIM] = {0};
    float org[DIM];

    dir[DIM-1] = 1;
    fX_set_dX(org, rwalk->vtx.P);

    /* Init the path hit field required to define the current enclosure and
     * fetch the interface data */
    SXD(scene_view_trace_ray(scn->sXd(view), org, dir, range, NULL, &rwalk->hit));
    rwalk->hit_side = fX(dot)(rwalk->hit.normal, dir) < 0 ? SDIS_FRONT : SDIS_BACK;

    if(SXD_HIT_NONE(&rwalk->hit)) {
      log_err(scn->dev,
"%s: the position %g %g %g lies in the surrounding fluid whose temperature must \n"
"be known.\n",
        FUNC_NAME, SPLIT3(rwalk->vtx.P));
      return RES_BAD_OP;
    }
  }

  /* Fetch the current interface and its associated enclosures */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  scene_get_enclosure_ids(scn, rwalk->hit.prim.prim_id, enc_ids);

  /* Define the enclosure identifier of the current medium */
  ASSERT(interf->medium_front != interf->medium_back);
  if(rwalk->mdm == interf->medium_front) {
    enc_id = enc_ids[0];
    ASSERT(rwalk->hit_side == SDIS_FRONT);
  } else {
    ASSERT(rwalk->mdm == interf->medium_back);
    enc_id = enc_ids[1];
    ASSERT(rwalk->hit_side == SDIS_BACK);
  }

  /* Fetch the enclosure data */
  enc = scene_get_enclosure(scn, enc_id);
  if(!enc) {
    /* The possibility for a fluid enclosure to be unregistred is that it is
     * the external enclosure. In this situation unknown temperature is
     * forbidden. */
    log_err(scn->dev,
"%s: invalid enclosure. The surrounding fluid has an unset temperature.\n",
      FUNC_NAME);
    return RES_BAD_ARG;
  }

  /* The hc upper bound can be 0 is h is uniformly 0. In that case the result
   * is the initial condition. */
  if(enc->hc_upper_bound == 0) {
    /* Cannot be in the fluid without starting there. */
    ASSERT(SXD_HIT_NONE(&rwalk->hit));
    rwalk->vtx.time = 0;
    tmp = fluid_get_temperature(rwalk->mdm, &rwalk->vtx);
    if(tmp >= 0) {
      T->value += tmp;
      T->done = 1;
      return RES_OK;
    }

    /* At t=0, the initial condition should have been reached. */
    log_err(scn->dev,
"%s: undefined initial condition. "
"Time is 0 but the temperature remains unknown.\n",
      FUNC_NAME);
    return RES_BAD_OP;
  }

  /* A trick to force first r test result. */
  r = 1;

  /* Sample time until init condition is reached or a true convection occurs. */
  for(;;) {
    /* Setup the fragment of the interface. */
    XD(setup_interface_fragment)(&frag, &rwalk->vtx, &rwalk->hit, rwalk->hit_side);

    /* Fetch hc. */
    hc = interface_get_convection_coef(interf, &frag);
    if(hc > enc->hc_upper_bound) {
      log_err(scn->dev,
        "%s: hc (%g) exceeds its provided upper bound (%g) at %g %g %g.\n",
        FUNC_NAME, hc, enc->hc_upper_bound, SPLIT3(rwalk->vtx.P));
      return RES_BAD_OP;
    }

    if(r < hc / enc->hc_upper_bound) {
      /* True convection. Always true if hc == bound. */
      break;
    }

    /* Fetch other physical properties. */
    cp = fluid_get_calorific_capacity(rwalk->mdm, &rwalk->vtx);
    rho = fluid_get_volumic_mass(rwalk->mdm, &rwalk->vtx);

    /* Sample the time using the upper bound. */
    mu = enc->hc_upper_bound / (rho * cp) * enc->S_over_V;
    tau = ssp_ran_exp(rng, mu);
    rwalk->vtx.time = MMAX(rwalk->vtx.time - tau, 0);

    /* Check the initial condition. */
    tmp = fluid_get_temperature(rwalk->mdm, &rwalk->vtx);
    if(tmp >= 0) {
      T->value += tmp;
      T->done = 1;
      return RES_OK;
    }

    if(rwalk->vtx.time <= 0) {
      /* The initial condition should have been reached. */
      log_err(scn->dev,
"%s: undefined initial condition. "
"Time is 0 but the temperature remains unknown.\n",
        FUNC_NAME);
      return RES_BAD_OP;
    }

    /* Uniformly sample the enclosure. */
#if DIM == 2
    SXD(scene_view_sample
    (enc->sXd(view),
      ssp_rng_canonical_float(rng),
      ssp_rng_canonical_float(rng),
      &rwalk->hit.prim,
      &rwalk->hit.u));
    st = rwalk->hit.u;
#else
    SXD(scene_view_sample
    (enc->sXd(view),
      ssp_rng_canonical_float(rng),
      ssp_rng_canonical_float(rng),
      ssp_rng_canonical_float(rng),
      &rwalk->hit.prim,
      rwalk->hit.uv));
    f2_set(st, rwalk->hit.uv);
#endif

    SXD(primitive_get_attrib(&rwalk->hit.prim, SXD_POSITION, st, &attr_P));
    SXD(primitive_get_attrib(&rwalk->hit.prim, SXD_GEOMETRY_NORMAL, st, &attr_N));
    dX_set_fX(rwalk->vtx.P, attr_P.value);
    fX(set)(rwalk->hit.normal, attr_N.value);

    /* Fetch the interface of the sampled point. */
    interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);

    /* Renew r for next loop. */
    r = ssp_rng_canonical_float(rng);
  }

  rwalk->hit.distance = 0;
  T->func = XD(boundary_temperature);
  rwalk->mdm = NULL; /* The random walk is at an interface between 2 media */
  return RES_OK;
}

static void
XD(solid_solid_boundary_temperature)
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
  const struct sdis_interface* interf = NULL;
  const struct sdis_medium* solid_front = NULL;
  const struct sdis_medium* solid_back = NULL;
  const struct sdis_medium* mdm;
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
    tmp = power * delta_in_meter * delta_in_meter / (2.0 * dim * lambda);
    T->value += tmp;
  }

  /* Reinject */
  XD(move_pos)(rwalk->vtx.P, dir, (float)reinject_dst);
  if(eq_epsf(hit->distance, (float)reinject_dst, 1.e-4f)) {
    T->func = XD(boundary_temperature);
    rwalk->mdm = NULL;
    rwalk->hit = *hit;
    rwalk->hit_side = fX(dot)(hit->normal, dir) < 0 ? SDIS_FRONT : SDIS_BACK;
  } else {
    T->func = XD(solid_temperature);
    rwalk->mdm = mdm;
    rwalk->hit = SXD_HIT_NULL;
    rwalk->hit_side = SDIS_SIDE_NULL__;
  }
}

static void
XD(solid_fluid_boundary_temperature)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
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
    T->func = XD(radiative_temperature);
    rwalk->mdm = fluid;
    rwalk->hit_side = rwalk->mdm == mdm_front ? SDIS_FRONT : SDIS_BACK;
  } else if(r < fluid_proba + radia_proba) { /* Switch to fluid random walk */
    T->func = XD(fluid_temperature);
    rwalk->mdm = fluid;
    rwalk->hit_side = rwalk->mdm == mdm_front ? SDIS_FRONT : SDIS_BACK;
  } else { /* Solid random walk */
    /* Handle the volumic power */
    const double power = solid_get_volumic_power(solid, &rwalk->vtx);
    if(power != SDIS_VOLUMIC_POWER_NONE) {
      const double delta_in_meter = delta_boundary * fp_to_meter;
      tmp = power * delta_in_meter * delta_in_meter / (2.0 * dim * lambda);
      T->value += tmp;
    }

    /* Reinject */
    XD(move_pos)(rwalk->vtx.P, dir0, (float)delta_boundary);
    if(eq_epsf(hit0.distance, (float)delta_boundary, 1.e-4f)) {
      T->func = XD(boundary_temperature);
      rwalk->mdm = NULL;
      rwalk->hit = hit0;
      rwalk->hit_side = fX(dot)(hit0.normal, dir0) < 0 ? SDIS_FRONT : SDIS_BACK;
    } else {
      T->func = XD(solid_temperature);
      rwalk->mdm = solid;
      rwalk->hit = SXD_HIT_NULL;
      rwalk->hit_side = SDIS_SIDE_NULL__;
    }
  }
}

static void
XD(solid_boundary_with_flux_temperature)
  (const struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   const double phi,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  const struct sdis_interface* interf = NULL;
  const struct sdis_medium* mdm = NULL;
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
  T->value += phi * delta_in_meter / lambda;

  /* Handle the volumic power */
  power = solid_get_volumic_power(mdm, &rwalk->vtx);
  if(power != SDIS_VOLUMIC_POWER_NONE) {
    delta_in_meter = delta_boundary * fp_to_meter;
    tmp = power * delta_in_meter * delta_in_meter / (2.0 * dim * lambda);
    T->value += tmp;
  }

  /* Reinject into the solid */
  XD(move_pos)(rwalk->vtx.P, dir0, (float)delta_boundary);
  if(eq_epsf(hit0.distance, (float)delta_boundary, 1.e-4f)) {
    T->func = XD(boundary_temperature);
    rwalk->mdm = NULL;
    rwalk->hit = hit0;
    rwalk->hit_side = fX(dot)(hit0.normal, dir0) < 0 ? SDIS_FRONT : SDIS_BACK;
  } else {
    T->func = XD(solid_temperature);
    rwalk->mdm = mdm;
    rwalk->hit = SXD_HIT_NULL;
    rwalk->hit_side = SDIS_SIDE_NULL__;
  }
}

res_T
XD(boundary_temperature)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  const struct sdis_interface* interf = NULL;
  const struct sdis_medium* mdm_front = NULL;
  const struct sdis_medium* mdm_back = NULL;
  const struct sdis_medium* mdm = NULL;
  double tmp;
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
    return RES_OK;
  }

  /* Check if the boundary flux is known. Note that actually, only solid media
   * can have a flux as limit condition */
  mdm = interface_get_medium(interf, frag.side);
  if(sdis_medium_get_type(mdm) == SDIS_SOLID ) {
    const double phi = interface_side_get_flux(interf, &frag);
    if(phi != SDIS_FLUX_NONE) {
      XD(solid_boundary_with_flux_temperature)
        (scn, fp_to_meter, ctx, &frag, phi, rwalk, rng, T);
      return RES_OK;
    }
  }

  mdm_front = interface_get_medium(interf, SDIS_FRONT);
  mdm_back = interface_get_medium(interf, SDIS_BACK);

  if(mdm_front->type == mdm_back->type) {
    XD(solid_solid_boundary_temperature)
      (scn, fp_to_meter, ctx, &frag, rwalk, rng, T);
  } else {
    XD(solid_fluid_boundary_temperature)
      (scn, fp_to_meter, ctx, &frag, rwalk, rng, T);
  }
  return RES_OK;
}

#ifdef COMPILER_CL
#pragma warning(push)
#pragma warning(disable : 4701)
/* potentially uninitialized local variable 'info' used
 *
 * For warning numbers in the range 4700-4999, which are the ones associated
 * with code generation, the state of the warning in effect when the compiler
 * encounters the open curly brace of a function will be in effect for the rest
 * of the function. Using the warning pragma in the function to change the
 * state of a warning that has a number larger than 4699 will only take effect
 * after the end of the function. The following example shows the correct
 * placement of warning pragmas to disable a code-generation warning message,
 * and then to restore it. */
#endif
res_T
XD(solid_temperature)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  double position_start[DIM];
  const struct sdis_medium* mdm;
  ASSERT(scn && fp_to_meter > 0 && rwalk && rng && T);
  ASSERT(rwalk->mdm->type == SDIS_SOLID);
  (void)ctx;

  /* Check the random walk consistency */
  CHK(scene_get_medium(scn, rwalk->vtx.P, NULL, &mdm) == RES_OK);
  if(mdm != rwalk->mdm) {
    log_err(scn->dev, "%s: invalid solid random walk. "
      "Unexpected medium at {%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(rwalk->vtx.P));
    return RES_BAD_OP_IRRECOVERABLE;
  }
  /* Save the submitted position */
  dX(set)(position_start, rwalk->vtx.P);

  do { /* Solid random walk */
    struct get_medium_info info;
    struct sXd(hit) hit0, hit1;
    double lambda; /* Thermal conductivity */
    double rho; /* Volumic mass */
    double cp; /* Calorific capacity */
    double tau, mu;
    double tmp;
    double power;
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
    power = solid_get_volumic_power(mdm, &rwalk->vtx);

#if (SDIS_SOLVE_DIMENSION == 2)
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

      /* Add the volumic power density to the measured temperature */
      if(power != SDIS_VOLUMIC_POWER_NONE) {
        const double delta_in_meter = delta * fp_to_meter;
        tmp = power * delta_in_meter * delta_in_meter / (2.0 * DIM * lambda);
        T->value += tmp;
      }
    } else {
      /* Hit something: move along dir0 of the minimum hit distance */
      delta = MMIN(hit0.distance, hit1.distance);

      /* Add the volumic power density to the measured temperature */
      if(power != SDIS_VOLUMIC_POWER_NONE) {
        const double delta_s_in_meter = delta_solid * fp_to_meter;
        double h;
        double h_in_meter;
        double cos_U_N;
        float N[DIM];

        if(delta == hit0.distance) {
          fX(normalize)(N, hit0.normal);
          cos_U_N = fX(dot)(dir0, N);
        } else {
          ASSERT(delta == hit1.distance);
          fX(normalize)(N, hit1.normal);
          cos_U_N = fX(dot)(dir1, N);
        }

        h = delta * fabs(cos_U_N);
        h_in_meter = h * fp_to_meter;

        /* The regular power term at wall */
        tmp = power * h_in_meter * h_in_meter / (2.0 * lambda);

        /* Add the power corrective term */
        if(h < delta_solid) {
          const double sin_a = h / delta_solid;
#if DIM==2
          /* tmp1 = sin(2a) / (PI - 2*a) */
          const double tmp1 = sin_a * sqrt(1 - sin_a*sin_a)/acos(sin_a);
          tmp += -(power*delta_s_in_meter*delta_s_in_meter)/(4.0*lambda) * tmp1;
#else
          const double tmp1 = (sin_a*sin_a*sin_a - sin_a)/ (1-sin_a);
          tmp += (power*delta_s_in_meter*delta_s_in_meter)/(6*lambda) * tmp1;
#endif

        } else if(h == delta_solid) {
          tmp += -(delta_s_in_meter*delta_s_in_meter*power)/(2.0*DIM*lambda);
        }
        T->value += tmp;
      }
    }

    /* Sample the time */
    mu = (2*DIM*lambda) / (rho*cp*delta*fp_to_meter*delta*fp_to_meter);
    tau = ssp_ran_exp(rng, mu);
    rwalk->vtx.time = MMAX(rwalk->vtx.time - tau, 0);

    /* Check the initial condition */
    tmp = solid_get_temperature(mdm, &rwalk->vtx);
    if(tmp >= 0) {
      T->value += tmp;
      T->done = 1;
      return RES_OK;
    }

    /* The initial condition should be reached */
    if(rwalk->vtx.time <=0) {
      log_err(scn->dev,
        "%s: undefined initial condition. "
        "The time is null but the temperature remains unknown.\n",
        FUNC_NAME);
      return RES_BAD_OP;
    }

    /* Define if the random walk hits something along dir0 */
    if(hit0.distance > delta) {
      rwalk->hit = SXD_HIT_NULL;
      rwalk->hit_side = SDIS_SIDE_NULL__;
    } else {
      rwalk->hit = hit0;
      rwalk->hit_side = fX(dot)(hit0.normal, dir0) < 0 ? SDIS_FRONT : SDIS_BACK;
    }

    /* Update the random walk position */
    XD(move_pos)(rwalk->vtx.P, dir0, delta);

    /* Fetch the current medium */
    if(SXD_HIT_NONE(&rwalk->hit)) {
      CHK(scene_get_medium(scn, rwalk->vtx.P, &info, &mdm) == RES_OK);
    } else {
      const struct sdis_interface* interf;
      interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
      mdm = interface_get_medium(interf, rwalk->hit_side);
    }

    /* Check random walk consistency */
    if(mdm != rwalk->mdm) {
      log_err(scn->dev,
        "%s: inconsistent medium during the solid random walk.\n", FUNC_NAME);
#if DIM == 2
  #define VEC_STR "%g %g"
  #define VEC_SPLIT SPLIT2
#else
  #define VEC_STR "%g %g %g"
  #define VEC_SPLIT SPLIT3
#endif
      log_err(scn->dev,
        "  start position: " VEC_STR "; current position: " VEC_STR "\n",
        VEC_SPLIT(position_start), VEC_SPLIT(rwalk->vtx.P));
      if(SXD_HIT_NONE(&rwalk->hit)) {
        float hit_pos[DIM];
        fX(mulf)(hit_pos, info.ray_dir, info.XD(hit).distance);
        fX(add)(hit_pos, info.ray_org, hit_pos);
        log_err(scn->dev, "  ray org: " VEC_STR "; ray dir: " VEC_STR "\n",
          VEC_SPLIT(info.ray_org), VEC_SPLIT(info.ray_dir));
        log_err(scn->dev, "  targeted point: " VEC_STR "\n",
          VEC_SPLIT(info.pos_tgt));
        log_err(scn->dev, "  hit pos: " VEC_STR "\n", VEC_SPLIT(hit_pos));
      }
#undef VEC_STR
#undef VEC_SPLIT
      return RES_BAD_OP;
    }

  /* Keep going while the solid random walk does not hit an interface */
  } while(SXD_HIT_NONE(&rwalk->hit));

  T->func = XD(boundary_temperature);
  rwalk->mdm = NULL; /* The random walk is at an interface between 2 media */
  return RES_OK;
}
#ifdef COMPILER_CL
#pragma warning(pop)
#endif

static res_T
XD(compute_temperature)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
#ifndef NDEBUG
  struct entry {
    struct XD(temperature) temperature;
    struct XD(rwalk) rwalk;
  }* stack = NULL;
  size_t istack = 0;
#endif
  const size_t max_fails = 10;
  res_T res = RES_OK;
  ASSERT(scn && fp_to_meter > 0 && ctx && rwalk && rng && T);

  do {
    /* Save the current random walk state */
    const struct XD(rwalk) rwalk_bkp = *rwalk;
    const struct XD(temperature) T_bkp = *T;

    size_t nfails = 0;

#ifndef NDEBUG
    struct entry e;
    e.temperature = *T;
    e.rwalk = *rwalk;
    sa_push(stack, e);
    ++istack;
#endif

    /* Reject the current step if a BAD_OP occurs and retry up to "max_fails"
     * times */
    do {
      res = T->func(scn, fp_to_meter, ctx, rwalk, rng, T);
      if(res == RES_BAD_OP) { *rwalk = rwalk_bkp; *T = T_bkp; }
    } while(res == RES_BAD_OP && ++nfails < max_fails);
    if(res != RES_OK) goto error;

  } while(!T->done);

exit:
#ifndef NDEBUG
  sa_release(stack);
#endif
  return res == RES_BAD_OP_IRRECOVERABLE ? RES_BAD_OP : res;
error:
  goto exit;
}

static res_T
XD(probe_realisation)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct sdis_medium* medium,
   const double position[],
   const double time_range[2],
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   const double ambient_radiative_temperature,
   const double reference_temperature,
   double* weight)
{
  struct rwalk_context ctx;
  struct XD(rwalk) rwalk = XD(RWALK_NULL);
  struct XD(temperature) T = XD(TEMPERATURE_NULL);
  res_T res = RES_OK;
  ASSERT(medium && position && fp_to_meter > 0 && weight);

  switch(medium->type) {
    case SDIS_FLUID: T.func = XD(fluid_temperature); break;
    case SDIS_SOLID: T.func = XD(solid_temperature); break;
    default: FATAL("Unreachable code\n"); break;
  }

  dX(set)(rwalk.vtx.P, position);
  /* Sample a time */
  rwalk.vtx.time = sample_time(time_range, rng);
  rwalk.hit = SXD_HIT_NULL;
  rwalk.mdm = medium;

  ctx.Tarad = ambient_radiative_temperature;
  ctx.Tref3 =
    reference_temperature
  * reference_temperature
  * reference_temperature;

  res = XD(compute_temperature)(scn, fp_to_meter, &ctx, &rwalk, rng, &T);
  if(res != RES_OK) return res;

  *weight = T.value;
  return RES_OK;
}

static res_T
XD(boundary_realisation)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const size_t iprim,
   const double uv[2],
   const double time_range[2],
   const enum sdis_side side,
   const double fp_to_meter,
   const double Tarad,
   const double Tref,
   double* weight)
{
  struct rwalk_context ctx;
  struct XD(rwalk) rwalk = XD(RWALK_NULL);
  struct XD(temperature) T = XD(TEMPERATURE_NULL);
  struct sXd(attrib) attr;
#if SDIS_SOLVE_DIMENSION == 2
  float st;
#else
  float st[2];
#endif
  res_T res = RES_OK;
  ASSERT(uv && fp_to_meter > 0 && weight && Tref >= 0
     && time_range && time_range[0] >= 0 && time_range[1] >= time_range[0]);

  T.func = XD(boundary_temperature);
  rwalk.hit_side = side;
  rwalk.hit.distance = 0;
  /* Sample a time */
  rwalk.vtx.time = sample_time(time_range, rng);
  rwalk.mdm = NULL; /* The random walk is at an interface between 2 media */

#if SDIS_SOLVE_DIMENSION == 2
  st = (float)uv[0];
#else
  f2_set_d2(st, uv);
#endif

  /* Fetch the primitive */
  SXD(scene_view_get_primitive
    (scn->sXd(view), (unsigned int)iprim, &rwalk.hit.prim));

  /* Retrieve the world space position of the probe onto the primitive */
  SXD(primitive_get_attrib(&rwalk.hit.prim, SXD_POSITION, st, &attr));
  dX_set_fX(rwalk.vtx.P, attr.value);

  /* Retrieve the primitive normal */
  SXD(primitive_get_attrib(&rwalk.hit.prim, SXD_GEOMETRY_NORMAL, st, &attr));
  fX(set)(rwalk.hit.normal, attr.value);

#if SDIS_SOLVE_DIMENSION==2
  rwalk.hit.u = st;
#else
  f2_set(rwalk.hit.uv, st);
#endif

  ctx.Tarad = Tarad;
  ctx.Tref3 = Tref*Tref*Tref;

  res = XD(compute_temperature)(scn, fp_to_meter, &ctx, &rwalk, rng, &T);
  if(res != RES_OK) return res;

  *weight = T.value;
  return RES_OK;
}

static res_T
XD(probe_flux_realisation)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const size_t iprim,
   const double uv[DIM],
   const double time,
   const enum sdis_side solid_side,
   const double fp_to_meter,
   const double Tarad,
   const double Tref,
   const char compute_radiative,
   const char compute_convective,
   double weight[3])
{
  struct rwalk_context ctx;
  struct XD(rwalk) rwalk;
  struct XD(temperature) T;
  struct sXd(attrib) attr;
  struct sXd(primitive) prim;
#if SDIS_SOLVE_DIMENSION == 2
  float st;
#else
  float st[2];
#endif
  double P[SDIS_SOLVE_DIMENSION];
  float N[SDIS_SOLVE_DIMENSION];
  const double Tr3 = Tref * Tref * Tref;
  const enum sdis_side fluid_side =
    (solid_side == SDIS_FRONT) ? SDIS_BACK : SDIS_FRONT;
  res_T res = RES_OK;
  ASSERT(uv && fp_to_meter > 0 && weight && time >= 0 && Tref >= 0);

#if SDIS_SOLVE_DIMENSION == 2
  #define SET_PARAM(Dest, Src) (Dest).u = (Src);
  st = (float)uv[0];
#else
  #define SET_PARAM(Dest, Src) f2_set((Dest).uv, (Src));
  f2_set_d2(st, uv);
#endif

  /* Fetch the primitive */
  SXD(scene_view_get_primitive(scn->sXd(view), (unsigned int)iprim, &prim));

  /* Retrieve the world space position of the probe onto the primitive */
  SXD(primitive_get_attrib(&prim, SXD_POSITION, st, &attr));
  dX_set_fX(P, attr.value);

  /* Retrieve the primitive normal */
  SXD(primitive_get_attrib(&prim, SXD_GEOMETRY_NORMAL, st, &attr));
  fX(set)(N, attr.value);

  #define RESET_WALK(Side, Mdm) \
  rwalk = XD(RWALK_NULL); \
  rwalk.hit_side = (Side); \
  rwalk.hit.distance = 0; \
  rwalk.vtx.time = time; \
  rwalk.mdm = (Mdm); \
  SET_PARAM(rwalk.hit, st); \
  ctx.Tarad = Tarad; \
  ctx.Tref3 = Tr3; \
  rwalk.hit.prim = prim; \
  dX(set)(rwalk.vtx.P, P); \
  fX(set)(rwalk.hit.normal, N); \
  T = XD(TEMPERATURE_NULL);

  /* Compute boundary temperature */
  RESET_WALK(solid_side, NULL);
  T.func = XD(boundary_temperature);
  res = XD(compute_temperature)(scn, fp_to_meter, &ctx, &rwalk, rng, &T);
  if(res != RES_OK) return res;
  weight[0] = T.value;

  /* Compute radiative temperature */
  if(compute_radiative) {
    RESET_WALK(fluid_side, NULL);
    T.func = XD(radiative_temperature);
    res = XD(compute_temperature)(scn, fp_to_meter, &ctx, &rwalk, rng, &T);
    if(res != RES_OK) return res;
    weight[1] = T.value;
  }

  /* Compute fluid temperature */
  if(compute_convective) {
    const struct sdis_interface* interf =
      scene_get_interface(scn, (unsigned)iprim);
    const struct sdis_medium* mdm = interface_get_medium(interf, fluid_side);

    RESET_WALK(fluid_side, mdm);
    T.func = XD(fluid_temperature);
    res = XD(compute_temperature)(scn, fp_to_meter, &ctx, &rwalk, rng, &T);
    if(res != RES_OK) return res;
    weight[2] = T.value;
  }

  #undef SET_PARAM
  #undef RESET_WALK

  return RES_OK;
}

static INLINE res_T
XD(interface_prebuild_fragment)
  (struct sdis_interface_fragment* frag,
   const struct sdis_scene* scn,
   const unsigned iprim,
   const double* uv,
   const enum sdis_side fluid_side)
{  struct sXd(attrib) attr;
  struct sXd(primitive) prim;
  struct sXd(hit) hit;
  struct sdis_rwalk_vertex vtx;
#if SDIS_SOLVE_DIMENSION == 2
  float st;
#else
  float st[2];
#endif
  res_T res = RES_OK;

  ASSERT(frag && scn && uv);
  ASSERT(fluid_side == SDIS_FRONT || fluid_side == SDIS_BACK);

  *frag = SDIS_INTERFACE_FRAGMENT_NULL;

#if SDIS_SOLVE_DIMENSION == 2
#define SET_PARAM(Dest, Src) (Dest).u = (Src);
  st = (float)uv[0];
#else
#define SET_PARAM(Dest, Src) f2_set((Dest).uv, (Src));
  f2_set_d2(st, uv);
#endif
  res = sXd(scene_view_get_primitive(scn->sXd(view), iprim, &prim));
  if(res != RES_OK) return res;
  res = sXd(primitive_get_attrib(&prim, SXD_POSITION, st, &attr));
  if(res != RES_OK) return res;
  dX_set_fX(vtx.P, attr.value);
  res = sXd(primitive_get_attrib(&prim, SXD_GEOMETRY_NORMAL, st, &attr));
  if(res != RES_OK) return res;
  fX(set)(hit.normal, attr.value);

  hit.distance = 0;
  hit.prim = prim;
  vtx.time = NaN;
  SET_PARAM(hit, st);
  #undef SET_PARAM
  XD(setup_interface_fragment)(frag, &vtx, &hit, fluid_side);

  return res;
}

static res_T
XD(interface_get_hc_epsilon)
  (double *hc,
   double* epsilon,
   const struct sdis_scene* scn,
   const unsigned iprim,
   const double* uv,
   const double time,
   const enum sdis_side fluid_side)
{
  struct sdis_interface_fragment frag;
  const struct sdis_interface* interf;
  res_T res = RES_OK;

  res = XD(interface_prebuild_fragment)(&frag, scn, iprim, uv, fluid_side);
  frag.time = time;
  interf = scene_get_interface(scn, iprim);
  ASSERT(interf);
  *epsilon = interface_side_get_emissivity(interf, &frag);
  *hc = interface_get_convection_coef(interf, &frag);

  return res;
}

#if SDIS_SOLVE_DIMENSION == 3
static res_T
XD(ray_realisation)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct sdis_medium* medium,
   const double position[],
   const double direction[],
   const double time,
   const double fp_to_meter,
   const double Tarad,
   const double Tref,
   double* weight)
{
  struct rwalk_context ctx;
  struct XD(rwalk) rwalk = XD(RWALK_NULL);
  struct XD(temperature) T = XD(TEMPERATURE_NULL);
  float dir[3];
  res_T res = RES_OK;
  ASSERT(scn && position && direction && time>=0 && fp_to_meter>0 && weight
    && Tref >= 0);
  ASSERT(medium && medium->type == SDIS_FLUID);

  dX(set)(rwalk.vtx.P, position);
  rwalk.vtx.time = time;
  rwalk.hit = SXD_HIT_NULL;
  rwalk.hit_side = SDIS_SIDE_NULL__;
  rwalk.mdm = medium;

  ctx.Tarad = Tarad;
  ctx.Tref3 = Tref*Tref*Tref;

  f3_set_d3(dir, direction);

  res = XD(trace_radiative_path)(scn, dir, fp_to_meter, &ctx, &rwalk, rng, &T);
  if(res != RES_OK) goto error;

  if(!T.done) {
    res = XD(compute_temperature)(scn, fp_to_meter, &ctx, &rwalk, rng, &T);
    if(res != RES_OK) goto error;
  }

  *weight = T.value;

exit:
  return res;
error:
  goto exit;
}
#endif /* SDIS_SOLVE_DIMENSION == 3 */

static res_T
XD(solve_boundary)
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t primitives[], /* List of boundary primitives to handle */
   const enum sdis_side sides[], /* Per primitive side to consider */
   const size_t nprimitives, /* #primitives */
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
   struct sdis_estimator** out_estimator)
{
  struct XD(boundary_context) ctx = XD(BOUNDARY_CONTEXT_NULL);
  struct sXd(vertex_data) vdata = SXD_VERTEX_DATA_NULL;
  struct sXd(scene)* scene = NULL;
  struct sXd(shape)* shape = NULL;
  struct sXd(scene_view)* view = NULL;
  struct sdis_estimator* estimator = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  size_t i;
  size_t N = 0; /* #realisations that do not fail */
  size_t view_nprims;
  double weight=0, sqr_weight=0;
  int64_t irealisation;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !primitives
    || !time_range || time_range[0] < 0 || time_range[1] < time_range[0]
    || (time_range[1] > DBL_MAX && time_range[0] != time_range[1])
    || !sides || !nprimitives || fp_to_meter < 0 || Tref < 0
    || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

  SXD(scene_view_primitives_count(scn->sXd(view), &view_nprims));
  FOR_EACH(i, 0, nprimitives) {
    if(primitives[i] >= view_nprims) {
      log_err(scn->dev,
        "%s: invalid primitive identifier `%lu'. It must be in the [0 %lu] range.\n",
        FUNC_NAME,
        (unsigned long)primitives[i],
        (unsigned long)scene_get_primitives_count(scn)-1);
      res = RES_BAD_ARG;
      goto error;
    }
  }

  /* Create the Star-XD shape of the boundary */
#if DIM == 2
  res = s2d_shape_create_line_segments(scn->dev->sXd_dev, &shape);
#else
  res = s3d_shape_create_mesh(scn->dev->sXd_dev, &shape);
#endif
  if(res != RES_OK) goto error;

  /* Initialise the boundary shape with the triangles/segments of the
   * submitted primitives  */
  ctx.primitives = primitives;
  ctx.view = scn->sXd(view);
  vdata.usage = SXD_POSITION;
  vdata.get = XD(boundary_get_position);
#if DIM == 2
  vdata.type = S2D_FLOAT2;
  res = s2d_line_segments_setup_indexed_vertices(shape, (unsigned)nprimitives,
    boundary_get_indices_2d, (unsigned)(nprimitives*2), &vdata, 1, &ctx);
#else /* DIM == 3 */
  vdata.type = S3D_FLOAT3;
  res = s3d_mesh_setup_indexed_vertices(shape, (unsigned)nprimitives,
    boundary_get_indices_3d, (unsigned)(nprimitives*3), &vdata, 1, &ctx);
#endif
  if(res != RES_OK) goto error;

  /* Create and setup the boundary Star-XD scene */
  res = sXd(scene_create)(scn->dev->sXd_dev, &scene);
  if(res != RES_OK) goto error;
  res = sXd(scene_attach_shape)(scene, shape);
  if(res != RES_OK) goto error;
  res = sXd(scene_view_create)(scene, SXD_SAMPLE, &view);
  if(res != RES_OK) goto error;

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*rngs));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs+i);
    if(res != RES_OK) goto error;
  }

  /* Create the estimator */
  res = estimator_create(scn->dev, SDIS_TEMPERATURE_ESTIMATOR, &estimator);
  if(res != RES_OK) goto error;

  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) reduction(+:weight,sqr_weight,N)
  for(irealisation=0; irealisation<(int64_t)nrealisations; ++irealisation) {
    const int ithread = omp_get_thread_num();
    struct sXd(primitive) prim;
    struct ssp_rng* rng = rngs[ithread];
    enum sdis_side side;
    size_t iprim;
    double w = NaN;
    double uv[DIM-1];
    float st[DIM-1];
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    /* Sample a position onto the boundary */
#if DIM == 2
    res_local = s2d_scene_view_sample
      (view,
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, st);
   uv[0] = (double)st[0];
#else
    res_local = s3d_scene_view_sample
      (view,
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, st);
    d2_set_f2(uv, st);
#endif
    if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); continue; }

    /* Map from boundary scene to sdis scene */
    ASSERT(prim.prim_id < nprimitives);
    iprim = primitives[prim.prim_id];
    side = sides[prim.prim_id];

    /* Invoke the boundary realisation */
    res_local = XD(boundary_realisation)
      (scn, rng, iprim, uv, time_range, side, fp_to_meter, Tarad, Tref, &w);

    /* Update the MC accumulators */
    if(res_local == RES_OK) {
      weight += w;
      sqr_weight += w*w;
      ++N;
    } else if(res_local != RES_BAD_OP) {
      ATOMIC_SET(&res, res_local);
      continue;
    }
  }

  setup_estimator(estimator, nrealisations, N, weight, sqr_weight);

exit:
  if(scene) SXD(scene_ref_put(scene));
  if(shape) SXD(shape_ref_put(shape));
  if(view) SXD(scene_view_ref_put(view));
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_estimator) *out_estimator = estimator;
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) {if(rngs[i]) SSP(rng_ref_put(rngs[i]));}
    MEM_RM(scn->dev->allocator, rngs);
  }
  return (res_T)res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

static res_T
XD(solve_boundary_flux)
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t primitives[], /* List of boundary primitives to handle */
   const size_t nprimitives, /* #primitives */
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
   struct sdis_estimator** out_estimator)
{
  struct XD(boundary_context) ctx = XD(BOUNDARY_CONTEXT_NULL);
  struct sXd(vertex_data) vdata = SXD_VERTEX_DATA_NULL;
  struct sXd(scene)* scene = NULL;
  struct sXd(shape)* shape = NULL;
  struct sXd(scene_view)* view = NULL;
  struct sdis_estimator* estimator = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  double weight_t = 0, sqr_weight_t = 0;
  double weight_fc = 0, sqr_weight_fc = 0;
  double weight_fr = 0, sqr_weight_fr = 0;
  double weight_f = 0, sqr_weight_f = 0;
  size_t i;
  size_t N = 0; /* #realisations that do not fail */
  size_t view_nprims;
  int64_t irealisation;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !primitives
    || !time_range || time_range[0] < 0 || time_range[1] < time_range[0]
    || (time_range[1] > DBL_MAX && time_range[0] != time_range[1])
    || !nprimitives || fp_to_meter < 0 || Tref < 0
    || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

  SXD(scene_view_primitives_count(scn->sXd(view), &view_nprims));
  FOR_EACH(i, 0, nprimitives) {
    if(primitives[i] >= view_nprims) {
      log_err(scn->dev,
        "%s: invalid primitive identifier `%lu'. It must be in the [0 %lu] range.\n",
        FUNC_NAME,
        (unsigned long)primitives[i],
        (unsigned long)scene_get_primitives_count(scn)-1);
      res = RES_BAD_ARG;
      goto error;
    }
  }

  /* Create the Star-XD shape of the boundary */
#if DIM == 2
  res = s2d_shape_create_line_segments(scn->dev->s2d, &shape);
#else
  res = s3d_shape_create_mesh(scn->dev->s3d, &shape);
#endif
  if(res != RES_OK) goto error;

  /* Initialise the boundary shape with the triangles/segments of the
   * submitted primitives  */
  ctx.primitives = primitives;
  ctx.view = scn->sXd(view);
  vdata.get = XD(boundary_get_position);
#if DIM == 2
  vdata.usage = S2D_POSITION;
  vdata.type = S2D_FLOAT2;
  res = s2d_line_segments_setup_indexed_vertices(shape, (unsigned)nprimitives,
    boundary_get_indices_2d, (unsigned)(nprimitives*2), &vdata, 1, &ctx);
#else /* DIM == 3 */
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  res = s3d_mesh_setup_indexed_vertices(shape, (unsigned)nprimitives,
    boundary_get_indices_3d, (unsigned)(nprimitives*3), &vdata, 1, &ctx);
#endif
  if(res != RES_OK) goto error;

  /* Create and setup the boundary Star-XD scene */
  res = sXd(scene_create)(scn->dev->sXd_dev, &scene);
  if(res != RES_OK) goto error;
  res = sXd(scene_attach_shape)(scene, shape);
  if(res != RES_OK) goto error;
  res = sXd(scene_view_create)(scene, SXD_SAMPLE, &view);
  if(res != RES_OK) goto error;

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*rngs));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs + i);
    if(res != RES_OK) goto error;
  }

  /* Create the estimator */
  res = estimator_create(scn->dev, SDIS_FLUX_ESTIMATOR, &estimator);
  if(res != RES_OK) goto error;

  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) reduction(+:weight_t,sqr_weight_t,\
     weight_fc,sqr_weight_fc,weight_fr,sqr_weight_fr,weight_f,sqr_weight_f,N)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    const int ithread = omp_get_thread_num();
    struct sXd(primitive) prim;
    struct ssp_rng* rng = rngs[ithread];
    const struct sdis_interface* interf;
    const struct sdis_medium *fmd, *bmd;
    enum sdis_side solid_side, fluid_side;
    double T_brf[3] = { 0, 0, 0 };
    double epsilon, hc, hr;
    size_t iprim;
    double uv[DIM - 1];
    float st[DIM - 1];
    double time;
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    /* Sample a time */
    time = sample_time(time_range, rng);

    /* Sample a position onto the boundary */
#if DIM == 2
    res_local = s2d_scene_view_sample
      (view,
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, st);
    uv[0] = (double)st[0];
#else
    res_local = s3d_scene_view_sample
      (view,
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, st);
    d2_set_f2(uv, st);
#endif
    if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); continue; }

    /* Map from boundary scene to sdis scene */
    ASSERT(prim.prim_id < nprimitives);
    iprim = primitives[prim.prim_id];

    interf = scene_get_interface(scn, (unsigned)iprim);
    fmd = interface_get_medium(interf, SDIS_FRONT);
    bmd = interface_get_medium(interf, SDIS_BACK);
    if(!fmd || !bmd
      || (!(fmd->type == SDIS_FLUID && bmd->type == SDIS_SOLID)
        && !(fmd->type == SDIS_SOLID && bmd->type == SDIS_FLUID)))
    {
      ATOMIC_SET(&res, RES_BAD_ARG);
      continue;
    }
    solid_side = (fmd->type == SDIS_SOLID) ? SDIS_FRONT : SDIS_BACK;
    fluid_side = (fmd->type == SDIS_FLUID) ? SDIS_FRONT : SDIS_BACK;

    res_local = XD(interface_get_hc_epsilon)(&hc, &epsilon, scn,
      (unsigned)iprim, uv, time, fluid_side);
    if(res_local != RES_OK) {
      ATOMIC_SET(&res, res_local);
      continue;
    }
    hr = 4.0 * BOLTZMANN_CONSTANT * Tref * Tref * Tref * epsilon;

    /* Fluid, Radiative and Solid temperatures */
    res_local = XD(probe_flux_realisation)(scn, rng, iprim, uv, time,
      solid_side, fp_to_meter, Tarad, Tref, hr > 0, hc > 0, T_brf);
    if(res_local != RES_OK) {
      if(res_local != RES_BAD_OP) {
        ATOMIC_SET(&res, res_local);
        continue;
      }
    } else {
      const double Tboundary = T_brf[0];
      const double Tradiative = T_brf[1];
      const double Tfluid = T_brf[2];
      const double w_conv = hc * (Tboundary - Tfluid);
      const double w_rad = hr * (Tboundary - Tradiative);
      const double w_total = w_conv + w_rad;
      weight_t += Tboundary;
      sqr_weight_t += Tboundary * Tboundary;
      weight_fc += w_conv;
      sqr_weight_fc += w_conv * w_conv;
      weight_fr += w_rad;
      sqr_weight_fr += w_rad * w_rad;
      weight_f += w_total;
      sqr_weight_f += w_total * w_total;
      ++N;
    }
  }
  if(res != RES_OK) goto error;

  setup_estimator(estimator, nrealisations, N, weight_t, sqr_weight_t);
  setup_estimator_flux(estimator, FLUX_CONVECTIVE__, weight_fc, sqr_weight_fc);
  setup_estimator_flux(estimator, FLUX_RADIATIVE__, weight_fr, sqr_weight_fr);
  setup_estimator_flux(estimator, FLUX_TOTAL__, weight_f, sqr_weight_f);

exit:
  if(scene) SXD(scene_ref_put(scene));
  if(shape) SXD(shape_ref_put(shape));
  if(view) SXD(scene_view_ref_put(view));
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_estimator) *out_estimator = estimator;
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) { if(rngs[i]) SSP(rng_ref_put(rngs[i])); }
    MEM_RM(scn->dev->allocator, rngs);
  }
  return (res_T)res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

#undef SDIS_SOLVE_DIMENSION
#undef DIM
#undef sXd
#undef sXd_dev
#undef SXD_HIT_NONE
#undef SXD_HIT_NULL
#undef SXD_HIT_NULL__
#undef SXD_POSITION
#undef SXD_GEOMETRY_NORMAL
#undef SXD_VERTEX_DATA_NULL
#undef SXD_FLOAT2
#undef SXD_FLOAT3
#undef SXD_SAMPLE
#undef SXD
#undef dX
#undef fX
#undef fX_set_dX
#undef XD

#endif /* !SDIS_SOLVE_DIMENSION */

