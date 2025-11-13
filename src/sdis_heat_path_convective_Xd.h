/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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
XD(handle_known_fluid_temperature)
  (struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct sdis_medium* mdm,
   struct temperature* T)
{
  double temperature;
  int known_temperature;
  res_T res = RES_OK;
  ASSERT(ctx && rwalk && T);
  ASSERT(sdis_medium_get_type(mdm) == SDIS_FLUID);

  temperature = fluid_get_temperature(mdm, &rwalk->vtx);

  /* Check if the temperature is known */
  known_temperature = SDIS_TEMPERATURE_IS_KNOWN(temperature);
  if(!known_temperature) goto exit;

  T->value += temperature;
  T->done = 1;

  if(ctx->green_path) {
    res = green_path_set_limit_vertex
      (ctx->green_path, mdm, &rwalk->vtx, rwalk->elapsed_time);
    if(res != RES_OK) goto error;
  }

  if(ctx->heat_path) {
    heat_path_get_last_vertex(ctx->heat_path)->weight = T->value;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(handle_convective_path_startup)
  (struct sdis_scene* scn,
   struct rwalk* rwalk,
   int* path_starts_in_fluid)
{
  const float range[2] = {FLT_MIN, FLT_MAX};
  float dir[DIM] = {0};
  float org[DIM] = {0};
  res_T res = RES_OK;
  ASSERT(scn && rwalk && path_starts_in_fluid);

  *path_starts_in_fluid = SXD_HIT_NONE(&rwalk->XD(hit));
  if(*path_starts_in_fluid == 0) goto exit; /* Nothing to do */

  dir[DIM-1] = 1;
  fX_set_dX(org, rwalk->vtx.P);

  /* Init the path hit field required to define the current enclosure and
   * fetch the interface data */
  SXD(scene_view_trace_ray(scn->sXd(view), org, dir, range, NULL, &rwalk->XD(hit)));
  if(SXD_HIT_NONE(&rwalk->XD(hit))) {
    log_err(scn->dev,
      "%s: the position %g %g %g lies in the surrounding fluid whose "
      "temperature must be known.\n", FUNC_NAME, SPLIT3(rwalk->vtx.P));
    res = RES_BAD_OP;
    goto error;
  }

  rwalk->hit_side = fX(dot)(rwalk->XD(hit).normal, dir) < 0
    ? SDIS_FRONT : SDIS_BACK;

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(check_enclosure)
  (struct sdis_scene* scn,
   const struct rwalk* rwalk,
   const struct enclosure* enc)
{
  res_T res = RES_OK;
  ASSERT(scn && rwalk && enc);

  if(enc->medium_id == MEDIUM_ID_MULTI) {
    /* The enclosures with multiple media are used to describe limit
     * conditions and therefore they cannot be fetched */
    log_err(scn->dev,
      "%s: enclosure with multiple media at ("FORMAT_VECX"). "
      "The path should be reached a limit condition before.\n",
      FUNC_NAME, SPLITX(rwalk->vtx.P));
      res = RES_BAD_ARG;
    goto error;
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
XD(convective_path)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  /* Properties */
  struct fluid_props props_ref = FLUID_PROPS_NULL;
  const struct sdis_interface* interf = NULL;
  struct sdis_medium* mdm = NULL;

  /* Enclosure */
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
  const struct enclosure* enc = NULL;

  /* Miscellaneous */
  struct sXd(attrib) attr_P, attr_N;
  struct sXd(hit)* rwalk_hit = NULL;
  double r;
#if SDIS_XD_DIMENSION == 2
  float st;
#else
  float st[2];
#endif
  int path_starts_in_fluid;
  res_T res = RES_OK;

  ASSERT(scn && ctx && rwalk && rng && T);
  (void)rng, (void)ctx; /* Avoid "unsued variable" warnings */

  rwalk_hit = &rwalk->XD(hit);

  /* Get the enclosure medium */
  enc = scene_get_enclosure(scn, rwalk->enc_id);
  if((res = XD(check_enclosure)(scn, rwalk, enc)) != RES_OK) goto error;
  if((res = scene_get_enclosure_medium(scn, enc, &mdm)) != RES_OK) goto error;
  ASSERT(sdis_medium_get_type(mdm) == SDIS_FLUID);

  res = XD(handle_known_fluid_temperature)(ctx, rwalk, mdm, T);
  if(res != RES_OK) goto error;
  if(T->done) goto exit; /* The fluid temperature is known */

  /* Setup the missing random walk member variables when the convective path
   * starts from the fluid */
  res = XD(handle_convective_path_startup)(scn, rwalk, &path_starts_in_fluid);
  if(res != RES_OK) goto error;

  /* Retrieve the fluid properties at the current position. Use them to verify
   * that those that are supposed to be constant by the convective random walk
   * remain the same. */
  res = fluid_get_properties(mdm, &rwalk->vtx, &props_ref);
  if(res != RES_OK) goto error;

  /* The hc upper bound can be 0 if h is uniformly 0. In that case the result
   * is the initial condition. */
  if(enc->hc_upper_bound == 0) {
    ASSERT(path_starts_in_fluid); /* Cannot be in the fluid without starting there. */
    rwalk->vtx.time = props_ref.t0;
    res = XD(handle_known_fluid_temperature)(ctx, rwalk, mdm, T);
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
    res = fluid_get_properties(mdm, &rwalk->vtx, &props);
    if(res != RES_OK) goto error;

    res = check_fluid_constant_properties(scn->dev, &props_ref, &props);
    if(res != RES_OK) goto error;

    /* Sample the time using the upper bound. */
    mu = enc->hc_upper_bound / (props.rho * props.cp) * enc->S_over_V;
    res = time_rewind(scn, mu, props.t0, rng, rwalk, ctx, T);
    if(res != RES_OK) goto error;
    if(T->done) break; /* Limit condition was reached */

    /* Uniformly sample the enclosure. */
#if DIM == 2
    SXD(scene_view_sample
      (enc->sXd(view),
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, &rwalk_hit->u));
    st = rwalk_hit->u;
#else
    SXD(scene_view_sample
      (enc->sXd(view),
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, rwalk_hit->uv));
    f2_set(st, rwalk_hit->uv);
#endif
    /* Map the sampled primitive id from the enclosure space to the scene
     * space. Note that the overall scene has only one shape. As a consequence
     * neither the geom_id nor the inst_id needs to be updated */
    rwalk_hit->prim.prim_id = enclosure_local2global_prim_id(enc, prim.prim_id);

    SXD(primitive_get_attrib(&rwalk_hit->prim, SXD_POSITION, st, &attr_P));
    SXD(primitive_get_attrib(&rwalk_hit->prim, SXD_GEOMETRY_NORMAL, st, &attr_N));
    dX_set_fX(rwalk->vtx.P, attr_P.value);
    fX(set)(rwalk_hit->normal, attr_N.value);

    /* Define the interface side */
    scene_get_enclosure_ids(scn, rwalk_hit->prim.prim_id, enc_ids);
    if(rwalk->enc_id == enc_ids[SDIS_BACK]) {
      rwalk->hit_side = SDIS_BACK;
    } else if(rwalk->enc_id == enc_ids[SDIS_FRONT]) {
      rwalk->hit_side = SDIS_FRONT;
    } else {
      FATAL("Unexpected fluid interface\n");
    }

    /* Get the interface properties */
    interf = scene_get_interface(scn, rwalk_hit->prim.prim_id);

    /* Register the new vertex against the heat path */
    res = register_heat_vertex(ctx->heat_path, &rwalk->vtx, T->value,
      SDIS_HEAT_VERTEX_CONVECTION, (int)ctx->nbranchings);
    if(res != RES_OK) goto error;

    /* Setup the fragment of the sampled position into the enclosure. */
    XD(setup_interface_fragment)(&frag, &rwalk->vtx, rwalk_hit, rwalk->hit_side);

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

  rwalk_hit->distance = 0;
  T->func = XD(boundary_path);
  rwalk->enc_id = ENCLOSURE_ID_NULL; /* Interface between 2 enclosures */

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
