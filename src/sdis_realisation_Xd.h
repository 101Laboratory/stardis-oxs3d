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
#include "sdis_heat_path.h"
#include "sdis_interface_c.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_scene_c.h"

#include <rsys/stretchy_array.h>
#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
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
  /* Stack that saves the state of each recursion steps.  */
  struct entry {
    struct XD(temperature) temperature;
    struct XD(rwalk) rwalk;
  }* stack = NULL;
  size_t istack = 0;
#endif
  /* Maximum accepted #failures before stopping the realisation */
  const size_t MAX_FAILS = 10;
  res_T res = RES_OK;
  ASSERT(scn && fp_to_meter > 0 && ctx && rwalk && rng && T);

  do {
    /* Save the current random walk state */
    const struct XD(rwalk) rwalk_bkp = *rwalk;
    const struct XD(temperature) T_bkp = *T;
    size_t nfails = 0; /* #failures */

#ifndef NDEBUG
    struct entry e;
    e.temperature = *T;
    e.rwalk = *rwalk;
    sa_push(stack, e);
    ++istack;
#endif

    /* Reject the step if a BAD_OP occurs and retry up to MAX_FAILS times */
    do {
      res = T->func(scn, fp_to_meter, ctx, rwalk, rng, T);
      if(res == RES_BAD_OP) { *rwalk = rwalk_bkp; *T = T_bkp; }
    } while(res == RES_BAD_OP && ++nfails < MAX_FAILS);
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


/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(probe_realisation)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct sdis_medium* medium,
   const double position[],
   const double time,
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   const double ambient_radiative_temperature,
   const double reference_temperature,
   double* weight)
{
  struct rwalk_context ctx;
  struct XD(rwalk) rwalk = XD(RWALK_NULL);
  struct XD(temperature) T = XD(TEMPERATURE_NULL);
  double t0;
  double (*get_initial_temperature)
    (const struct sdis_medium* mdm,
     const struct sdis_rwalk_vertex* vtx);
  res_T res = RES_OK;
  ASSERT(medium && position && fp_to_meter > 0 && weight && time >= 0);

  switch(medium->type) {
    case SDIS_FLUID:
      T.func = XD(convective_path);
      get_initial_temperature = fluid_get_temperature;
      t0 = fluid_get_t0(medium);
      break;
    case SDIS_SOLID:
      T.func = XD(conductive_path);
      get_initial_temperature = solid_get_temperature;
      t0 = solid_get_t0(medium);
      break;
    default: FATAL("Unreachable code\n"); break;
  }

  dX(set)(rwalk.vtx.P, position);
  rwalk.vtx.time = time;
  if(t0 >= rwalk.vtx.time) {
    double tmp;
    /* Check the initial condition. */
    rwalk.vtx.time = t0;
    tmp = get_initial_temperature(medium, &rwalk.vtx);
    if(tmp >= 0) {
      *weight = tmp;
      return RES_OK;
    }
    /* The initial condition should have been reached */
    log_err(scn->dev,
      "%s: undefined initial condition. "
      "The time is %f but the temperature remains unknown.\n",
      FUNC_NAME, t0);
    return RES_BAD_OP;
  }

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

res_T
XD(boundary_realisation)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const size_t iprim,
   const double uv[2],
   const double time,
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
  ASSERT(uv && fp_to_meter > 0 && weight && Tref >= 0 && time >= 0);

  T.func = XD(boundary_path);
  rwalk.hit_side = side;
  rwalk.hit.distance = 0;
  rwalk.vtx.time = time;
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

res_T
XD(boundary_flux_realisation)
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

  #define RESET_WALK(Side, Mdm) {                                              \
    rwalk = XD(RWALK_NULL);                                                    \
    rwalk.hit_side = (Side);                                                   \
    rwalk.hit.distance = 0;                                                    \
    rwalk.vtx.time = time;                                                     \
    rwalk.mdm = (Mdm);                                                         \
    rwalk.hit.prim = prim;                                                     \
    SET_PARAM(rwalk.hit, st);                                                  \
    ctx.Tarad = Tarad;                                                         \
    ctx.Tref3 = Tr3;                                                           \
    dX(set)(rwalk.vtx.P, P);                                                   \
    fX(set)(rwalk.hit.normal, N);                                              \
    T = XD(TEMPERATURE_NULL);                                                  \
  } (void)0

  /* Compute boundary temperature */
  RESET_WALK(solid_side, NULL);
  T.func = XD(boundary_path);
  res = XD(compute_temperature)(scn, fp_to_meter, &ctx, &rwalk, rng, &T);
  if(res != RES_OK) return res;
  weight[0] = T.value;

  /* Compute radiative temperature */
  if(compute_radiative) {
    RESET_WALK(fluid_side, NULL);
    T.func = XD(radiative_path);
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
    T.func = XD(convective_path);
    res = XD(compute_temperature)(scn, fp_to_meter, &ctx, &rwalk, rng, &T);
    if(res != RES_OK) return res;
    weight[2] = T.value;
  }

  #undef SET_PARAM
  #undef RESET_WALK

  return RES_OK;
}

#include "sdis_Xd_end.h"
