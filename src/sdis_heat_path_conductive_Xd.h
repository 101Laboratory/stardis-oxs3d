/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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
  struct sXd(hit) hits[2];
  float dirs[2][DIM];
  float range[2];
  float delta;
  ASSERT(scn && rng && pos && delta_solid>0 && dir0 && dir1 && hit0 && hit1);

  *hit0 = SXD_HIT_NULL;
  *hit1 = SXD_HIT_NULL;

#if DIM == 2
  /* Sample a main direction around 2PI */
  ssp_ran_circle_uniform_float(rng, dirs[0], NULL);
#else
  /* Sample a main direction around 4PI */
  ssp_ran_sphere_uniform_float(rng, dirs[0], NULL);
#endif

  /* Negate the sampled dir */
  fX(minus)(dirs[1], dirs[0]);

  /* Use the previously sampled direction to estimate the minimum distance from
   * `pos' to the scene boundary */
  f2(range, 0.f, delta_solid*RAY_RANGE_MAX_SCALE);
  SXD(scene_view_trace_ray(scn->sXd(view), pos, dirs[0], range, NULL, &hits[0]));
  SXD(scene_view_trace_ray(scn->sXd(view), pos, dirs[1], range, NULL, &hits[1]));
  if(SXD_HIT_NONE(&hits[0]) && SXD_HIT_NONE(&hits[1])) {
    delta = delta_solid;
  } else {
    delta = MMIN(hits[0].distance, hits[1].distance);
  }

  if(!SXD_HIT_NONE(&hits[0])
  && delta != hits[0].distance
  && eq_eps(hits[0].distance, delta, delta_solid*0.1)) {
    /* Set delta to the main hit distance if it is roughly equal to it in order
     * to avoid numerical issues on moving along the main direction. */
    delta = hits[0].distance;
  }

  /* Setup outputs */
  if(delta <= delta_solid*0.1 && hits[1].distance == delta) {
    /* Snap the random walk to the boundary if delta is too small */
    fX(set)(dir0, dirs[1]);
    *hit0 = hits[1];
    fX(splat)(dir1, (float)INF);
    *hit1 = SXD_HIT_NULL;
  } else {
    fX(set)(dir0, dirs[0]);
    *hit0 = hits[0];
    if(delta == hits[0].distance) {
      fX(set)(dir1, dirs[0]);
      *hit1 = hits[0];
    } else if(delta == hits[1].distance) {
      fX(set)(dir1, dirs[1]);
      *hit1 = hits[1];
    } else {
      fX(splat)(dir1, 0);
      *hit1 = SXD_HIT_NULL;
    }
  }

  return delta;
}

/* Sample the next direction to walk toward and compute the distance to travel.
 * If the targeted position does not lie inside the current medium, reject it
 * and sample a new next step. */
static res_T
XD(sample_next_step_robust)
  (struct sdis_scene* scn,
   struct sdis_medium* current_mdm,
   struct ssp_rng* rng,
   const double pos[DIM],
   const float delta_solid,
   float dir0[DIM], /* Sampled direction */
   float dir1[DIM], /* Direction used to adjust delta */
   struct sXd(hit)* hit0, /* Hit along the sampled direction */
   struct sXd(hit)* hit1, /* Hit used to adjust delta */
   float* out_delta)
{
  struct sdis_medium* mdm;
  float delta;
  float org[DIM];
  const size_t MAX_ATTEMPTS = 100;
  size_t iattempt = 0;
  res_T res = RES_OK;
  ASSERT(scn && current_mdm && rng && pos && delta_solid > 0);
  ASSERT(dir0 && dir1 && hit0 && hit1 && out_delta);

  fX_set_dX(org, pos);
  do {
    double pos_next[DIM];

    /* Compute the next step */
    delta = XD(sample_next_step)
      (scn, rng, org, delta_solid, dir0, dir1, hit0, hit1);

    /* Retrieve the medium of the next step */
    if(hit0->distance > delta) {
      XD(move_pos)(dX(set)(pos_next, pos), dir0, delta);
      res = scene_get_medium_in_closed_boundaries(scn, pos_next, &mdm);
      if(res == RES_BAD_OP) { mdm = NULL; res = RES_OK; }
      if(res != RES_OK) goto error;
    } else {
      struct sdis_interface* interf;
      enum sdis_side side;
      interf = scene_get_interface(scn, hit0->prim.prim_id);
      side = fX(dot)(dir0, hit0->normal) < 0 ? SDIS_FRONT : SDIS_BACK;
      mdm = interface_get_medium(interf, side);
    }

    /* Check medium consistency */
    if(current_mdm != mdm) {
#if 0
#if DIM == 2
      log_warn(scn->dev,
        "%s: inconsistent medium during the solid random walk at {%g, %g}.\n",
        FUNC_NAME, SPLIT2(pos));
#else
      log_warn(scn->dev,
        "%s: inconsistent medium during the solid random walk at {%g, %g, %g}.\n",
        FUNC_NAME, SPLIT3(pos));
#endif
#endif
    }
  } while(current_mdm != mdm && ++iattempt < MAX_ATTEMPTS);

  /* Handle error */
  if(iattempt >= MAX_ATTEMPTS) {
#if DIM == 2
    log_warn(scn->dev,
      "%s: could not find a next valid conductive step at {%g, %g}.\n",
      FUNC_NAME, SPLIT2(pos));
#else
    log_warn(scn->dev,
      "%s: could not find a next valid conductive step at {%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(pos));
#endif
    res = RES_BAD_OP;
    goto error;
  }

  *out_delta = delta;

exit:
  return res;
error:
  goto exit;
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
  res = scene_get_medium_in_closed_boundaries(scn, rwalk->vtx.P, &mdm);
  if(res != RES_OK || mdm != rwalk->mdm) {
    log_warn(scn->dev, "%s: invalid solid random walk. "
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

    /* Check the limit condition 
     * REVIEW Rfo: This can be a bug if the random walk comes from a boundary */
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
    res = XD(sample_next_step_robust)(scn, mdm, rng, rwalk->vtx.P, delta_solid,
      dir0, dir1, &hit0, &hit1, &delta);
    if(res != RES_OK) goto error;

    /* Add the volumic power density to the measured temperature */
    if(power != SDIS_VOLUMIC_POWER_NONE) {
      if((S3D_HIT_NONE(&hit0) && S3D_HIT_NONE(&hit1))) { /* Hit nothing */
        const double delta_in_meter = delta * fp_to_meter;
        power_factor = delta_in_meter * delta_in_meter / (2.0 * DIM * lambda);
        T->value += power * power_factor;
      } else {
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
          "The time is %g but the temperature remains unknown.\n",
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
