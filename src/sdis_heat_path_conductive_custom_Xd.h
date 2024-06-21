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
   const struct sdis_path* path)
{
  const struct sXd(hit)* hit = NULL;
  res_T res = RES_OK;
  ASSERT(scn && path);

  hit = &path->XD(hit);

  /* Check end of path */
  if(!path->at_limit && SXD_HIT_NONE(hit)) {
    log_err(scn->dev,
      "%s: the sampled path should have reached a limit condition or a boundary"
      " -- pos=("FORMAT_VECX")\n",
      FUNC_NAME, SPLITX(path->vtx.P));
    res = RES_BAD_ARG;
    goto error;
  }

  if(!SXD_HIT_NONE(&path->XD(hit))) {
    struct sXd(attrib) attr;
    struct sXd(primitive) prim;
    const unsigned iprim = hit->prim.prim_id;
    float N[3] = {0,0,0};

    /* Check intersected primitive */
    res = sXd(scene_view_get_primitive)(scn->sXd(view), iprim, &prim);
    if(res != RES_OK) {
      log_err(scn->dev,
        "%s: invalid intersected primitive on sampled path -- %s\n",
        FUNC_NAME, res_to_cstr(res));
      goto error;
    }

    /* Check normal */
#if DIM == 2
    SXD(primitive_get_attrib(&prim, SXD_GEOMETRY_NORMAL, hit->u, &attr));
#else
    SXD(primitive_get_attrib(&prim, SXD_GEOMETRY_NORMAL, hit->uv, &attr));
#endif
    fX(normalize)(attr.value, attr.value);
    fX(normalize)(N, hit->normal);
    if(!fX(eq)(attr.value, N)) {
      log_err(scn->dev,
        "%s: invalid normal on the intersected primitive\n",
        FUNC_NAME);
      res = RES_BAD_ARG;
      goto error;
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
  ASSERT(scn && rwalk && rng && T);

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

  res = XD(check_sampled_path)(scn, &path);
  if(res != RES_OK) goto error;

  /* Update random walk position and time from sampled path */
  rwalk->vtx = path.vtx;
  rwalk->XD(hit) = path.XD(hit);
  rwalk->elapsed_time += path.elapsed_time;

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
