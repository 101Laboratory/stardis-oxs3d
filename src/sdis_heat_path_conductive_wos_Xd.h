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
      "%s: invalid solid walk. Unexpected medium (position: "FORMAT_VECX").\n",
      FUNC_NAME, SPLITX(rwalk->vtx.P));
    res = RES_BAD_OP_IRRECOVERABLE;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(setup_hit)
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
  double dir[DIM] = {0}; /* Direction along which intersection occurs */
  double N[DIM] = {0}; /* Normal of the intersected triangle */
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
  dX_set_fX(N, hit->normal);
  dX(sub)(dir, tgt, rwalk->vtx.P);
  dX(normalize)(dir, dir);
  dX(normalize)(N, N);
  side = dX(dot)(N, dir) > 0 ? SDIS_BACK : SDIS_FRONT;

  /* Fetch interface properties and check path consistency */
  interf = scene_get_interface(scn, hit->prim.prim_id);
  mdm = side == SDIS_FRONT ? interf->medium_front : interf->medium_back;
  if(mdm != rwalk->mdm) {
    log_err(scn->dev,
      "%s: the conductive path has reached an invalid interface; "
      "unexpected medium (position: "FORMAT_VECX"; side: %s).\n",
      FUNC_NAME, SPLITX(tgt), side == SDIS_FRONT ? "front" : "back");
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

  /* Step */
  double pos[DIM] = {0};
  double dir[DIM] = {0};

  /* Walk on sphere */
  float wos_pos[DIM] = {0};
  float wos_radius = 0;
  float wos_epsilon = 0;

  res_T res = RES_OK;
  ASSERT(rwalk && rng);

  /* Find the closest distance from the current position to the geometry */
  wos_radius = (float)INF;
  fX_set_dX(wos_pos, rwalk->vtx.P);
  SXD(scene_view_closest_point(scn->sXd(view), wos_pos, wos_radius, NULL, &hit));

  /* The current position is in the epsilon shell:
   * move it to the nearest interface position */
  wos_epsilon = (float)(scn->fp_to_meter*1.e-6);
  if(hit.distance <= wos_epsilon) {
    XD(setup_hit)(scn, &hit, rwalk);
    goto exit;
  }

  /* Uniformly sample a new position on the surrounding sphere */
#if DIM == 2
  ssp_ran_circle_uniform(rng, dir, NULL);
#else
  ssp_ran_sphere_uniform(rng, dir, NULL);
#endif
  dX(muld)(pos, dir, (double)hit.distance);
  dX(add)(pos, pos, rwalk->vtx.P);

  /* TODO check that the new position is in the expected medium. The
   * inconsistency of the medium means that there were numerical problems in
   * moving the position. We can instead move the path to the solid interface,
   * since we can assume that the invalid position is actually in the epsilon
   * shell. */

  /* Set new path position */
  dX(set)(rwalk->vtx.P, pos);

exit:
  return res;
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
  }

  T->func = XD(boundary_path);
  ASSERT(rwalk->mdm = NULL);

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
