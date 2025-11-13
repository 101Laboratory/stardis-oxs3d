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

#include "sdis_brdf.h"
#include "sdis_device_c.h"
#include "sdis_green.h"
#include "sdis_heat_path.h"
#include "sdis_heat_path_boundary_c.h"
#include "sdis_interface_c.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_radiative_env_c.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Non generic helper functions
 ******************************************************************************/
#ifndef SDIS_HEAT_PATH_RADIATIVE_XD_H
#define SDIS_HEAT_PATH_RADIATIVE_XD_H

static res_T
set_limit_radiative_temperature
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   /* Direction along which the random walk reached the radiative environment */
   const double dir[3],
   const int branch_id,
   struct temperature* T)
{
  struct sdis_radiative_ray ray = SDIS_RADIATIVE_RAY_NULL;
  double trad = 0; /* [K] */
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(scn && ctx && rwalk && dir && T);
  ASSERT(SXD_HIT_NONE(&rwalk->XD(hit)));

  rwalk->hit_side = SDIS_SIDE_NULL__;
  d3_set(rwalk->dir, dir);
  d3_normalize(rwalk->dir, rwalk->dir);
  d3_set(ray.dir, rwalk->dir);
  ray.time = rwalk->vtx.time;

  trad = radiative_env_get_temperature(scn->radenv, &ray);
  if(SDIS_TEMPERATURE_IS_UNKNOWN(trad)) {
    log_err(scn->dev,
      "%s:%s: the random walk has reached an invalid radiative environment from "
      "position (%g, %g, %g) along direction (%g, %g, %g): the temperature is "
      "unknown. This may be due to numerical inaccuracies or inconsistencies "
      "in the simulated system (e.g. non-closed geometry). For systems where "
      "sampled paths can reach such a temperature, we need to define a valid "
      "radiative temperature, i.e. one with a known temperature.\n",
      __FILE__, FUNC_NAME, SPLIT3(rwalk->vtx.P), SPLIT3(rwalk->dir));
    res = RES_BAD_OP;
    goto error;
  }

  /* The limit condition is reached */
  T->value += trad;
  T->done = 1;

  /* Update green path */
  if(ctx->green_path) {
    res = green_path_set_limit_radiative_ray
      (ctx->green_path, &ray, rwalk->elapsed_time);
    if(res != RES_OK) goto error;
  }

  /* Record the limit vertex of the sampled path. Set it arbitrarily at a
   * distance of 0.1 meters from the surface along the direction reaching the
   * radiative environment */
  if(ctx->heat_path) {
    const double empirical_dst = 0.1 * (float)scn->fp_to_meter;
    struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;

    vtx = rwalk->vtx;
    vtx.P[0] += dir[0] * empirical_dst;
    vtx.P[1] += dir[1] * empirical_dst;
    vtx.P[2] += dir[2] * empirical_dst;
    res = register_heat_vertex(ctx->heat_path, &vtx, T->value,
      SDIS_HEAT_VERTEX_RADIATIVE, branch_id);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

/* Check that the trajectory reaches a valid interface, i.e. that it is on a
 * fluid/solid interface and has reached it from the fluid */
static res_T
check_interface
  (const struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag,
   const int verbose) /* Control the verbosity of the function */
{
  enum sdis_medium_type mdm_frt_type = SDIS_MEDIUM_TYPES_COUNT__;
  enum sdis_medium_type mdm_bck_type = SDIS_MEDIUM_TYPES_COUNT__;
  enum sdis_side fluid_side = SDIS_SIDE_NULL__;
  res_T res = RES_OK;

  mdm_frt_type = sdis_medium_get_type(interf->medium_front);
  mdm_bck_type = sdis_medium_get_type(interf->medium_back);

  /* Semi-transparent materials are not supported. This means that a solid/solid
   * interface must not be intersected when tracing radiative paths */
  if(mdm_frt_type == SDIS_SOLID && mdm_bck_type == SDIS_SOLID) {
    if(verbose) {
      log_err(interf->dev,
        "Error when sampling the radiatve path. The trajectory reaches a "
        "solid/solid interface, whereas this is supposed to be impossible "
        "(path position: %g, %g, %g).\n",
      SPLIT3(frag->P));
    }
    res = RES_BAD_OP;
    goto error;
  }

  /* Find out which side of the interface the fluid is on */
  if(mdm_frt_type == SDIS_FLUID) {
    fluid_side = SDIS_FRONT;
  } else if(mdm_bck_type == SDIS_FLUID) {
    fluid_side = SDIS_BACK;
  } else {
    FATAL("Unreachable code\n");
  }

  /* Check that the current position is on the correct side of the interface */
  if(frag->side != fluid_side) {
    if(verbose) {
      log_err(interf->dev,
        "Inconsistent intersection when sampling the radiative path. "
        "The path reaches an interface on its solid side, whereas this is "
        "supposed to be impossible (path position: %g, %g, %g).\n",
        SPLIT3(frag->P));
    }
    res = RES_BAD_OP;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#endif /* SDIS_HEAT_PATH_RADIATIVE_XD_H */

/*******************************************************************************
 * Generic helper functions
 ******************************************************************************/
static INLINE void
XD(setup_fragment)
  (struct sdis_interface_fragment* frag,
   const double pos[DIM],
   const double dir[DIM], /* Direction _toward_ the hit position */
   const double time, /* Current time */
   const double N[DIM],/* Surface normal */
   const struct sXd(hit)* hit)
{
  struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;
  enum sdis_side side = SDIS_SIDE_NULL__;
  ASSERT(frag && pos && dir && N);
  ASSERT(dX(is_normalized)(N));

  /* Setup the interface fragment at the intersection position */
  dX(set)(vtx.P, pos);
  vtx.time = time;
  side = dX(dot)(dir, N) < 0 ? SDIS_FRONT : SDIS_BACK;
  XD(setup_interface_fragment)(frag, &vtx, hit, side);
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(trace_radiative_path)
  (struct sdis_scene* scn,
   const float ray_dir[3],
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  /* The radiative random walk is always performed in 3D. In 2D, the geometry
   * are assumed to be extruded to the infinity along the Z dimension. */
  double N[3] = {0,0,0};
  double dir[3] = {0,0,0};
  double pos[3] = {0,0,0};
  int branch_id;
  size_t nbounces = 0; /* For debug */
  res_T res = RES_OK;

  ASSERT(scn && ray_dir && ctx && rwalk && rng && T);

  d3_set_f3(dir, ray_dir);
  d3_normalize(dir, dir);

  /* (int)ctx->nbranchings < 0 <=> Beginning of the realisation */
  branch_id = MMAX((int)ctx->nbranchings, 0);

  /* Launch the radiative random walk */
  for(;;) {
    /* BRDF */
    struct brdf brdf = BRDF_NULL;
    struct brdf_sample bounce = BRDF_SAMPLE_NULL;
    struct brdf_setup_args brdf_setup_args = BRDF_SETUP_ARGS_NULL;

    /* Miscellaneous */
    struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
    struct sdis_interface* interf = NULL;
    struct sdis_medium* chk_mdm = NULL;
    double wi[3] = {0,0,0};

    d3_set(pos, rwalk->vtx.P);
    d3_minus(wi, dir);

    res = XD(find_next_fragment)(scn, pos, dir, &rwalk->XD(hit),
      rwalk->vtx.time, rwalk->enc_id, &rwalk->XD(hit), &interf, &frag);
    if(res != RES_OK) goto error;

    /* The path reaches the radiative environment */
    if(SXD_HIT_NONE(&rwalk->XD(hit))) {
      res = set_limit_radiative_temperature(scn, ctx, rwalk, dir, branch_id, T);
      if(res != RES_OK) goto error;

      ASSERT(T->done);
      break; /* Stop the radiative path */
    }

   /* Move the random walk to the hit position, i.e., the next position on the
    * interface returned as a fragment by the find_next_fragment function. Do
    * not use the sampled direction and distance to the hit point to
    * calculate the new position, as the current position may have been slightly
    * shifted on the starting triangle by the find_next_fragment function in
    * order to avoid numerical inaccuracy issues, making it impossible to
    * reconstruct the position actually returned by the function. The starting
    * point and distance returned are not, in any case, those used by the
    * function to calculate the new wall position. So simply use the position
    * returned by this function. */
    d3_set(rwalk->vtx.P, frag.P);
    rwalk->hit_side = frag.side;

    /* Verify that the intersection, although in the same enclosure, touches the
     * interface of a fluid. We verify this by interface, since a radiative path
     * can be traced in an enclosure containing several media used to describe a
     * set of boundary conditions.
     *
     * If the enclosure is good but the media type is not, this means that the
     * radiative path is sampled in the wrong media. This is not a numerical
     * problem, but a user problem: trying to sample a radiative path in a solid
     * when semi-transparent solids are not yet supported by Stardis. This error
     * is therefore fatal for the calculation */
    chk_mdm = rwalk->hit_side == SDIS_FRONT
      ? interf->medium_front
      : interf->medium_back;
    if(sdis_medium_get_type(chk_mdm) == SDIS_SOLID) {
      log_err(scn->dev,
        "%s: a radiative path cannot evolve in a solid -- pos=(%g, %g, %g)\n",
        FUNC_NAME, SPLIT3(rwalk->vtx.P));
      res = RES_BAD_OP_IRRECOVERABLE;
      goto error;
    }

    /* Register the random walk vertex against the heat path */
    res = register_heat_vertex(ctx->heat_path, &rwalk->vtx, T->value,
      SDIS_HEAT_VERTEX_RADIATIVE, branch_id);
    if(res != RES_OK) goto error;

    /* Retrieve BRDF at current interface position */
    brdf_setup_args.interf = interf;
    brdf_setup_args.frag = &frag;
    brdf_setup_args.source_id = SDIS_INTERN_SOURCE_ID;
    res = brdf_setup(scn->dev, &brdf_setup_args, &brdf);
    if(res != RES_OK) goto error;

    /* Switch in boundary temperature? */
    if(ssp_rng_canonical(rng) < brdf.emissivity) {
      T->func = XD(boundary_path);
      rwalk->enc_id = ENCLOSURE_ID_NULL; /* Interface between 2 enclosures */
      break;
    }

    /* Normalize the normal of the interface and ensure that it points toward the
     * current medium */
    switch(frag.side) {
      case SDIS_FRONT: d3_set(N, frag.Ng); break;
      case SDIS_BACK:  d3_minus(N, frag.Ng); break;
      default: FATAL("Unreachable code\n"); break;
    }
    brdf_sample(&brdf, rng, wi, N, &bounce);
    d3_set(dir, bounce.dir); /* Always in 3D */

    ++nbounces;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
XD(radiative_path)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  /* The radiative random walk is always performed in 3D. In 2D, the geometry
   * are assumed to be extruded to the infinity along the Z dimension. */
  float N[3] = {0, 0, 0};
  float dir[3] = {0, 0, 0};
  res_T res = RES_OK;

  ASSERT(scn && ctx && rwalk && rng && T);
  ASSERT(!SXD_HIT_NONE(&rwalk->XD(hit)));

  /* Normalize the normal of the interface and ensure that it points toward the
   * current medium */
  fX(normalize(N, rwalk->XD(hit).normal));
  if(rwalk->hit_side == SDIS_BACK) {
    fX(minus(N, N));
  }

  /* Cosine weighted sampling of a direction around the surface normal */
  ssp_ran_hemisphere_cos_float(rng, N, dir, NULL);

  /* Launch the radiative random walk */
  res = XD(trace_radiative_path)(scn, dir, ctx, rwalk, rng, T);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

void
XD(trace_ray)
  (struct sdis_scene* scn,
   const double pos[DIM],
   const double dir[3],
   const double distance,
   const unsigned enc_id,
   const struct sXd(hit)* hit_from,
   struct sXd(hit)* hit)
{
  struct hit_filter_data filter_data = HIT_FILTER_DATA_NULL;
  float ray_org[DIM] = {0};
  float ray_dir[3] = {0};
  float ray_range[2] = {0};
  ASSERT(scn && pos && dir && distance >= 0 && hit_from && hit);

  fX_set_dX(ray_org, pos);
  f3_set_d3(ray_dir, dir);
  ray_range[0] = 0;
  ray_range[1] = (float)distance;
  filter_data.XD(hit) = *hit_from;
  filter_data.epsilon = 1.e-6;
  filter_data.scn = scn; /* Enable the filtering wrt the enclosure id */
  filter_data.enc_id = enc_id;
#if DIM == 2
  SXD(scene_view_trace_ray_3d
    (scn->sXd(view), ray_org, ray_dir, ray_range, &filter_data, hit));
#else
  SXD(scene_view_trace_ray
    (scn->sXd(view), ray_org, ray_dir, ray_range, &filter_data, hit));
#endif
}

res_T
XD(find_next_fragment)
  (struct sdis_scene* scn,
   const double in_pos[DIM],
   const double in_dir[3], /* Always in 3D */
   const struct sXd(hit)* in_hit,
   const double time,
   const unsigned enc_id,
   struct sXd(hit)* out_hit,
   struct sdis_interface** out_interf,
   struct sdis_interface_fragment* out_frag)
{
  int NATTEMPTS_MAX = 10;
  int nattempts = 1;

  /* Stardis */
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  struct sdis_interface* interf = NULL;

  struct sXd(hit) hit = SXD_HIT_NULL;
  double rt_pos[DIM] = {0};
  res_T res = RES_OK;

  ASSERT(scn && in_pos && in_dir && in_hit);
  ASSERT(out_hit && out_interf && out_frag);

  /* Only one attempt is allowed when the ray does not start from a primitive */
  NATTEMPTS_MAX = S3D_HIT_NONE(in_hit) ? 1 : 10;

  dX(set)(rt_pos, in_pos);

  do {
    struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;
    struct sdis_medium* solid = NULL;
    double pos[3] = {0};
    double vec[3] = {0};
    double N[3] = {0};
    double delta = 0;

    /* Reset result code. It may have been modified during a previous attempt */
    res = RES_OK;

    /* Find the following surface along the direction of propagation */
    XD(trace_ray)(scn, rt_pos, in_dir, INF, enc_id, in_hit, &hit);
    if(SXD_HIT_NONE(&hit)) break;

    /* Retrieve the current position and normal */
    dX(add)(pos, rt_pos, dX(muld)(vec, in_dir, hit.distance));
    dX_set_fX(N, hit.normal);
    dX(normalize(N, N));

    /* Retrieve the current interface properties */
    interf = scene_get_interface(scn, hit.prim.prim_id);
    XD(setup_fragment)(&frag, pos, in_dir, time, N, &hit);

    /* Check that the path reaches a valid interface.
     * An invalid fragment may mean that the ray position is in a corner and the
     * traced ray has missed the surface of that corner. To correct this, the
     * ray position is moved slightly away from the corner before a ray is drawn
     * in the same direction. This fallback solution is executed a number of
     * times, after which, if the fragment is still invalid, it is considered
     * that the numerical error cannot be mitigated. */
    res = check_interface(interf, &frag, nattempts == NATTEMPTS_MAX);
    if(res != RES_OK && nattempts == NATTEMPTS_MAX) goto error;
    ++nattempts;

    if(res != RES_OK) { /* Mitigate numerical error (see above) */
      if(sdis_medium_get_type(interf->medium_front) == SDIS_SOLID) {
        solid = interf->medium_front;
      } else {
        ASSERT(sdis_medium_get_type(interf->medium_back) == SDIS_SOLID);
        solid = interf->medium_back;
      }

      /* Retrieves the delta of the solid that surrounds the boundary, as it is
       * actually the only numerical parameter that says something about the
       * system. */
      vtx.P[0] = pos[0];
      vtx.P[1] = pos[1];
      vtx.P[2] = pos[2];
      vtx.time = time;
      delta = solid_get_delta(solid, &vtx);

      XD(move_away_primitive_boundaries)(in_hit, delta, rt_pos);
    }
  } while(res != RES_OK);

exit:
  *out_hit = hit;
  *out_interf = interf;
  *out_frag = frag;
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
