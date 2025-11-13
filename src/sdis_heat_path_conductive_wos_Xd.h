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
#include "sdis_heat_path_conductive_c.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <star/swf.h>

#include "sdis_Xd_begin.h"

/* Define epsilon shell from delta */
#define EPSILON_SHELL(Delta) ((Delta)*1e-2)

/*******************************************************************************
 * Non generic helper functions
 ******************************************************************************/
#ifndef SDIS_HEAT_PATH_CONDUCTIVE_WOS_XD_H
#define SDIS_HEAT_PATH_CONDUCTIVE_WOS_XD_H

static res_T
update_green_path
  (struct green_path_handle* green_path,
   struct rwalk* rwalk,
   struct sdis_medium* mdm,
   const struct solid_props* props,
   const double power_term,
   const struct temperature* T)
{
  res_T res = RES_OK;
  ASSERT(mdm && props && T);

  /* Is the green function estimated? */
  if(!green_path) goto exit;

  /* Save power term for green function if any */
  if(props->power != SDIS_VOLUMIC_POWER_NONE) {
    res = green_path_add_power_term(green_path, mdm, &rwalk->vtx, power_term);
    if(res != RES_OK) goto error;
  }

  /* Set the green path limit to the current position if the initial condition
   * has been reached */
  if(T->done) {
    res = green_path_set_limit_vertex
      (green_path, mdm, &rwalk->vtx, rwalk->elapsed_time);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#endif /* SDIS_HEAT_PATH_CONDUCTIVE_WOS_XD_H */

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static res_T
XD(check_enclosure_consistency)
  (struct sdis_scene* scn,
   const struct rwalk* rwalk)
{
  unsigned enc_id = ENCLOSURE_ID_NULL;
  res_T res = RES_OK;
  ASSERT(rwalk);

  res = scene_get_enclosure_id_in_closed_boundaries(scn, rwalk->vtx.P, &enc_id);
  if(res != RES_OK) goto error;

  /* Check enclosure consistency */
  if(enc_id != rwalk->enc_id) {
    log_err(scn->dev,
      "%s:%s: invalid solid walk. Unexpected enclosure -- pos=("FORMAT_VECX")\n",
      __FILE__, FUNC_NAME, SPLITX(rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(time_travel)
  (struct sdis_scene* scn,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct sdis_medium* mdm,
   const double alpha, /* Diffusivity, i.e. lambda/(rho*cp) */
   const double t0, /* Initial time [s] */
   const double pos[3], /* Position before the diffusive step */
   double* distance, /* Displacement [m/fp_to_meter] */
   struct temperature* T)
{
  double dir[DIM] = {0};
  double dst = 0; /* Distance [m] */
  double tau = 0; /* Time [s] */
  double x = 0;
  double r = 0;
  double temperature = 0; /* [k] */
  double time = 0; /* [s] */
  res_T res = RES_OK;
  ASSERT(scn && rwalk && rng && alpha > 0 && pos && distance && T);

  dst = *distance * scn->fp_to_meter;
  ASSERT(dst >= 0);

  /* No displacement => no time travel */
  if(dst == 0) goto exit;

  /* Sample x = tau*alpha/distance^2 */
  r = ssp_rng_canonical(rng);
  x = swf_tabulation_inverse(XD(scn->dev->H), SWF_QUADRATIC, r);

  /* Retrieve the time to travel */
  tau = x / alpha * dst * dst;
  time = MMIN(tau, rwalk->vtx.time - t0);

  /* Increment the elapsed time */
  rwalk->elapsed_time += time;

  if(IS_INF(rwalk->vtx.time)) goto exit; /* Steady computation */

  /* Let's take a trip back in time */
  rwalk->vtx.time = MMAX(t0, rwalk->vtx.time - tau);

  /* The path does not reach the initial condition */
  if(rwalk->vtx.time > t0) goto exit;

  /* The path reaches the initial condition. Sample a distance corresponding to
   * the travel time to the initial condition.
   *
   * TODO while we use the H function to sample the distance, one should use the
   * U function. For the moment, this function is not available, hence the use
   * of H. This is not a problem, since we currently assume that the initial
   * condition is uniform. Position is only used for path geometry */
  r = ssp_rng_canonical(rng);
  x = swf_tabulation_inverse(XD(scn->dev->H), SWF_QUADRATIC, r);
  dst = sqrt(alpha * time / x);
  *distance = dst / scn->fp_to_meter; /* Update travel distance */

  /* Uniformly sample a direction and move along it of the distance that
   * separate the path position before diffusion position to its initial
   * condition */
#if DIM == 2
  ssp_ran_circle_uniform(rng, dir, NULL);
#else
  ssp_ran_sphere_uniform(rng, dir, NULL);
#endif
  dX(muld)(dir, dir, *distance);
  dX(add)(rwalk->vtx.P, pos, dir);

  /* Fetch the initial temperature */
  temperature = medium_get_temperature(mdm, &rwalk->vtx);
  if(SDIS_TEMPERATURE_IS_UNKNOWN(temperature)) {
    log_err(scn->dev,
      "%s:%s: the path reaches the initial condition but the "
      "%s temperature remains unknown -- pos=("FORMAT_VECX")\n",
      __FILE__, FUNC_NAME,
      medium_type_to_string(sdis_medium_get_type(mdm)),
      SPLITX(rwalk->vtx.P));
    res = RES_BAD_ARG;
    goto error;
  }

  /* Update the temperature */
  T->value += temperature;
  T->done = 1;

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(handle_volumic_power_wos)
  (struct sdis_scene* scn,
   const struct solid_props* props,
   const double distance, /* [m/fp_to_meter] */
   double* power_term,
   struct temperature* T)
{
  double dst = distance * scn->fp_to_meter; /* [m] */
  double term = 0;
  res_T res = RES_OK;
  ASSERT(scn && props && distance >= 0 && power_term && T);

  if(props->power == SDIS_VOLUMIC_POWER_NONE) goto exit;

  /* No displacement => no power density */
  if(distance == 0) goto exit;

  term = dst*dst / (2*DIM*props->lambda);
  T->value += props->power * term;

exit:
  *power_term = term;
  return res;
}

#if DIM == 2
static INLINE enum sdis_side
compute_hit_side_2d
  (const struct s2d_hit* hit,
   const double delta,
   const double pos[2]) /* Position from which intersection occurs */
{
  struct s2d_attrib p0, p1; /* Segment positions */
  double v0[2] = {0}; /* Vector from segment vertex 0 to segment vertex 1 */
  double v1[2] = {0}; /* Vector from segment vertex 0 to input position */
  double z = 0;

  /* Check pre-conditions */
  ASSERT(hit && delta > 0 && pos && !S2D_HIT_NONE(hit));

  /* Delta is not yet used. It could be used to check confidence in the impact
   * side calculation. A small value of Z (in relation to epsilon) means that the
   * position of the input is close to the boundary and that the calculation of
   * the side must be carried out with care. So far, however, no such problems
   * have been observed in 2D. */
  (void)delta;

  /* Retrieve the positions of the intersected segment */
  S2D(segment_get_vertex_attrib(&hit->prim, 0, S2D_POSITION, &p0));
  S2D(segment_get_vertex_attrib(&hit->prim, 1, S2D_POSITION, &p1));

  v0[0] = p1.value[0] - p0.value[0];
  v0[1] = p1.value[1] - p0.value[1];
  v1[0] = pos[0] - p0.value[0];
  v1[1] = pos[1] - p0.value[1];

  /* Z coordinate of the cross product between v0 and v1. Its sign indicates on
   * which side of the segment the position lies. */
  z = d2_cross(v1, v0);
  return z > 0 ? SDIS_FRONT : SDIS_BACK;
}
#endif

#if DIM == 3
/* May return SDIS_SIDE_NULL__ if the side cannot be calculated with confidence,
 * i.e. if the input position is too close to the boundary and the calculation
 * may therefore present numerical problems */
static INLINE enum sdis_side
compute_hit_side_3d
  (const struct s3d_hit* hit,
   const double delta,
   const double pos[3]) /* Position from which intersection occurs */
{
  struct s3d_attrib v0; /* Position of the 1st triangle vertex */
  double p[3] = {0}; /* Position of the 1st triangle vertex in double */
  double N[3] = {0}; /* Normalized triangle normal */
  double D = 0; /* Last parameter of the plane triangle plane equation */
  double dst = 0; /* Distance of pos to the plane */

  /* Check pre-conditions */
  ASSERT(hit && delta > 0 && pos && !S3D_HIT_NONE(hit));

  /* The distance is close to the border and its calculation can suffer from
   * numerical problems. No side can therefore be estimated with confidence */
  if(hit->distance < 1e-4 && eq_eps(hit->distance, 0, EPSILON_SHELL(delta))) {
    return SDIS_SIDE_NULL__;
  }

  /* Retrieve the positions of the intersected triangle */
  S3D(triangle_get_vertex_attrib(&hit->prim, 0, S3D_POSITION, &v0));
  d3_set_f3(p, v0.value);

  /* Compute the plane equation of the triangle */
  d3_set_f3(N, hit->normal);
  d3_normalize(N, N);
  D = -d3_dot(N, p);

  /* Calculate the distance of the input position from the plane of the triangle
   * and use the sign to define which side of the triangle the position is on */
  dst = d3_dot(N, pos) + D;
  return dst > 0 ? SDIS_FRONT : SDIS_BACK;
}
#endif

/* Verify that the submitted position is in the expected enclosure */
static res_T
XD(check_diffusion_position)
  (struct sdis_scene* scn,
   const unsigned expected_enc_id,
   const double delta, /* Used to adjust thresholds */
   const double pos[DIM])
{
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
  enum sdis_side side = SDIS_SIDE_NULL__;

  struct sXd(hit) hit = SXD_HIT_NULL;
  struct hit_filter_data filter_data = HIT_FILTER_DATA_NULL;
  float wos_pos[DIM] = {0};
  float wos_radius = 0;
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(scn && pos);
  ASSERT(expected_enc_id != ENCLOSURE_ID_NULL);

  /* Filter positions on/near a primitive boundary that don't look towards the
   * query position */
  filter_data.scn = scn;
  filter_data.enc_id = expected_enc_id;

  /* Look for the nearest surface of the position to be checked. By limiting the
   * search radius to delta we speed up the closest point query. If no surface
   * is found, we assume that the position is in the intended medium.  We rely
   * on this assumption because this function is used to verify positions during
   * diffusive random walks. Diffusion algorithms ensure that positions are in
   * the current medium. This function is only concerned with numerical problems
   * which, once the new position has been calculated, position the random walk
   * beyond the medium. In other words, the path jumps a boundary that lies
   * within the numerical imprecision of the calculation, i.e. very close to the
   * position to be verified. So, if no surface is found close to this position,
   * it means that there is no nearby boundary and, consequently, no numerical
   * problem of this kind could have arisen. */
  wos_radius = (float)delta;
  fX_set_dX(wos_pos, pos);
  SXD(scene_view_closest_point
    (scn->sXd(view), wos_pos, wos_radius, &filter_data, &hit));
  if(SXD_HIT_NONE(&hit)) goto exit;

  /* Check path consistency */
  scene_get_enclosure_ids(scn, hit.prim.prim_id, enc_ids);
  side = XD(compute_hit_side)(&hit, delta, pos);
  if(side != SDIS_SIDE_NULL__ && enc_ids[side] != expected_enc_id) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Position is close of the border: check both sides to handle numerical
   * problems in calculating hit side */
  if(side == SDIS_SIDE_NULL__
  && enc_ids[SDIS_FRONT] != expected_enc_id
  && enc_ids[SDIS_BACK]  != expected_enc_id) {
     res = RES_BAD_ARG;
     goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(setup_hit_wos)
  (struct sdis_scene* scn,
   const struct sXd(hit)* hit,
   const double delta,
   struct rwalk* rwalk)
{
  /* Geometry */
  struct sXd(primitive) prim;
  struct sXd(attrib) attr;

  /* Properties */
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
  enum sdis_side side = SDIS_SIDE_NULL__;

  /* Miscellaneous */
  double tgt[DIM] = {0}; /* Target point, i.e. hit position */
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(rwalk && hit);

  /* Find intersected position */
  SXD(scene_view_get_primitive(scn->sXd(view), hit->prim.prim_id, &prim));
#if DIM == 2
  SXD(primitive_get_attrib(&prim, SXD_POSITION, hit->u, &attr));
#else
  SXD(primitive_get_attrib(&prim, SXD_POSITION, hit->uv, &attr));
#endif

  scene_get_enclosure_ids(scn, hit->prim.prim_id, enc_ids);

  /* Calculate on which side the intersection occurs */
  dX_set_fX(tgt, attr.value);
  side = XD(compute_hit_side)(hit, delta, rwalk->vtx.P);

  /* Calculating the side of the intersection can suffer from numerical problems
   * when the position of the path is close to the intersected surface (i.e.
   * side == SDIS_SIDE_NULL). It is therefore reasonable to assume that there is
   * no cause for concern as long as the enclosure identifier on the other side
   * of the triangle is the expected one. */
  if(side == SDIS_SIDE_NULL__) {
    if(rwalk->enc_id == enc_ids[SDIS_FRONT]) side = SDIS_FRONT;
    if(rwalk->enc_id == enc_ids[SDIS_BACK]) side = SDIS_BACK;
  }

  /* Check path consistency */
  if(side == SDIS_SIDE_NULL__ || enc_ids[side] != rwalk->enc_id) {
    res = RES_BAD_OP_IRRECOVERABLE;
    log_err(scn->dev,
      "%s:%s: the conductive path has reached an invalid interface. "
      "Unexpected enclosure -- pos=("FORMAT_VECX"), side=%s\n",
      __FILE__, FUNC_NAME, SPLITX(tgt),
      side == SDIS_FRONT ? "front" : "back");
    goto error;
  }

  /* Random walk update. Do not set the medium to NULL as the intersection is
   * found regardless of time, so the initial condition could be reached before
   * the interface. So we can't yet assume that the random walk has left the
   * current medium */
  dX(set)(rwalk->vtx.P, tgt);
  rwalk->XD(hit) = *hit;
  rwalk->hit_side = side;

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(setup_hit_rt)
  (struct sdis_scene* scn,
   const double pos[DIM],
   const double dir[DIM],
   const struct sXd(hit)* hit,
   struct rwalk* rwalk)
{
  /* Properties */
  unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
  enum sdis_side side = SDIS_SIDE_NULL__;

  /* Miscellaneous */
  double tgt[DIM] = {0}; /* Target point, i.e. hit position */
  double N[DIM] = {0};
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(pos && dir && rwalk && hit);
  ASSERT(dX(is_normalized)(dir));

  /* Calculate on which side the intersection occurs */
  dX(muld)(tgt, dir, hit->distance);
  dX(add)(tgt, tgt, pos);
  dX_set_fX(N, hit->normal);
  dX(normalize)(N, N);
  side = dX(dot)(N, dir) > 0 ? SDIS_BACK : SDIS_FRONT;

  /* Fetch interface properties and check path consistency */
  scene_get_enclosure_ids(scn, hit->prim.prim_id, enc_ids);
  if(enc_ids[side] != rwalk->enc_id) {
    res = RES_BAD_OP;
    goto error;
  }

  /* Random walk update. Do not set the medium to NULL as the intersection is
   * found regardless of time, so the initial condition could be reached before
   * the interface. So we can't yet assume that the random walk has left the
   * current medium */
  dX(set)(rwalk->vtx.P, tgt);
  rwalk->XD(hit) = *hit;
  rwalk->hit_side = side;

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(sample_next_position)
  (struct sdis_scene* scn,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   const double delta, /* Used to adjust thresholds */
   double* distance) /* Displacement distance */
{
  /* Intersection */
  struct sXd(hit) hit = SXD_HIT_NULL;

  /* Walk on sphere */
  double wos_distance = 0;
  double wos_epsilon = 0;
  float wos_pos[DIM] = {0};
  float wos_radius = 0;

  /* Miscellaneous */
  res_T res = RES_OK;
  ASSERT(rwalk && rng && distance);

  /* Find the closest distance from the current position to the geometry */
  wos_radius = (float)INF;
  fX_set_dX(wos_pos, rwalk->vtx.P);
  SXD(scene_view_closest_point(scn->sXd(view), wos_pos, wos_radius, NULL, &hit));
  CHK(!SXD_HIT_NONE(&hit));
  wos_distance = hit.distance;

  /* The current position is in the epsilon shell,
   * move it to the nearest interface position */
  wos_epsilon = EPSILON_SHELL(delta);
  if(wos_distance <= wos_epsilon) {
    res = XD(setup_hit_wos)(scn, &hit, delta, rwalk);
    if(res != RES_OK) goto error;

  /* Uniformly sample a new position on the surrounding sphere */
  } else {
    double pos[DIM] = {0};
    double dir[DIM] = {0};

#if DIM == 2
    ssp_ran_circle_uniform(rng, dir, NULL);
#else
    ssp_ran_sphere_uniform(rng, dir, NULL);
#endif
    dX(muld)(pos, dir, (double)hit.distance);
    dX(add)(pos, pos, rwalk->vtx.P);

    /* Check that the new position is in the intended medium. Please note that
     * we do not use the scene_get_medium_in_closed_boundaries function. It uses
     * the ray-tracing operator, which has its own numerical uncertainty that is
     * not the same as that of the closest point operator used by this
     * scattering algorithm. It can therefore return the expected medium,
     * whereas the nearest point operator would return an inconsistent medium.
     * The next diffusion step would then detect an error. This is why we use a
     * new function based on the same geometric operator used in the present
     * algorithm. */
    res = XD(check_diffusion_position)(scn, rwalk->enc_id, delta, pos);

    /* Diffusion position is valid => move the path to the new position */
    if(res == RES_OK) {
      dX(set)(rwalk->vtx.P, pos);

    /* As a result, the new position is detected as being in the wrong medium.
     * This means that there has been a numerical problem in moving the
     * position, which has therefore jumped the solid boundary. To solve this
     * problem, we can move the trajectory on the solid interface along the
     * direction of displacement. Indeed, we can assume that the position we
     * want to move to is actually inside the epsilon shell. In this case, the
     * trajectory will be moved to this interface in the next step anyway. */
    } else {
      struct sXd(hit) hit_rt = SXD_HIT_NULL;
      float rt_pos[DIM] = {0};
      float rt_dir[DIM] = {0};
      float rt_range[2] = {0, 0};

      fX_set_dX(rt_pos, rwalk->vtx.P);
      fX_set_dX(rt_dir, dir);
      rt_range[0] = 0;
      rt_range[1] = (float)INF;
      SXD(scene_view_trace_ray
        (scn->sXd(view), rt_pos, rt_dir, rt_range, NULL, &hit_rt));

      if(SXD_HIT_NONE(&hit_rt)) {
        /* The lack of intersection is probably due to a current position close
         * to the boundary. And although it is detected in the solid by WoS, the
         * specific numerical errors of the ray-tracing operator may contradict
         * the WoS algorithm, which relies on the closest point operator. But
         * since the position is close to the boundary, it can be snaped to it*/
        res = XD(setup_hit_wos)(scn, &hit, delta, rwalk);
        if(res != RES_OK) goto error;

      } else {
        res = XD(setup_hit_rt)(scn, rwalk->vtx.P, dir, &hit_rt, rwalk);
        if(res != RES_OK) {
          /* An error occurs while handling ray intersection. This means that
           * the Ray-Tracing operator find an invalid intersection regarding the
           * current enclosure in which the path should be sampled. As in the
           * case of the lack of intersection (see above) this means that the
           * position is close to the enclosure boundary and that the ray missed
           * it. So, As previously, the position can be simply snaped to it
           * since one can assumes that the current position is in the right
           * enclosure */
          res = XD(setup_hit_wos)(scn, &hit, delta, rwalk);
          if(res != RES_OK) goto error;
        }
      }
    }
  }

exit:
  *distance = hit.distance;
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
res_T
XD(conductive_path_wos)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  /* Properties */
  const struct enclosure* enc = NULL;
  struct sdis_medium* mdm = NULL;
  struct solid_props props_ref = SOLID_PROPS_NULL;
  struct solid_props props = SOLID_PROPS_NULL;
  double alpha = 0; /* diffusivity, i.e. lambda/(rho*cp) */

  /* Miscellaneous */
  size_t ndiffusion_steps = 0; /* For debug */
  double green_power_term = 0;
  int green = 0;
  const int wos = 1;
  res_T res = RES_OK;
  (void)ctx; /* Avoid the "unused variable" warning */

  /* Check pre-conditions */
  ASSERT(scn && ctx && rwalk && rng && T);

  /* Is green evaluated evaluated */
  green = ctx->green_path != NULL;

  res = XD(check_enclosure_consistency)(scn, rwalk);
  if(res != RES_OK) goto error;

  /* Get the enclosure medium */
  enc = scene_get_enclosure(scn, rwalk->enc_id);
  res = scene_get_enclosure_medium(scn, enc, &mdm);
  if(res != RES_OK) goto error;
  ASSERT(sdis_medium_get_type(mdm) == SDIS_SOLID);

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
  props = props_ref;

  /* The algorithm assumes that lambda, rho and cp are constants. The
   * diffusivity of the material (alpha) can therefore be calculated once */
  alpha = props_ref.lambda / (props_ref.rho * props_ref.cp);

  /* Sample a diffusive path */
  for(;;) {
    double power_term = 0; /* */
    double pos[3] = {0,0,0}; /* Position before diffusive step */
    double dst = 0; /* [m/fp_to_meter] */

    /* Register the new vertex against the heat path */
    #define REGISTER_HEAT_VERTEX {                                             \
      res = register_heat_vertex(ctx->heat_path, &rwalk->vtx, T->value,        \
        SDIS_HEAT_VERTEX_CONDUCTION, (int)ctx->nbranchings);                   \
      if(res != RES_OK) goto error;                                            \
    } (void)0

    /* The temperature is known */
    if(SDIS_TEMPERATURE_IS_KNOWN(props.temperature)) {
      REGISTER_HEAT_VERTEX;
      T->value += props.temperature;
      T->done = 1;
      break;
    }

    d3_set(pos, rwalk->vtx.P);

    /* Find the next position of the conductive path */
    res = XD(sample_next_position)(scn, rwalk, rng, props.delta, &dst);
    if(res != RES_OK) goto error;

    /* Going back in time */
    res = XD(time_travel)(scn, rwalk, rng, mdm, alpha, props.t0, pos, &dst, T);
    if(res != RES_OK) goto error;

    /* Add the volumic power density */
    res = XD(handle_volumic_power_wos)(scn, &props, dst, &power_term, T);
    if(res != RES_OK) goto error;

    REGISTER_HEAT_VERTEX;

    /* Accumulate the power term */
    if(green) green_power_term += power_term;

    /* The path reaches the initial condition */
    if(T->done) {
      T->func = NULL;
      break;
    }

    /* The path reaches a boundary */
    if(!SXD_HIT_NONE(&rwalk->XD(hit))) {
      T->func = XD(boundary_path);
      rwalk->enc_id = ENCLOSURE_ID_NULL;
      break;
    }

    #undef REGISTER_VERTEX

    /* Retreive and check solid properties at the new position */
    res = solid_get_properties(mdm, &rwalk->vtx, &props);
    if(res != RES_OK) goto error;
    res = check_solid_constant_properties(scn->dev, green, wos, &props_ref, &props);
    if(res != RES_OK) goto error;

    ++ndiffusion_steps; /* For debug */
  }

  /* Save green function data */
  res = update_green_path
    (ctx->green_path, rwalk, mdm, &props_ref, green_power_term, T);

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
