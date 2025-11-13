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

#include "sdis_log.h"
#include "sdis_green.h"
#include "sdis_interface_c.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_scene_c.h"

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
  f2(range, FLT_MIN, delta_solid*RAY_RANGE_MAX_SCALE);
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
   const unsigned current_enc_id,
   struct ssp_rng* rng,
   const double pos[DIM],
   const float delta_solid,
   float dir0[DIM], /* Sampled direction */
   float dir1[DIM], /* Direction used to adjust delta */
   struct sXd(hit)* hit0, /* Hit along the sampled direction */
   struct sXd(hit)* hit1, /* Hit used to adjust delta */
   float* out_delta)
{
  unsigned enc_id = ENCLOSURE_ID_NULL;
  float delta;
  float org[DIM];
  const size_t MAX_ATTEMPTS = 100;
  size_t iattempt = 0;
  res_T res = RES_OK;
  ASSERT(scn && rng && pos && delta_solid > 0);
  ASSERT(current_enc_id != ENCLOSURE_ID_NULL);
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
      res = scene_get_enclosure_id_in_closed_boundaries(scn, pos_next, &enc_id);
      if(res == RES_BAD_OP) { enc_id = ENCLOSURE_ID_NULL; res = RES_OK; }
      if(res != RES_OK) goto error;
    } else {
      unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
      scene_get_enclosure_ids(scn, hit0->prim.prim_id, enc_ids);
      enc_id = fX(dot)(dir0, hit0->normal) < 0 ? enc_ids[0] : enc_ids[1];
    }

    /* Check medium consistency */
    if(current_enc_id != enc_id) {
#if 0
      log_warn(scn->dev,
        "%s: inconsistent medium during the solid random walk -- "
        "pos=("FORMAT_VECX")\n", FUNC_NAME, SPLITX(pos));
#endif
    }
  } while(current_enc_id != enc_id && ++iattempt < MAX_ATTEMPTS);

  /* Handle error */
  if(iattempt >= MAX_ATTEMPTS) {
    log_warn(scn->dev,
      "%s: could not find a next valid conductive -- pos=("FORMAT_VECX")\n",
      FUNC_NAME, SPLITX(pos));
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
 * Handle the volumic power at a given diffusive step
 ******************************************************************************/
struct XD(handle_volumic_power_args) {
  /* Forward/backward direction of the sampled diffusive step */
  const float* dir0;
  const float* dir1;

  /* Forward/backward intersections along the sampled diffusive step */
  const struct sXd(hit)* hit0;
  const struct sXd(hit)* hit1;

  /* Physical properties */
  double power; /* Volumic power */
  double lambda; /* Conductivity  */

  float delta_solid; /* Challenged length of a diffusive step */
  float delta; /* Current length of the current diffusive step */

  size_t picard_order;
};
static const struct XD(handle_volumic_power_args)
XD(HANDLE_VOLUMIC_POWER_ARGS_NULL) = {
  NULL, NULL, NULL, NULL, -1, -1, -1, -1, 0
};

static INLINE int
XD(check_handle_volumic_power_args)
  (const struct XD(handle_volumic_power_args)* args)
{
  ASSERT(args);
  return args
      && args->dir0
      && args->dir1
      && args->hit0
      && args->hit1
      && args->lambda >= 0
      && args->delta_solid > 0
      && args->delta >= 0
      && args->delta_solid >= 0
      && args->picard_order > 0;
}

static res_T
XD(handle_volumic_power)
  (const struct sdis_scene* scn,
   const struct XD(handle_volumic_power_args)* args,
   double* out_power_term,
   struct temperature* T)
{
  double power_term = 0;
  res_T res = RES_OK;
  ASSERT(scn && out_power_term && T && XD(check_handle_volumic_power_args)(args));

  /* No volumic power. Do nothing */
  if(args->power == SDIS_VOLUMIC_POWER_NONE) goto exit;

  /* Check that picardN is not enabled when a volumic power is set since in
   * this situation the upper bound of the Monte-Carlo weight required by
   * picardN cannot be known */
  if(args->picard_order > 1) {
    log_err(scn->dev,
     "%s: invalid not null volumic power '%g' kg/m^3. Could not manage a "
     "volumic power when the picard order is not equal to 1; Picard order is "
     "currently set to %lu.\n",
     FUNC_NAME, args->power, (unsigned long)args->picard_order);
    res = RES_BAD_ARG;
    goto error;
  }

  /* No forward/backward intersection along the sampled direction */
  if(SXD_HIT_NONE(args->hit0) && SXD_HIT_NONE(args->hit1)) {
    const double delta_in_meter = args->delta * scn->fp_to_meter;
    power_term = delta_in_meter * delta_in_meter / (2.0 * DIM * args->lambda);
    T->value += args->power * power_term;

  /* An intersection along this diffusive step is find. Use it to statically
   * correct the power term currently registered */
  } else {
    const double delta_s_adjusted = args->delta_solid * RAY_RANGE_MAX_SCALE;
    const double delta_s_in_meter = args->delta_solid * scn->fp_to_meter;
    double h;
    double h_in_meter;
    double cos_U_N;
    float N[DIM] = {0};

    if(args->delta == args->hit0->distance) {
      fX(normalize)(N, args->hit0->normal);
      cos_U_N = fX(dot)(args->dir0, N);

    } else {
      ASSERT(args->delta == args->hit1->distance);
      fX(normalize)(N, args->hit1->normal);
      cos_U_N = fX(dot)(args->dir1, N);
    }

    h = args->delta * fabs(cos_U_N);
    h_in_meter = h * scn->fp_to_meter;

    /* The regular power term */
    power_term = h_in_meter * h_in_meter / (2.0*args->lambda);

    /* Add the power corrective term. Be careful to use the adjusted
     * delta_solid to correctly handle the RAY_RANGE_MAX_SCALE factor in the
     * computation of the limit angle. But keep going with the unmodified
     * delta_solid in the corrective term since it was the one that was
     * "wrongly" used in the previous step and that must be corrected. */
    if(h == delta_s_adjusted) {
      power_term +=
        -(delta_s_in_meter * delta_s_in_meter) / (2.0*DIM*args->lambda);

    } else if(h < delta_s_adjusted) {
      const double sin_a = h / delta_s_adjusted;
#if DIM==2
      /* tmp = sin(2a) / (PI - 2*a) */
      const double tmp = sin_a * sqrt(1 - sin_a*sin_a) / acos(sin_a);
      power_term +=
        -(delta_s_in_meter * delta_s_in_meter) / (4.0*args->lambda) * tmp;
#else
      const double tmp = (sin_a*sin_a*sin_a - sin_a) / (1-sin_a);
      power_term +=
         (delta_s_in_meter * delta_s_in_meter) / (6.0*args->lambda) * tmp;
#endif
    }
    T->value += args->power * power_term;
  }

exit:
  *out_power_term = power_term;
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
res_T
XD(conductive_path_delta_sphere)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  /* Enclosure/medium in which the conductive path starts */
  struct sdis_medium* mdm = NULL;
  unsigned enc_id = ENCLOSURE_ID_NULL;

  /* Physical properties */
  struct solid_props props_ref = SOLID_PROPS_NULL;
  double green_power_term = 0;

  /* Miscellaneous */
  double position_start[DIM] = {0};
  size_t istep = 0; /* Help for debug */
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(scn && rwalk && rng && T);

  (void)ctx, (void)istep; /* Avoid "unsued variable" warnings */

  res = scene_get_enclosure_id_in_closed_boundaries(scn, rwalk->vtx.P, &enc_id);
  if(res != RES_OK) goto error;
  res = scene_get_enclosure_medium(scn, scene_get_enclosure(scn, enc_id), &mdm);
  if(res != RES_OK) goto error;
  ASSERT(sdis_medium_get_type(mdm) == SDIS_SOLID);

  /* Check the random walk consistency */
  if(enc_id != rwalk->enc_id) {
    log_err(scn->dev, "%s: invalid solid random walk. "
      "Unexpected enclosure -- pos=("FORMAT_VECX")\n",
      FUNC_NAME, SPLITX(rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  /* Save the submitted position */
  dX(set)(position_start, rwalk->vtx.P);

  /* Retrieve the solid properties at the current position. Use them to verify
   * that those that are supposed to be constant by the conductive random walk
   * remain the same. Note that we take care of the same constraints on the
   * solid reinjection since once reinjected, the position of the random walk
   * is that at the beginning of the conductive random walk. Thus, after a
   * reinjection, the next line retrieves the properties of the reinjection
   * position. By comparing them to the properties along the random walk, we
   * thus verify that the properties are constant throughout the random walk
   * with respect to the properties of the reinjected position. */
  solid_get_properties(mdm, &rwalk->vtx, &props_ref);

  do { /* Solid random walk */
    struct XD(handle_volumic_power_args) handle_volpow_args =
       XD(HANDLE_VOLUMIC_POWER_ARGS_NULL);
    struct sXd(hit) hit0, hit1;
    struct solid_props props = SOLID_PROPS_NULL;
    double power_term = 0;
    double mu;
    float delta; /* Random walk numerical parameter */
    double delta_m;
    float dir0[DIM], dir1[DIM];
    float org[DIM];

    /* Fetch solid properties */
    res = solid_get_properties(mdm, &rwalk->vtx, &props);
    if(res != RES_OK) goto error;

    res = check_solid_constant_properties
      (scn->dev, ctx->green_path != NULL, 0/*use WoS?*/, &props_ref, &props);
    if(res != RES_OK) goto error;

    /* Check the limit condition
     * REVIEW Rfo: This can be a bug if the random walk comes from a boundary */
    if(SDIS_TEMPERATURE_IS_KNOWN(props.temperature)) {
      T->value += props.temperature;
      T->done = 1;

      if(ctx->green_path) {
        res = green_path_set_limit_vertex
          (ctx->green_path, mdm, &rwalk->vtx, rwalk->elapsed_time);
        if(res != RES_OK) goto error;
      }

      if(ctx->heat_path) {
        heat_path_get_last_vertex(ctx->heat_path)->weight = T->value;
      }

      break;
    }

    fX_set_dX(org, rwalk->vtx.P);

    /* Sample the direction to walk toward and compute the distance to travel */
    res = XD(sample_next_step_robust)(scn, enc_id, rng, rwalk->vtx.P,
      (float)props.delta, dir0, dir1, &hit0, &hit1, &delta);
    if(res != RES_OK) goto error;

    /* Add the volumic power density to the measured temperature */
    handle_volpow_args.dir0 = dir0;
    handle_volpow_args.dir1 = dir1;
    handle_volpow_args.hit0 = &hit0;
    handle_volpow_args.hit1 = &hit1;
    handle_volpow_args.power = props.power;
    handle_volpow_args.lambda = props.lambda;
    handle_volpow_args.delta_solid = (float)props.delta;
    handle_volpow_args.delta = delta;
    handle_volpow_args.picard_order = get_picard_order(ctx);
    res = XD(handle_volumic_power)(scn, &handle_volpow_args, &power_term, T);
    if(res != RES_OK) goto error;

    /* Register the power term for the green function. Delay its registration
     * until the end of the conductive path, i.e. the path is valid */
    if(ctx->green_path && props.power != SDIS_VOLUMIC_POWER_NONE) {
      green_power_term += power_term;
    }

    /* Rewind the time */
    delta_m = delta * scn->fp_to_meter;
    mu = (2*DIM*props.lambda)/(props.rho*props.cp*delta_m*delta_m);
    res = time_rewind(scn, mu, props.t0, rng, rwalk, ctx, T);
    if(res != RES_OK) goto error;
    if(T->done) break; /* Limit condition was reached */

   /* Define if the random walk hits something along dir0 */
    if(hit0.distance > delta) {
      rwalk->XD(hit) = SXD_HIT_NULL;
      rwalk->hit_side = SDIS_SIDE_NULL__;
    } else {
      rwalk->XD(hit) = hit0;
      rwalk->hit_side = fX(dot)(hit0.normal, dir0) < 0 ? SDIS_FRONT : SDIS_BACK;
    }

    /* Update the random walk position */
    XD(move_pos)(rwalk->vtx.P, dir0, delta);

    /* Register the new vertex against the heat path */
    res = register_heat_vertex(ctx->heat_path, &rwalk->vtx, T->value,
      SDIS_HEAT_VERTEX_CONDUCTION, (int)ctx->nbranchings);
    if(res != RES_OK) goto error;

    ++istep;

  /* Keep going while the solid random walk does not hit an interface */
  } while(SXD_HIT_NONE(&rwalk->XD(hit)));

  /* Register the power term for the green function */
  if(ctx->green_path && props_ref.power != SDIS_VOLUMIC_POWER_NONE) {
    res = green_path_add_power_term
      (ctx->green_path, mdm, &rwalk->vtx, green_power_term);
    if(res != RES_OK) goto error;
  }

  T->func = XD(boundary_path);
  rwalk->enc_id = ENCLOSURE_ID_NULL; /* At the interface between 2 media */

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
