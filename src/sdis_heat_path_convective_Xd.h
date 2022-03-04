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
#include "sdis_green.h"
#include "sdis_heat_path.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Non generic helper functions
 ******************************************************************************/
#ifndef SDIS_HEAT_PATH_CONVECTIVE_XD_H
#define SDIS_HEAT_PATH_CONVECTIVE_XD_H

static res_T
check_fluid_constant_properties
  (struct sdis_device* dev,
   const struct fluid_props* props_ref,
   const struct fluid_props* props)
{
  res_T res = RES_OK;
  ASSERT(dev && props_ref && props);

  if(props_ref->rho != props->rho) {
    log_err(dev,
      "%s: invalid volumic mass. One assumes a constant volumic mass for "
      "the whole fluid.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  if(props_ref->cp != props->cp) {
    log_err(dev,
       "%s: invalid calorific capacity. One assumes a constant calorific "
       "capacity for the whole fluid.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#endif /* SDIS_HEAT_PATH_CONVECTIVE_XD_H */

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
XD(register_heat_vertex_in_fluid)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   const double weight)
{
  struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;
  struct hit_filter_data filter_data;
  const float empirical_dst = 0.1f;
  const float range[2] = {0, FLT_MAX};
  float org[DIM];
  float dir[DIM];
  float pos[DIM];
  float dst;
  struct sXd(hit) hit;

  if(!ctx->heat_path) return RES_OK;

  ASSERT(!SXD_HIT_NONE(&rwalk->hit));

  fX_set_dX(org, rwalk->vtx.P);
  fX(set)(dir, rwalk->hit.normal);
  if(rwalk->hit_side == SDIS_BACK) fX(minus)(dir, dir);

  filter_data.XD(hit) = rwalk->hit;
  filter_data.epsilon = 1.e-6;
  SXD(scene_view_trace_ray(scn->sXd(view), org, dir, range, &filter_data, &hit));
  dst = SXD_HIT_NONE(&hit) ? empirical_dst : hit.distance * 0.5f;

  vtx = rwalk->vtx;
  fX(add)(pos, org, fX(mulf)(dir, dir, dst));
  dX_set_fX(vtx.P, pos);

  return register_heat_vertex(ctx->heat_path, &vtx, weight,
    SDIS_HEAT_VERTEX_CONVECTION, (int)ctx->nbranchings);
}

static res_T
XD(handle_known_fluid_temperature)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct XD(temperature)* T)
{
  double temperature;
  int known_temperature;
  res_T res = RES_OK;
  ASSERT(scn && ctx && rwalk && T);
  ASSERT(sdis_medium_get_type(rwalk->mdm) == SDIS_FLUID);

  temperature = fluid_get_temperature(rwalk->mdm, &rwalk->vtx);

  /* Check if the temperature is known */
  known_temperature = temperature >= 0;
  if(!known_temperature) goto exit;

  T->value += temperature;
  T->done = 1;

  if(ctx->green_path) {
    res = green_path_set_limit_vertex
      (ctx->green_path, rwalk->mdm, &rwalk->vtx, rwalk->elapsed_time);
    if(res != RES_OK) goto error;
  }

  res = XD(register_heat_vertex_in_fluid)(scn, ctx, rwalk, T->value);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(handle_convective_path_startup)
  (struct sdis_scene* scn,
   struct XD(rwalk)* rwalk,
   int* path_starts_in_fluid)
{
  const float range[2] = {FLT_MIN, FLT_MAX};
  float dir[DIM] = {0};
  float org[DIM] = {0};
  res_T res = RES_OK;
  ASSERT(scn && rwalk && path_starts_in_fluid);
  ASSERT(sdis_medium_get_type(rwalk->mdm) == SDIS_FLUID);

  *path_starts_in_fluid = SXD_HIT_NONE(&rwalk->hit);
  if(*path_starts_in_fluid == 0) goto exit; /* Nothing to do */

  dir[DIM-1] = 1;
  fX_set_dX(org, rwalk->vtx.P);

  /* Init the path hit field required to define the current enclosure and
   * fetch the interface data */
  SXD(scene_view_trace_ray(scn->sXd(view), org, dir, range, NULL, &rwalk->hit));
  if(SXD_HIT_NONE(&rwalk->hit)) {
    log_err(scn->dev,
      "%s: the position %g %g %g lies in the surrounding fluid whose "
      "temperature must be known.\n", FUNC_NAME, SPLIT3(rwalk->vtx.P));
    res = RES_BAD_OP;
    goto error;
  }

  rwalk->hit_side = fX(dot)(rwalk->hit.normal, dir) < 0 ? SDIS_FRONT : SDIS_BACK;

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(fetch_fluid_enclosure)
  (struct sdis_scene* scn,
   struct XD(rwalk)* rwalk,
   const struct enclosure** out_enclosure)
{
  const struct sdis_interface* interf;
  const struct enclosure* enc;
  unsigned enc_ids[2];
  unsigned enc_id;
  res_T res = RES_OK;
  ASSERT(scn && rwalk && out_enclosure);
  ASSERT(sdis_medium_get_type(rwalk->mdm) == SDIS_FLUID);
  ASSERT(!SXD_HIT_NONE(&rwalk->hit));

  /* Fetch the current interface and its associated enclosures */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
  scene_get_enclosure_ids(scn, rwalk->hit.prim.prim_id, enc_ids);

  /* Find the enclosure identifier of the current medium */
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
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  *out_enclosure = enc;
  return res;
error:
  enc = NULL;
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(convective_path)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  struct sXd(attrib) attr_P, attr_N;
  struct fluid_props props_ref = FLUID_PROPS_NULL;
  const struct sdis_interface* interf;
  const struct enclosure* enc;
  double r;
#if SDIS_XD_DIMENSION == 2
  float st;
#else
  float st[2];
#endif
  int path_starts_in_fluid;
  res_T res = RES_OK;
  (void)rng, (void)ctx;
  ASSERT(scn && ctx && rwalk && rng && T);
  ASSERT(rwalk->mdm->type == SDIS_FLUID);

  res = XD(handle_known_fluid_temperature)(scn, ctx, rwalk, T);
  if(res != RES_OK) goto error;
  if(T->done) goto exit; /* The fluid temperature is known */

  /* Setup the missing random walk member variables when the convective path
   * starts from the fluid */
  res = XD(handle_convective_path_startup)(scn, rwalk, &path_starts_in_fluid);
  if(res != RES_OK) goto error;

  res = XD(fetch_fluid_enclosure)(scn, rwalk, &enc);
  if(res != RES_OK) goto error;

  /* Retrieve the fluid properties at the current position. Use them to verify
   * that those that are supposed to be constant by the convective random walk
   * remain the same. */
  res = fluid_get_properties(rwalk->mdm, &rwalk->vtx, &props_ref);
  if(res != RES_OK) goto error;

  /* The hc upper bound can be 0 if h is uniformly 0. In that case the result
   * is the initial condition. */
  if(enc->hc_upper_bound == 0) {
    ASSERT(path_starts_in_fluid); /* Cannot be in the fluid without starting there. */
    rwalk->vtx.time = props_ref.t0;
    res = XD(handle_known_fluid_temperature)(scn, ctx, rwalk, T);
    if(res != RES_OK) goto error;
    if(T->done) {
      goto exit; /* Stop the random walk */
    } else {
      log_err(scn->dev, "%s: undefined initial condition.", FUNC_NAME);
      res = RES_BAD_OP;
      goto error;
    }
  }

  /* Sample time until init condition is reached or a true convection occurs. */
  for(;;) {
    struct sdis_interface_fragment frag;
    struct sXd(primitive) prim;
    struct fluid_props props = FLUID_PROPS_NULL;
    double hc;
    double mu;

    /* Fetch fluid properties */
    res = fluid_get_properties(rwalk->mdm, &rwalk->vtx, &props);
    if(res != RES_OK) goto error;

    res = check_fluid_constant_properties(scn->dev, &props_ref, &props);
    if(res != RES_OK) goto error;

    /* Sample the time using the upper bound. */
    mu = enc->hc_upper_bound / (props.rho * props.cp) * enc->S_over_V;
    res = XD(time_rewind)(mu, props.t0, rng, rwalk, ctx, T);
    if(res != RES_OK) goto error;
    if(T->done) break; /* Limit condition was reached */

    /* Uniformly sample the enclosure. */
#if DIM == 2
    SXD(scene_view_sample
      (enc->sXd(view),
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, &rwalk->hit.u));
    st = rwalk->hit.u;
#else
    SXD(scene_view_sample
      (enc->sXd(view),
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, rwalk->hit.uv));
    f2_set(st, rwalk->hit.uv);
#endif
    /* Map the sampled primitive id from the enclosure space to the scene
     * space. Note that the overall scene has only one shape. As a consequence
     * neither the geom_id nor the inst_id needs to be updated */
    rwalk->hit.prim.prim_id = enclosure_local2global_prim_id(enc, prim.prim_id);

    SXD(primitive_get_attrib(&rwalk->hit.prim, SXD_POSITION, st, &attr_P));
    SXD(primitive_get_attrib(&rwalk->hit.prim, SXD_GEOMETRY_NORMAL, st, &attr_N));
    dX_set_fX(rwalk->vtx.P, attr_P.value);
    fX(set)(rwalk->hit.normal, attr_N.value);

    /* Fetch the interface of the sampled point. */
    interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);
    if(rwalk->mdm == interf->medium_front) {
      rwalk->hit_side = SDIS_FRONT;
    } else if(rwalk->mdm == interf->medium_back) {
      rwalk->hit_side = SDIS_BACK;
    } else {
      FATAL("Unexpected fluid interface.\n");
    }

    /* Register the new vertex against the heat path */
    res = register_heat_vertex(ctx->heat_path, &rwalk->vtx, T->value,
      SDIS_HEAT_VERTEX_CONVECTION, (int)ctx->nbranchings);
    if(res != RES_OK) goto error;

    /* Setup the fragment of the sampled position into the enclosure. */
    XD(setup_interface_fragment)(&frag, &rwalk->vtx, &rwalk->hit, rwalk->hit_side);

    /* Fetch the convection coefficient of the sampled position */
    hc = interface_get_convection_coef(interf, &frag);
    if(hc > enc->hc_upper_bound) {
      log_err(scn->dev,
        "%s: hc (%g) exceeds its provided upper bound (%g) at %g %g %g.\n",
        FUNC_NAME, hc, enc->hc_upper_bound, SPLIT3(rwalk->vtx.P));
      res = RES_BAD_OP;
      goto error;
    }

    r = ssp_rng_canonical_float(rng);
    if(r < hc / enc->hc_upper_bound) {
      /* True convection. Always true if hc == bound. */
      break;
    }
  }

  rwalk->hit.distance = 0;
  T->func = XD(boundary_path);
  rwalk->mdm = NULL; /* The random walk is at an interface between 2 media */

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
