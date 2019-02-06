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

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(trace_radiative_path)
  (struct sdis_scene* scn,
   const float ray_dir[3],
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  /* The radiative random walk is always performed in 3D. In 2D, the geometry
   * are assumed to be extruded to the infinty along the Z dimension. */
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
#if (SDIS_XD_DIMENSION == 2)
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
      res = RES_BAD_OP;
      goto error;
    }

    /* Switch in boundary temperature ? */
    r = ssp_rng_canonical(rng);
    if(r < epsilon) {
      T->func = XD(boundary_path);
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
      /* To ease the setting of models, the external enclosure is allowed to be
       * incoherent regarding media. Here a radiative path is allowed to join
       * 2 different fluids. */
      const int outside = scene_is_outside
        (scn, rwalk->hit_side, rwalk->hit.prim.prim_id);
      if(outside && chk_mdm->type == SDIS_FLUID) {
        rwalk->mdm = chk_mdm;
      } else {
        log_err(scn->dev, "%s: inconsistent medium definition at `%g %g %g'.\n",
          FUNC_NAME, SPLIT3(rwalk->vtx.P));
        res = RES_BAD_OP;
        goto error;
      }
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
XD(radiative_path)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  /* The radiative random walk is always performed in 3D. In 2D, the geometry
   * are assumed to be extruded to the infinty along the Z dimension. */
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

#include "sdis_Xd_end.h"
