/* Copyright (C) 2016-2024 |Méso|Star> (contact@meso-star.com)
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
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_radiative_env_c.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

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
  float N[3] = {0, 0, 0};
  float dir[3] = {0, 0, 0};
  int branch_id;
  res_T res = RES_OK;

  ASSERT(scn && ray_dir && ctx && rwalk && rng && T);

  f3_set(dir, ray_dir);

  /* (int)ctx->nbranchings < 0 <=> Beginning of the realisation */
  branch_id = MMAX((int)ctx->nbranchings, 0);

  /* Launch the radiative random walk */
  for(;;) {
    const struct sdis_interface* interf = NULL;
    struct sdis_medium* chk_mdm = NULL;
    struct hit_filter_data filter_data = HIT_FILTER_DATA_NULL;
    struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
    unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
    unsigned chk_enc_id = ENCLOSURE_ID_NULL;
    double alpha;
    double epsilon;
    double r;
    float pos[DIM];
    const float range[2] = { 0, FLT_MAX };

    fX_set_dX(pos, rwalk->vtx.P);

    /* Trace the radiative ray */
    filter_data.XD(hit) = rwalk->XD(hit);
    filter_data.epsilon = 1.e-6;
#if (SDIS_XD_DIMENSION == 2)
    SXD(scene_view_trace_ray_3d
      (scn->sXd(view), pos, dir, range, &filter_data, &rwalk->XD(hit)));
#else
    SXD(scene_view_trace_ray
      (scn->sXd(view), pos, dir, range, &filter_data, &rwalk->XD(hit)));
#endif
    if(SXD_HIT_NONE(&rwalk->XD(hit))) { /* Fetch the ambient radiative temperature */
      struct sdis_radiative_ray ray = SDIS_RADIATIVE_RAY_NULL;
      double trad = 0; /* [K] */

      rwalk->hit_side = SDIS_SIDE_NULL__;
      d3_set_f3(rwalk->dir, dir);
      d3_normalize(rwalk->dir, rwalk->dir);
      d3_set(ray.dir, rwalk->dir);

      trad = radiative_env_get_temperature(scn->radenv, &ray);
      if(SDIS_TEMPERATURE_IS_UNKNOWN(trad)) {
        log_err(scn->dev,
          "%s: the random walk has reached an invalid radiative environment from "
          "position `%g %g %g' along direction `%g %g %g': the temperature is "
          "unknown. This may be due to numerical inaccuracies or inconsistencies "
          "in the simulated system (e.g. non-closed geometry). For systems where "
          "random walks can reach such a temperature, we need to define a valid "
          "radiative temperature, i.e. one with a known temperature.\n",
          FUNC_NAME, SPLIT3(rwalk->vtx.P), SPLIT3(rwalk->dir));
        res = RES_BAD_OP;
        goto error;
      }

      T->value += trad;
      T->done = 1;

      if(ctx->green_path) {
        res = green_path_set_limit_radiative_ray
          (ctx->green_path, &ray, rwalk->elapsed_time);
        if(res != RES_OK) goto error;
      }

      if(ctx->heat_path) {
        const float empirical_dst = 0.1f * (float)scn->fp_to_meter;
        struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;

        vtx = rwalk->vtx;
        vtx.P[0] += dir[0] * empirical_dst;
        vtx.P[1] += dir[1] * empirical_dst;
        vtx.P[2] += dir[2] * empirical_dst;
        res = register_heat_vertex(ctx->heat_path, &vtx, T->value,
          SDIS_HEAT_VERTEX_RADIATIVE, branch_id);
        if(res != RES_OK) goto error;
      }

      /* Stop the radiative path */
      break;
    }

    /* Define the hit side */
    rwalk->hit_side = fX(dot)(dir, rwalk->XD(hit).normal) < 0
      ? SDIS_FRONT : SDIS_BACK;

    /* Move the random walk to the hit position */
    XD(move_pos)(rwalk->vtx.P, dir, rwalk->XD(hit).distance);

    /* Register the random walk vertex against the heat path */
    res = register_heat_vertex(ctx->heat_path, &rwalk->vtx, T->value,
      SDIS_HEAT_VERTEX_RADIATIVE, branch_id);
    if(res != RES_OK) goto error;

    /* Fetch the new interface and setup the hit fragment */
    interf = scene_get_interface(scn, rwalk->XD(hit).prim.prim_id);
    XD(setup_interface_fragment)(&frag, &rwalk->vtx, &rwalk->XD(hit), rwalk->hit_side);

    /* Fetch the interface emissivity */
    epsilon = interface_side_get_emissivity(interf, SDIS_INTERN_SOURCE_ID, &frag);
    if(epsilon > 1 || epsilon < 0) {
      log_err(scn->dev,
        "%s: invalid overall emissivity `%g' at position `%g %g %g'.\n",
        FUNC_NAME, epsilon, SPLIT3(rwalk->vtx.P));
      res = RES_BAD_OP;
      goto error;
    }

    /* Switch in boundary temperature ? */
    r = ssp_rng_canonical(rng);
    if(r < epsilon) {
      T->func = XD(boundary_path);
      rwalk->enc_id = ENCLOSURE_ID_NULL; /* Interface between 2 enclosures */
      break;
    }

    /* Normalize the normal of the interface and ensure that it points toward the
     * current medium */
    fX(normalize)(N, rwalk->XD(hit).normal);
    if(rwalk->hit_side == SDIS_BACK) fX(minus)(N, N);

    /* Check that the radiative path still lies in the same enclosure */
    scene_get_enclosure_ids(scn, rwalk->XD(hit).prim.prim_id, enc_ids);
    chk_enc_id = rwalk->hit_side == SDIS_FRONT ? enc_ids[0] : enc_ids[1];
    if(chk_enc_id != rwalk->enc_id) {
      log_warn(scn->dev,
        "%s: the radiative path has escaped from its cavity -- pos=(%g, %g, %g)\n",
        FUNC_NAME, SPLIT3(rwalk->vtx.P));
      res = RES_BAD_OP;
      goto error;
    }

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
      ? interf->medium_front : interf->medium_back;
    if(sdis_medium_get_type(chk_mdm)) {
      log_err(scn->dev,
        "%s: a radiative path cannot evolve in a solid -- pos=(%g, %g, %g)\n",
        FUNC_NAME, SPLIT3(rwalk->vtx.P));
      res = RES_BAD_OP_IRRECOVERABLE;
      goto error;
    }

    alpha = interface_side_get_specular_fraction(interf, SDIS_INTERN_SOURCE_ID, &frag);
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

#include "sdis_Xd_end.h"
