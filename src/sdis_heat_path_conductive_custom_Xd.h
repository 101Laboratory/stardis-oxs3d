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

#include "sdis.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <rsys/cstr.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static res_T
XD(check_sampled_path)
  (struct sdis_scene* scn,
   const struct rwalk* rwalk,
   const struct sdis_path* path)
{
  int null_prim = 0;
  res_T res = RES_OK;
  ASSERT(scn && path);

  null_prim = SXD_PRIMITIVE_EQ(&path->XD(prim), &SXD_PRIMITIVE_NULL);

  /* Check end of path */
  if(!path->at_limit && null_prim) {
    log_err(scn->dev,
      "%s: the sampled path should have reached a limit condition or a boundary"
      " -- pos=("FORMAT_VECX")\n",
      FUNC_NAME, SPLITX(path->vtx.P));
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check path time */
  if(path->vtx.time > rwalk->vtx.time) {
    log_err(scn->dev,
      "%s: the sampled trajectory cannot be in the future. "
      "It can only go back in time -- starting time=%g s; current time=%g s\n",
      FUNC_NAME, rwalk->vtx.time, path->vtx.time);
    res = RES_BAD_ARG;
    goto error;
  }

  if(!null_prim) {
    struct sXd(primitive) prim;
    const unsigned iprim = path->XD(prim).prim_id;

    /* Check intersected primitive */
    res = sXd(scene_view_get_primitive)(scn->sXd(view), iprim, &prim);
    if(res != RES_OK || !SXD_PRIMITIVE_EQ(&path->XD(prim), &prim)) {
      log_err(scn->dev,
        "%s: invalid intersected primitive on sampled path -- %s\n",
        FUNC_NAME, res_to_cstr(res));
      goto error;
    }
  }

exit:
  return res;
error:
  goto exit;
}

static int
XD(keep_only_one_primitive)
  (const struct sXd(hit)* hit,
   const float org[DIM],
   const float dir[DIM],
   const float range[2],
   void* query_data,
   void* filter_data)
{
  const struct sXd(primitive)* prim = query_data;
  (void)org, (void)dir, (void)range, (void)filter_data;
  return !SXD_PRIMITIVE_EQ(prim, &hit->prim);
}

static res_T
XD(get_path_hit)
  (struct sdis_scene* scn,
   struct sdis_path* path,
   const struct sdis_medium* mdm,
   struct sXd(hit)* hit)
{
  struct hit_filter_data filter_data = HIT_FILTER_DATA_NULL;
  float query_radius = 0;
  float query_pos[DIM] = {0};
  double delta = 0;
  res_T res = RES_OK;
  ASSERT(scn && path && hit);

  filter_data.XD(custom_filter) = XD(keep_only_one_primitive);
  filter_data.custom_filter_data = &path->XD(prim);

  /* Search for the hit corresponding to the path position on a primitive. Search
   * at a maximum distance from the delta of the medium, as this hit should be
   * very close to the submitted position, since it should represent the same
   * point. */
  delta = solid_get_delta(mdm, &path->vtx);
  fX_set_dX(query_pos, path->vtx.P);
  query_radius = (float)delta;
  SXD(scene_view_closest_point(scn->sXd(view), query_pos, query_radius,
      &filter_data, hit));
  ASSERT(SXD_PRIMITIVE_EQ(&hit->prim, &path->XD(prim)));

  if(SXD_HIT_NONE(hit)) {
    log_warn(scn->dev,
      "%s: the position returned by custom sampling of the conductive path "
      "is too far from the primitive it should be on -- "
      "query position=("FORMAT_VECX"); search distance=%g\n",
      FUNC_NAME, SPLITX(path->vtx.P), delta);
    res = RES_BAD_OP;
    goto error;
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
XD(conductive_path_custom)
  (struct sdis_scene* scn,
   const unsigned enc_id,
   const struct sdis_medium* mdm,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  struct sdis_path path = SDIS_PATH_NULL;
  res_T res = RES_OK;

  /* Check pre-conditions */
  ASSERT(scn && rwalk && rng && T);
  ASSERT(sdis_medium_get_type(mdm) == SDIS_SOLID);
  ASSERT(mdm->shader.solid.sample_path);

  /* Sample a conductive path */
  path.vtx = rwalk->vtx;
  res = mdm->shader.solid.sample_path(scn, rng, &path, mdm->data);
  if(res != RES_OK) {
    log_err(scn->dev,
      "%s: error in customized sampling of a conductive path "
      "from pos=("FORMAT_VECX") -- %s\n",
      FUNC_NAME, SPLITX(rwalk->vtx.P), res_to_cstr(res));
    goto error;
  }

  res = XD(check_sampled_path)(scn, rwalk, &path);
  if(res!= RES_OK) goto error;

  /* Update random walk position and time from sampled path */
  rwalk->elapsed_time += rwalk->vtx.time - path.vtx.time;
  rwalk->vtx = path.vtx;

  /* Calculate path intersection with geometry if it hasn't already reached a
   * boundary condition */
  if(!path.at_limit) {
    res = XD(get_path_hit)(scn, &path, mdm, &rwalk->XD(hit));
    if(res != RES_OK) goto error;
  }


  /* The path reached a boundary */
  if(!SXD_HIT_NONE(&rwalk->XD(hit))) {
    unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};

    rwalk->enc_id = ENCLOSURE_ID_NULL; /* Not in an enclosure */

      /* Define which side of the interface the path is on */
    scene_get_enclosure_ids(scn, rwalk->XD(hit).prim.prim_id, enc_ids);
         if(enc_id == enc_ids[SDIS_FRONT]) rwalk->hit_side = SDIS_FRONT;
    else if(enc_id == enc_ids[SDIS_BACK])  rwalk->hit_side = SDIS_BACK;
    else FATAL("Unreachable code.\n");
  }

  /* Update Monte Carlo weight */
  T->value += path.weight;
  T->done = path.at_limit;

  /* The path either reaches a boundary condition and will be stopped, or is
   * on a boundary and a boundary path must be sampled */
  T->func = XD(boundary_path);

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
