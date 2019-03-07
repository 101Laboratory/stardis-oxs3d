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
    float range[2];
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

#if DIM == 2
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
        power_factor = delta_in_meter * delta_in_meter / (2.0 * DIM * lambda);
        T->value += power * power_factor;
      }
    } else {
      /* Hit something: move along dir0 of the minimum hit distance */
      delta = MMIN(hit0.distance, hit1.distance);

      /* Add the volumic power density to the measured temperature */
      if(power != SDIS_VOLUMIC_POWER_NONE) {
        const double delta_s_adjusted = delta_solid * RAY_RANGE_MAX_SCALE;
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
        tmp = h_in_meter * h_in_meter / (2.0 * lambda);

        /* Add the power corrective term. Be careful to use the adjusted
         * delta_solid to correctly handle the RAY_RANGE_MAX_SCALE factor in
         * the computation of the limit angle. But keep going with the
         * unmodified delta_solid in the corrective term since it was the one
         * that was "wrongly" used in the previous step and that must be
         * corrected. */
        if(h == delta_s_adjusted) {
          tmp += -(delta_s_in_meter * delta_s_in_meter)/(2.0*DIM*lambda);
        } else if(h < delta_s_adjusted) {
          const double sin_a = h / delta_s_adjusted;
#if DIM==2
          /* tmp1 = sin(2a) / (PI - 2*a) */
          const double tmp1 = sin_a * sqrt(1 - sin_a*sin_a)/acos(sin_a);
          tmp += -(delta_s_in_meter * delta_s_in_meter)/(4.0*lambda) * tmp1;
#else
          const double tmp1 = (sin_a*sin_a*sin_a - sin_a)/ (1-sin_a);
          tmp += (delta_s_in_meter * delta_s_in_meter)/(6.0*lambda) * tmp1;
#endif
        }
        power_factor = tmp;
        T->value += power * power_factor;
      }
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

    /* Define if the random walk hits something along dir0. Multiply delta by
     * the empirical ray range scale factor to ensure that once moved, the
     * random walk does not lie in the uncertainty zone near the geometry */
    if(hit0.distance > delta * RAY_RANGE_MAX_SCALE) {
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
