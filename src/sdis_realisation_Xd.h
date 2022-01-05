/* Copyright (C) 2016-2022 |Meso|Star> (contact@meso-star.com)
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
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_scene_c.h"

#include <rsys/stretchy_array.h>
#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Non generic helper functions
 ******************************************************************************/
#ifndef SDIS_REALISATION_XD_H
#define SDIS_REALISATION_XD_H

static INLINE int
check_probe_realisation_args(const struct probe_realisation_args* args)
{
  return args
      && args->rng
      && args->medium
      && args->time >= 0
      && args->picard_order > 0;
}

static INLINE int
check_boundary_realisation_args(const struct boundary_realisation_args* args)
{
  return args
      && args->rng
      && args->uv[0] >= 0
      && args->uv[0] <= 1
      && args->uv[1] >= 0
      && args->uv[1] <= 1
      && args->time >= 0
      && args->picard_order > 0
      && (args->side == SDIS_FRONT || args->side == SDIS_BACK);
}

static INLINE int
check_boundary_flux_realisation_args
  (const struct boundary_flux_realisation_args* args)
{
  return args
      && args->rng
      && args->uv[0] >= 0
      && args->uv[0] <= 1
      && args->uv[1] >= 0
      && args->uv[1] <= 1
      && args->time >= 0
      && args->picard_order > 0
      && (args->solid_side == SDIS_FRONT || args->solid_side == SDIS_BACK);
}
#endif /* SDIS_REALISATION_XD_H */

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(compute_temperature)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
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
  struct sdis_heat_vertex* heat_vtx = NULL;
  /* Maximum accepted #failures before stopping the realisation */
  const size_t MAX_FAILS = 1;
  res_T res = RES_OK;
  ASSERT(scn && ctx && rwalk && rng && T);

  ctx->nbranchings += 1;
  CHK(ctx->nbranchings <= ctx->max_branchings);

  if(ctx->heat_path && T->func == XD(boundary_path)) {
    heat_vtx = heat_path_get_last_vertex(ctx->heat_path);
  }

  while(!T->done) {
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
      res = T->func(scn, ctx, rwalk, rng, T);
      if(res == RES_BAD_OP) { *rwalk = rwalk_bkp; *T = T_bkp; }
    } while(res == RES_BAD_OP && ++nfails < MAX_FAILS);
    if(res != RES_OK) goto error;

    /* Update the type of the first vertex of the random walks that begin on a
     * boundary. Indeed, one knows the "right" type of the first vertex only
     * after the boundary_path execution that defines the sub path to resolve
     * from the submitted boundary position. Note that if the boundary
     * temperature is known, the type is let as it. */
    if(heat_vtx && !T->done && T->func != XD(boundary_path)) {
      heat_vtx = heat_path_get_last_vertex(ctx->heat_path);
      if(T->func == XD(conductive_path)) {
        heat_vtx->type = SDIS_HEAT_VERTEX_CONDUCTION;
      } else if(T->func == XD(convective_path)) {
        heat_vtx->type = SDIS_HEAT_VERTEX_CONVECTION;
      } else if(T->func == XD(radiative_path)) {
        heat_vtx->type = SDIS_HEAT_VERTEX_RADIATIVE;
      } else {
        FATAL("Unreachable code.\n");
      }
      heat_vtx = NULL; /* Notify that the first vertex is finalized */
    }
  }


exit:
#ifndef NDEBUG
  sa_release(stack);
#endif
  ctx->nbranchings -= 1;
  return res == RES_BAD_OP_IRRECOVERABLE ? RES_BAD_OP : res;
error:
  goto exit;
}

res_T
XD(probe_realisation)
  (struct sdis_scene* scn,
   struct probe_realisation_args* args,
   double* weight)
{
  struct rwalk_context ctx = RWALK_CONTEXT_NULL;
  struct XD(rwalk) rwalk = XD(RWALK_NULL);
  struct XD(temperature) T = XD(TEMPERATURE_NULL);
  enum sdis_heat_vertex_type type;
  double t0;
  double (*get_initial_temperature)
    (const struct sdis_medium* mdm,
     const struct sdis_rwalk_vertex* vtx);
  res_T res = RES_OK;
  ASSERT(scn && weight && check_probe_realisation_args(args));

  switch(args->medium->type) {
    case SDIS_FLUID:
      T.func = XD(convective_path);
      get_initial_temperature = fluid_get_temperature;
      t0 = fluid_get_t0(args->medium);
      break;
    case SDIS_SOLID:
      T.func = XD(conductive_path);
      get_initial_temperature = solid_get_temperature;
      t0 = solid_get_t0(args->medium);
      break;
    default: FATAL("Unreachable code\n"); break;
  }

  dX(set)(rwalk.vtx.P, args->position);
  rwalk.vtx.time = args->time;

  /* Register the starting position against the heat path */
  type = args->medium->type == SDIS_SOLID
    ? SDIS_HEAT_VERTEX_CONDUCTION
    : SDIS_HEAT_VERTEX_CONVECTION;
  res = register_heat_vertex(args->heat_path, &rwalk.vtx, 0, type);
  if(res != RES_OK) goto error;

  if(t0 >= rwalk.vtx.time) {
    double tmp;
    /* Check the initial condition. */
    rwalk.vtx.time = t0;
    tmp = get_initial_temperature(args->medium, &rwalk.vtx);
    if(tmp >= 0) {
      *weight = tmp;
      goto exit;
    }
    /* The initial condition should have been reached */
    log_err(scn->dev,
      "%s: undefined initial condition. "
      "The time is %g but the temperature remains unknown.\n",
      FUNC_NAME, t0);
    res = RES_BAD_OP;
    goto error;
  }

  rwalk.hit = SXD_HIT_NULL;
  rwalk.mdm = args->medium;

  ctx.green_path = args->green_path;
  ctx.heat_path = args->heat_path;
  ctx.Tmin  = scn->tmin;
  ctx.Tmin2 = ctx.Tmin * ctx.Tmin;
  ctx.Tmin3 = ctx.Tmin * ctx.Tmin2;
  ctx.That  = scn->tmax;
  ctx.That2 = ctx.That * ctx.That;
  ctx.That3 = ctx.That * ctx.That2;
  ctx.max_branchings = args->picard_order - 1;

  res = XD(compute_temperature)(scn, &ctx, &rwalk, args->rng, &T);
  if(res != RES_OK) goto error;

  ASSERT(T.value >= 0);
  *weight = T.value;

exit:
  return res;
error:
  goto exit;
}

res_T
XD(boundary_realisation)
  (struct sdis_scene* scn,
   struct boundary_realisation_args* args,
   double* weight)
{
  struct rwalk_context ctx = RWALK_CONTEXT_NULL;
  struct XD(rwalk) rwalk = XD(RWALK_NULL);
  struct XD(temperature) T = XD(TEMPERATURE_NULL);
  struct sXd(attrib) attr;
#if SDIS_XD_DIMENSION == 2
  float st;
#else
  float st[2];
#endif
  res_T res = RES_OK;
  ASSERT(scn && weight && check_boundary_realisation_args(args));

  T.func = XD(boundary_path);
  rwalk.hit_side = args->side;
  rwalk.hit.distance = 0;
  rwalk.vtx.time = args->time;
  rwalk.mdm = NULL; /* The random walk is at an interface between 2 media */

#if SDIS_XD_DIMENSION == 2
  st = (float)args->uv[0];
#else
  f2_set_d2(st, args->uv);
#endif

  /* Fetch the primitive */
  SXD(scene_view_get_primitive
    (scn->sXd(view), (unsigned int)args->iprim, &rwalk.hit.prim));

  /* Retrieve the world space position of the probe onto the primitive */
  SXD(primitive_get_attrib(&rwalk.hit.prim, SXD_POSITION, st, &attr));
  dX_set_fX(rwalk.vtx.P, attr.value);

  /* Retrieve the primitive normal */
  SXD(primitive_get_attrib(&rwalk.hit.prim, SXD_GEOMETRY_NORMAL, st, &attr));
  fX(set)(rwalk.hit.normal, attr.value);

#if SDIS_XD_DIMENSION==2
  rwalk.hit.u = st;
#else
  f2_set(rwalk.hit.uv, st);
#endif

  res = register_heat_vertex(args->heat_path, &rwalk.vtx, 0/*weight*/,
    SDIS_HEAT_VERTEX_CONDUCTION);
  if(res != RES_OK) goto error;

  ctx.green_path = args->green_path;
  ctx.heat_path = args->heat_path;
  ctx.Tmin  = scn->tmin;
  ctx.Tmin2 = ctx.Tmin * ctx.Tmin;
  ctx.Tmin3 = ctx.Tmin * ctx.Tmin2;
  ctx.That  = scn->tmax;
  ctx.That2 = ctx.That * ctx.That;
  ctx.That3 = ctx.That * ctx.That2;
  ctx.max_branchings = args->picard_order - 1;

  res = XD(compute_temperature)(scn, &ctx, &rwalk, args->rng, &T);
  if(res != RES_OK) goto error;

  *weight = T.value;

exit:
  return res;
error:
  goto exit;
}

res_T
XD(boundary_flux_realisation)
  (struct sdis_scene* scn,
   struct boundary_flux_realisation_args* args,
   struct bound_flux_result* result)
{
  struct rwalk_context ctx = RWALK_CONTEXT_NULL;
  struct XD(rwalk) rwalk;
  struct XD(temperature) T;
  struct sXd(attrib) attr;
  struct sXd(primitive) prim;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* fluid_mdm = NULL;

#if SDIS_XD_DIMENSION == 2
  float st;
#else
  float st[2];
#endif
  double P[SDIS_XD_DIMENSION];
  float N[SDIS_XD_DIMENSION];
  double Tmin, Tmin2, Tmin3;
  double That, That2, That3;
  enum sdis_side fluid_side;
  res_T res = RES_OK;
  char compute_radiative;
  char compute_convective;
  ASSERT(scn && result && check_boundary_flux_realisation_args(args));

#if SDIS_XD_DIMENSION == 2
  #define SET_PARAM(Dest, Src) (Dest).u = (Src);
  st = (float)args->uv[0];
#else
  #define SET_PARAM(Dest, Src) f2_set((Dest).uv, (Src));
  f2_set_d2(st, args->uv);
#endif

  Tmin = scn->tmin;
  Tmin2 = Tmin * Tmin;
  Tmin3 = Tmin * Tmin2;
  That = scn->tmax;
  That2 = That * That;
  That3 = That * That2;

  fluid_side = (args->solid_side/*solid*/==SDIS_FRONT) ? SDIS_BACK : SDIS_FRONT;

  compute_radiative = (args->flux_mask & FLUX_FLAG_RADIATIVE) != 0;
  compute_convective = (args->flux_mask & FLUX_FLAG_CONVECTIVE) != 0;

  /* Fetch the primitive */
  SXD(scene_view_get_primitive(scn->sXd(view), (unsigned int)args->iprim, &prim));

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
    rwalk.vtx.time = args->time;                                               \
    rwalk.mdm = (Mdm);                                                         \
    rwalk.hit.prim = prim;                                                     \
    SET_PARAM(rwalk.hit, st);                                                  \
    ctx.Tmin  = Tmin;                                                          \
    ctx.Tmin3 = Tmin3;                                                         \
    ctx.That  = That;                                                          \
    ctx.That2 = That2;                                                         \
    ctx.That3 = That3;                                                         \
    ctx.max_branchings = args->picard_order - 1;                               \
    dX(set)(rwalk.vtx.P, P);                                                   \
    fX(set)(rwalk.hit.normal, N);                                              \
    T = XD(TEMPERATURE_NULL);                                                  \
  } (void)0

  /* Compute boundary temperature */
  RESET_WALK(args->solid_side, NULL);
  T.func = XD(boundary_path);
  res = XD(compute_temperature)(scn, &ctx, &rwalk, args->rng, &T);
  if(res != RES_OK) return res;
  result->Tboundary = T.value;

  /* Fetch the fluid medium */
  interf = scene_get_interface(scn, (unsigned)args->iprim);
  fluid_mdm = interface_get_medium(interf, fluid_side);

  /* Compute radiative temperature */
  if(compute_radiative) {
    RESET_WALK(fluid_side, fluid_mdm);
    T.func = XD(radiative_path);
    res = XD(compute_temperature)(scn, &ctx, &rwalk, args->rng, &T);
    if(res != RES_OK) return res;
    ASSERT(T.value >= 0);
    result->Tradiative = T.value;
  }

  /* Compute fluid temperature */
  if(compute_convective) {
    RESET_WALK(fluid_side, fluid_mdm);
    T.func = XD(convective_path);
    res = XD(compute_temperature)(scn, &ctx, &rwalk, args->rng, &T);
    if(res != RES_OK) return res;
    result->Tfluid = T.value;
  }

  #undef SET_PARAM
  #undef RESET_WALK

  return RES_OK;
}

#include "sdis_Xd_end.h"
