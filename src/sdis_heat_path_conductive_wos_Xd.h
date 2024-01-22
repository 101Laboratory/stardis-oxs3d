/* Copyright (C) 2016-2023 |Méso|Star> (contact@meso-star.com)
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

#include "sdis_heat_path_conductive_c.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static res_T
XD(check_medium_consistency)
  (struct sdis_scene* scn,
   const struct XD(rwalk)* rwalk)
{
  struct sdis_medium* mdm = NULL;
  res_T res = RES_OK;
  ASSERT(rwalk);

  res = scene_get_medium_in_closed_boundaries(scn, rwalk->vtx.P, &mdm);
  if(res != RES_OK) goto error;

  /* Check medium consistency */
  if(mdm != rwalk->mdm) {
    log_err(scn->dev,
      "%s:%s: invalid solid walk. Unexpected medium (position: "FORMAT_VECX").\n",
      __FILE__, FUNC_NAME, SPLITX(rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#if DIM == 2
static INLINE enum sdis_side
compute_hit_side_2d
  (const struct s2d_hit* hit,
   const double pos[2]) /* Position from which intersection occurs */
{
  struct s2d_attrib p0, p1; /* Segment positions */
  double v0[2] = {0}; /* Vector from segment vertex 0 to segment vertex 1 */
  double v1[2] = {0}; /* Vector from segment vertex 0 to input position */
  double z = 0;

  /* Check pre-conditions */
  ASSERT(scn && hit && pos && !S2D_HIT_NONE(hit));

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
static INLINE enum sdis_side
compute_hit_side_3d
  (const struct s3d_hit* hit,
   const double pos[3]) /* Position from which intersection occurs */
{
  struct s3d_attrib v0; /* Position of the 1st triangle vertex */
  double p[3] = {0}; /* Position of the 1st triangle vertex in double */
  double N[3] = {0}; /* Normalized triangle normal */
  double D = 0; /* Last parameter of the plane triangle plane equation */
  double dst = 0; /* Distance of pos to the plane */

  /* Check pre-conditions */
  ASSERT(scn && hit && pos && !S3D_HIT_NONE(hit));

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

/* Verify that the submitted position is in the expected medium */
static res_T
XD(check_diffusion_position)
  (struct sdis_scene* scn,
   const struct sdis_medium* expected_medium,
   const double pos[DIM])
{
  struct sdis_interface* interf = NULL;
  struct sdis_medium* mdm = NULL;
  enum sdis_side side;

  struct sXd(hit) hit = SXD_HIT_NULL;
  float wos_pos[DIM] = {0};
  float wos_radius = 0;
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(scn && expected_medium && pos);

  /* Look for the nearest surface within 1 mm of the position to be checked. By
   * limiting the search radius we speed up the closest point query. If no
   * surface is found, we assume that the position is in the intended medium.
   * We rely on this assumption because this function is used to verify
   * positions during diffusive random walks. Diffusion algorithms ensure that
   * positions are in the current medium. This function is only concerned with
   * numerical problems which, once the new position has been calculated,
   * position the random walk beyond the medium. In other words, the path jumps
   * a boundary that lies within the numerical imprecision of the calculation,
   * i.e. very close to the position to be verified. So, if no surface is found
   * close to this position, it means that there is no nearby boundary and,
   * consequently, no numerical problem of this kind could have arisen. */
  wos_radius = (float)(scn->fp_to_meter * 1.0e-3);
  fX_set_dX(wos_pos, pos);
  SXD(scene_view_closest_point(scn->sXd(view), wos_pos, wos_radius, NULL, &hit));
  if(SXD_HIT_NONE(&hit)) goto exit;

  /* Fetch interface properties and check path consistency */
  interf = scene_get_interface(scn, hit.prim.prim_id);
  side = XD(compute_hit_side)(&hit, pos);
  mdm = side == SDIS_FRONT ? interf->medium_front : interf->medium_back;
  if(mdm != expected_medium) {
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
   struct XD(rwalk)* rwalk)
{
  /* Geometry */
  struct sXd(primitive) prim;
  struct sXd(attrib) attr;

  /* Properties */
  struct sdis_interface* interf = NULL;
  struct sdis_medium* mdm = NULL;
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

  /* Calculate on which side the intersection occurs */
  dX_set_fX(tgt, attr.value);
  side = XD(compute_hit_side)(hit, rwalk->vtx.P);

  /* Fetch interface properties and check path consistency */
  interf = scene_get_interface(scn, hit->prim.prim_id);
  mdm = side == SDIS_FRONT ? interf->medium_front : interf->medium_back;
  if(mdm != rwalk->mdm) {
    log_err(scn->dev,
      "%s:%s: the conductive path has reached an invalid interface; "
      "unexpected medium (position: "FORMAT_VECX"; side: %s).\n",
      __FILE__, FUNC_NAME, SPLITX(tgt), side == SDIS_FRONT ? "front" : "back");
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  /* Update the random walk */
  dX(set)(rwalk->vtx.P, tgt);
  rwalk->hit = *hit;
  rwalk->hit_side = side;
  rwalk->mdm = NULL;

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
   struct XD(rwalk)* rwalk)
{
  /* Properties */
  struct sdis_interface* interf = NULL;
  struct sdis_medium* mdm = NULL;
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
  interf = scene_get_interface(scn, hit->prim.prim_id);
  mdm = side == SDIS_FRONT ? interf->medium_front : interf->medium_back;
  if(mdm != rwalk->mdm) {
    log_err(scn->dev,
      "%s:%s: the conductive path has reached an invalid interface; "
      "unexpected medium (position: "FORMAT_VECX"; side: %s).\n",
      __FILE__, FUNC_NAME, SPLITX(tgt), side == SDIS_FRONT ? "front" : "back");
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

  /* Update the random walk */
  dX(set)(rwalk->vtx.P, tgt);
  rwalk->hit = *hit;
  rwalk->hit_side = side;
  rwalk->mdm = NULL;

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(sample_next_position)
  (struct sdis_scene* scn,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng)
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
  ASSERT(rwalk && rng);

  /* Find the closest distance from the current position to the geometry */
  wos_radius = (float)INF;
  fX_set_dX(wos_pos, rwalk->vtx.P);
  SXD(scene_view_closest_point(scn->sXd(view), wos_pos, wos_radius, NULL, &hit));
  CHK(!SXD_HIT_NONE(&hit));
  wos_distance = hit.distance;

  /* The current position is in the epsilon shell:
   * move it to the nearest interface position */
  wos_epsilon = scn->fp_to_meter*1.e-6;
  if(wos_distance <= wos_epsilon) {
    res = XD(setup_hit_wos)(scn, &hit, rwalk);
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
    res = XD(check_diffusion_position)(scn, rwalk->mdm, pos);

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
      float rt_pos[DIM] = {0};
      float rt_dir[DIM] = {0};
      float rt_range[2] = {0, 0};

      fX_set_dX(rt_pos, rwalk->vtx.P);
      fX_set_dX(rt_dir, dir);
      rt_range[0] = 0;
      rt_range[1] = (float)INF;
      SXD(scene_view_trace_ray(scn->sXd(view), rt_pos, rt_dir, rt_range, NULL, &hit));

      /* An intersection should be found. If not, we can do nothing and simply
       * reject the path.
       *
       * TODO: we could take the treatment of numerical problems a step further
       * by sampling other directions and trying to move in them again. But at
       * present, and until we have proof to the contrary, we assume that the
       * rejection of a path should not occur, or that it will be so rare that
       * we don't care to save it. */
      if(SXD_HIT_NONE(&hit)) {
        log_err(scn->dev,
          "%s:%s: unable to find the next diffusion position "
          "(position: "FORMAT_VECX"; direction: "FORMAT_VECX"; distance: %g\n",
          __FILE__, FUNC_NAME, SPLITX(pos), SPLITX(dir), wos_distance);
        res = RES_BAD_OP_IRRECOVERABLE;
        goto error;
      }

      res = XD(setup_hit_rt)(scn, rwalk->vtx.P, dir, &hit, rwalk);
      if(res != RES_OK) goto error;
    }
  }

exit:
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
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  /* Properties */
  struct solid_props props_ref = SOLID_PROPS_NULL;
  struct solid_props props = SOLID_PROPS_NULL;

  /* Miscellaneous */
  size_t ndiffusion_steps = 0; /* For debug */
  int eval_green = 0;
  res_T res = RES_OK;
  (void)ctx; /* Avoid the "unused variable" warning */

  /* Check pre-conditions */
  ASSERT(scn && ctx && rwalk && rng && T);
  ASSERT(sdis_medium_get_type(rwalk->mdm) == SDIS_SOLID);

  res = XD(check_medium_consistency)(scn, rwalk);
  if(res != RES_OK) goto error;

  /* Retrieve the solid properties at the current position. Use them to verify
   * that those that are supposed to be constant by the conductive random walk
   * remain the same. Note that we take care of the same constraints on the
   * solid reinjection since once reinjected, the position of the random walk
   * is that at the beginning of the conductive random walk. Thus, after a
   * reinjection, the next line retrieves the properties of the reinjection
   * position. By comparing them to the properties along the random walk, we
   * thus verify that the properties are constant throughout the random walk
   * with respect to the properties of the reinjected position. */
  solid_get_properties(rwalk->mdm, &rwalk->vtx, &props_ref);

  /* Sample a diffusive path */
  for(;;) {

    /* Find the next position of the conductive path */
    res = XD(sample_next_position)(scn, rwalk, rng);
    if(res != RES_OK) goto error;

    /* The path reaches a boundary */
    if(!SXD_HIT_NONE(&rwalk->hit)) break;

    /* Retreive and check solid properties at the new position */
    res = solid_get_properties(rwalk->mdm, &rwalk->vtx, &props);
    if(res != RES_OK) goto error;
    res = check_solid_constant_properties(scn->dev, eval_green, &props_ref, &props);
    if(res != RES_OK) goto error;

    /* The temperature is known, the path has reached a limit condition */
    if(props.temperature >= 0) {
      T->value += props.temperature;
      T->done = 1;
      break;
    }

    ++ndiffusion_steps;
  }

  T->func = XD(boundary_path);
  ASSERT(rwalk->mdm == NULL);

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
