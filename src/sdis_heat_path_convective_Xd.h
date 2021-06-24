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

#include "sdis_device_c.h"
#include "sdis_green.h"
#include "sdis_heat_path.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
XD(register_heat_vertex_in_fluid)
  (struct sdis_scene* scn,
   const struct rwalk_context* ctx,
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

  return register_heat_vertex
    (ctx->heat_path, &vtx, weight, SDIS_HEAT_VERTEX_CONVECTION);
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(convective_path)
  (struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  struct sXd(attrib) attr_P, attr_N;
  const struct sdis_interface* interf;
  const struct enclosure* enc;
  unsigned enc_ids[2];
  unsigned enc_id;
  double rho; /* Volumic mass */
  double hc; /* Convection coef */
  double cp; /* Calorific capacity */
  double tmp;
  double r;
  int path_started_in_fluid;
#if SDIS_XD_DIMENSION == 2
  float st;
#else
  float st[2];
#endif
  res_T res = RES_OK;
  (void)rng, (void)ctx;
  ASSERT(scn && ctx && rwalk && rng && T);
  ASSERT(rwalk->mdm->type == SDIS_FLUID);

  tmp = fluid_get_temperature(rwalk->mdm, &rwalk->vtx);
  if(tmp >= 0) { /* T is known. */
    T->value += tmp;
    T->done = 1;

    if(ctx->green_path) {
      res = green_path_set_limit_vertex
        (ctx->green_path, rwalk->mdm, &rwalk->vtx, rwalk->elapsed_time);
      if(res != RES_OK) goto error;
    }

    res = XD(register_heat_vertex_in_fluid)(scn, ctx, rwalk, T->value);
    if(res != RES_OK) goto error;

    goto exit;
  }

  path_started_in_fluid = SXD_HIT_NONE(&rwalk->hit);
  if(path_started_in_fluid) { /* The path begins in the fluid */
    const float range[2] = {0, FLT_MAX};
    float dir[DIM] = {0};
    float org[DIM];

    dir[DIM-1] = 1;
    fX_set_dX(org, rwalk->vtx.P);

    /* Init the path hit field required to define the current enclosure and
     * fetch the interface data */
    SXD(scene_view_trace_ray(scn->sXd(view), org, dir, range, NULL, &rwalk->hit));

    if(SXD_HIT_NONE(&rwalk->hit)) {
      log_err(scn->dev,
"%s: the position %g %g %g lies in the surrounding fluid whose temperature must \n"
"be known.\n", FUNC_NAME, SPLIT3(rwalk->vtx.P));
      res = RES_BAD_OP;
      goto error;
    }

    rwalk->hit_side = fX(dot)(rwalk->hit.normal, dir) < 0 ? SDIS_FRONT : SDIS_BACK;
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
    res = RES_BAD_ARG;
    goto error;
  }

  /* The hc upper bound can be 0 if h is uniformly 0. In that case the result
   * is the initial condition. */
  if(enc->hc_upper_bound == 0) {
    /* Cannot be in the fluid without starting there. */
    ASSERT(path_started_in_fluid);

    rwalk->vtx.time = fluid_get_t0(rwalk->mdm);
    tmp = fluid_get_temperature(rwalk->mdm, &rwalk->vtx);
    if(tmp >= 0) {
      T->value += tmp;
      T->done = 1;
      goto exit;
    }

    /* At t=t0, the initial condition should have been reached. */
    log_err(scn->dev,
      "%s: undefined initial condition. "
      "Time is %g but the temperature remains unknown.\n",
      FUNC_NAME, rwalk->vtx.time);
    res = RES_BAD_OP;
    goto error;
  }

  /* Sample time until init condition is reached or a true convection occurs. */
  for(;;) {
    struct sdis_interface_fragment frag;
    struct sXd(primitive) prim;
    double mu, tau, t0;

    /* Fetch other physical properties. */
    cp = fluid_get_calorific_capacity(rwalk->mdm, &rwalk->vtx);
    rho = fluid_get_volumic_mass(rwalk->mdm, &rwalk->vtx);
    t0 = fluid_get_t0(rwalk->mdm); /* Limit time */

    /* Sample the time using the upper bound. */
    mu = enc->hc_upper_bound / (rho * cp) * enc->S_over_V;
    tau = ssp_ran_exp(rng, mu);

    /* Increment the elapsed time */
    ASSERT(rwalk->vtx.time > t0);
    rwalk->elapsed_time += MMIN(tau, rwalk->vtx.time - t0);

    if(rwalk->vtx.time != INF) {
      rwalk->vtx.time = MMAX(rwalk->vtx.time - tau, t0); /* Time rewind */

      /* Register the new vertex against the heat path */
      res = XD(register_heat_vertex_in_fluid)(scn, ctx, rwalk, T->value);
      if(res != RES_OK) goto error;

      if(rwalk->vtx.time == t0) {
        /* Check the initial condition. */
        tmp = fluid_get_temperature(rwalk->mdm, &rwalk->vtx);
        if(tmp >= 0) {
          T->value += tmp;
          T->done = 1;
          if(ctx->heat_path) { /* Update the weight of the last heat vertex */
            heat_path_get_last_vertex(ctx->heat_path)->weight = T->value;
          }
          goto exit;
        }
        /* The initial condition should have been reached. */
        log_err(scn->dev,
          "%s: undefined initial condition. "
          "Time is %g but the temperature remains unknown.\n",
          FUNC_NAME, t0);
        res = RES_BAD_OP;
        goto error;
      }
    }

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
    res = register_heat_vertex
      (ctx->heat_path, &rwalk->vtx, T->value, SDIS_HEAT_VERTEX_CONVECTION);
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
