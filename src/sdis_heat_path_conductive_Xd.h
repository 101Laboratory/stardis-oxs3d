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
#include "sdis_green.h"
#include "sdis_heat_path.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
/* Sample the next direction to walk toward and compute the distance to travel.
 * Return the sampled direction `dir0', the distance to travel along this
 * direction, the hit `hit0' along `dir0' wrt to the returned distance, the
 * direction `dir1' used to adjust the displacement distance, and the hit
 * `hit1' along `dir1' used to adjust the displacement distance. */
static float
XD(sample_next_step)
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const float pos[DIM],
   const float delta_solid,
   float dir0[DIM], /* Sampled direction */
   float dir1[DIM], /* Direction used to adjust delta */
   struct sXd(hit)* hit0, /* Hit along the sampled direction */
   struct sXd(hit)* hit1) /* Hit used to adjust delta */
{
  float dirs[2*DIM][DIM];
  float range[2];
  float delta;
  struct sXd(hit) hit= SXD_HIT_NULL;
  int idir;
  int idir1;
  ASSERT(scn && rng && pos && delta_solid>0 && dir0 && dir1 && hit0 && hit1);

  *hit0 = SXD_HIT_NULL;
  *hit1 = SXD_HIT_NULL;

#if DIM == 2
  /* Sample a main direction around 2PI */
  ssp_ran_circle_uniform_float(rng, dirs[0], NULL);

  /* Compute in dirs[2] a direction orthogonal to dirs[0] */
  dirs[2][0] = -dirs[0][1];
  dirs[2][1] =  dirs[0][0];
  ASSERT(f2_is_normalized(dirs[2]));
  ASSERT(eq_epsf(f2_dot(dirs[0], dirs[2]), 0, 1.e-6f));

  /* Negate the orthornormal frame */
  f2_minus(dirs[1], dirs[0]);
  f2_minus(dirs[3], dirs[2]);
#else
  {
    float dir_abs[DIM];
    int i, j, k;

    /* Sample a main direction around 4PI */
    ssp_ran_sphere_uniform_float(rng, dirs[0], NULL);

    /* Find the index of the maximum coordinate of the sampled direction */
    dir_abs[0] = absf(dirs[0][0]);
    dir_abs[1] = absf(dirs[0][1]);
    dir_abs[2] = absf(dirs[0][2]);
    i =  dir_abs[0] > dir_abs[1]
      ? (dir_abs[0] > dir_abs[2] ? 0 : 2)
      : (dir_abs[1] > dir_abs[2] ? 1 : 2);
    j = (i+1) % 3;
    k = (j+1) % 3;

    /* Compute a direction orthogonal to the sample dir */
    dirs[2][i] = -(dirs[0][j]*dirs[0][j] + dirs[0][k]*dirs[0][k]) / dirs[0][i];
    dirs[2][j] = dirs[0][j];
    dirs[2][k] = dirs[0][k];
    f3_normalize(dirs[2], dirs[2]);

    /* Complete the orthonormal frame */
    f3_cross(dirs[4], dirs[0], dirs[2]);
    f3_normalize(dirs[4], dirs[4]);

    /* Negate the orthonormal frame */
    f3_minus(dirs[1], dirs[0]);
    f3_minus(dirs[3], dirs[2]);
    f3_minus(dirs[5], dirs[4]);

    ASSERT(f3_is_normalized(dirs[2]));
    ASSERT(f3_is_normalized(dirs[4]));
    ASSERT(eq_epsf(f3_dot(dirs[0], dirs[2]), 0, 1.e-6f));
    ASSERT(eq_epsf(f3_dot(dirs[0], dirs[4]), 0, 1.e-6f));
    ASSERT(eq_epsf(f3_dot(dirs[2], dirs[4]), 0, 1.e-6f));
  }
#endif

  /* Use the previously computed orthornormal frame to estimate the minimum
   * distance from `pos' to the scene boundary */
  range[0] = 0.f;
  range[1] = delta_solid*RAY_RANGE_MAX_SCALE;
  delta = FLT_MAX;
  idir1 = 0;
  FOR_EACH(idir, 0, 2*DIM) {
    SXD(scene_view_trace_ray(scn->sXd(view), pos, dirs[idir], range, NULL, &hit));
    if(idir == 0) *hit0 = hit;
    if(hit.distance < delta) {
      delta = hit.distance;
      *hit1 = hit;
      idir1 = idir;
    }
  }

  if(delta == FLT_MAX) {
    /* Hit nothing along all tested directions. Set delta to delta_solid. */
    delta = delta_solid;
  } else if
  (  !SXD_HIT_NONE(hit0)
  && delta != hit0->distance
  && (  eq_eps(hit0->distance, delta, delta_solid*(RAY_RANGE_MAX_SCALE-1))
     || hit0->distance < delta_solid * 0.1)) {
    /* Set delta to the main hit distance if it is roughly equal to it in order
     * to avoid numerical issues on moving along the main direction. Use the
     * RAY_RANGE_MAX_SCALE factor to define the `epsilon' used by this
     * comparison. In addition force delta to the main hit distance if this
     * distance is quite small regarding the original delta. */
    delta = hit0->distance;
    *hit1 = *hit0;
    idir1 = 0;
  }

  fX(set)(dir0, dirs[0]);
  fX(set)(dir1, dirs[idir1]);

  return delta;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
res_T
XD(conductive_path)
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  double position_start[DIM];
  double green_power_factor = 0;
  double power_ref = SDIS_VOLUMIC_POWER_NONE;
  struct sdis_medium* mdm;
  size_t istep = 0; /* Help for debug */
  res_T res = RES_OK;
  ASSERT(scn && fp_to_meter > 0 && rwalk && rng && T);
  ASSERT(rwalk->mdm->type == SDIS_SOLID);
  (void)ctx, (void)istep;

  /* Check the random walk consistency */
  CHK(scene_get_medium(scn, rwalk->vtx.P, NULL, &mdm) == RES_OK);
  if(mdm != rwalk->mdm) {
    log_err(scn->dev, "%s: invalid solid random walk. "
      "Unexpected medium at {%g, %g, %g}.\n", FUNC_NAME, SPLIT3(rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }
  /* Save the submitted position */
  dX(set)(position_start, rwalk->vtx.P);

  if(ctx->green_path) {
    /* Retrieve the power of the medium. Use it to check that it is effectively
     * constant along the random walk */
    power_ref = solid_get_volumic_power(mdm, &rwalk->vtx);
  }

  do { /* Solid random walk */
    struct get_medium_info info = GET_MEDIUM_INFO_NULL;
    struct sXd(hit) hit0, hit1;
    double lambda; /* Thermal conductivity */
    double rho; /* Volumic mass */
    double cp; /* Calorific capacity */
    double tmp;
    double power_factor = 0;
    double power;
    float delta, delta_solid; /* Random walk numerical parameter */
    float dir0[DIM], dir1[DIM];
    float org[DIM];

    /* Check the limit condition */
    tmp = solid_get_temperature(mdm, &rwalk->vtx);
    if(tmp >= 0) {
      T->value += tmp;
      T->done = 1;

      if(ctx->green_path) {
        res = green_path_set_limit_vertex
          (ctx->green_path, rwalk->mdm, &rwalk->vtx);
        if(res != RES_OK) goto error;
      }

      if(ctx->heat_path) {
        heat_path_get_last_vertex(ctx->heat_path)->weight = T->value;
      }

      break;
    }

    /* Fetch solid properties */
    delta_solid = (float)solid_get_delta(mdm, &rwalk->vtx);
    lambda = solid_get_thermal_conductivity(mdm, &rwalk->vtx);
    rho = solid_get_volumic_mass(mdm, &rwalk->vtx);
    cp = solid_get_calorific_capacity(mdm, &rwalk->vtx);
    power = solid_get_volumic_power(mdm, &rwalk->vtx);

    if(ctx->green_path && power_ref != power) {
      log_err(scn->dev,
        "%s: invalid non constant volumic power term. Expecting a constant "
        "volumic power in time and space on green function estimation.\n",
        FUNC_NAME);
      res = RES_BAD_ARG;
      goto error;
    }

    fX_set_dX(org, rwalk->vtx.P);

    /* Sample the direction to walk toward and compute the distance to travel */
    delta = XD(sample_next_step)
      (scn, rng, org, delta_solid, dir0, dir1, &hit0, &hit1);

    /* Add the volumic power density to the measured temperature */
    if(power != SDIS_VOLUMIC_POWER_NONE) {
      const double delta_in_meter = delta * fp_to_meter;
      power_factor = delta_in_meter * delta_in_meter / (2.0 * DIM * lambda);
      T->value += power * power_factor;
    }

    /* Register the power term for the green function. Delay its registration
     * until the end of the conductive path, i.e. the path is valid */
    if(ctx->green_path && power != SDIS_VOLUMIC_POWER_NONE) {
      green_power_factor += power_factor;
    }

    /* Sample the time */
    if(!IS_INF(rwalk->vtx.time)) {
      double tau, mu, t0;
      mu = (2*DIM*lambda) / (rho*cp*delta*fp_to_meter*delta*fp_to_meter);
      tau = ssp_ran_exp(rng, mu);
      t0 = ctx->green_path ? -INF : solid_get_t0(rwalk->mdm);
      rwalk->vtx.time = MMAX(rwalk->vtx.time - tau, t0);
      if(rwalk->vtx.time == t0) {
        /* Check the initial condition */
        tmp = solid_get_temperature(mdm, &rwalk->vtx);
        if(tmp >= 0) {
          T->value += tmp;
          T->done = 1;

          if(ctx->heat_path) {
            struct sdis_heat_vertex* vtx;
            vtx = heat_path_get_last_vertex(ctx->heat_path);
            vtx->time = rwalk->vtx.time;
            vtx->weight = T->value;
          }
          break;
        }
        /* The initial condition should have been reached */
        log_err(scn->dev,
          "%s: undefined initial condition. "
          "The time is %f but the temperature remains unknown.\n",
          FUNC_NAME, t0);
        res = RES_BAD_OP;
        goto error;
      }
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

    /* Register the new vertex against the heat path */
    res = register_heat_vertex
      (ctx->heat_path, &rwalk->vtx, T->value, SDIS_HEAT_VERTEX_CONDUCTION);
    if(res != RES_OK) goto error;

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
      res = RES_BAD_OP;
      goto error;
    }

    ++istep;

  /* Keep going while the solid random walk does not hit an interface */
  } while(SXD_HIT_NONE(&rwalk->hit));

  /* Register the power term for the green function */
  if(ctx->green_path && power_ref != SDIS_VOLUMIC_POWER_NONE) {
    res = green_path_add_power_term
      (ctx->green_path, rwalk->mdm, &rwalk->vtx, green_power_factor);
    if(res != RES_OK) goto error;
  }

  T->func = XD(boundary_path);
  rwalk->mdm = NULL; /* The random walk is at an interface between 2 media */

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
