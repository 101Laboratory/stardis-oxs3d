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
#include "sdis_heat_path.h"
#include "sdis_heat_path_conductive_c.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <rsys/cstr.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(conductive_path)
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  struct sdis_medium* mdm = NULL;
  unsigned enc_id = ENCLOSURE_ID_NULL;
  res_T res = RES_OK;
  ASSERT(ctx && rwalk);

  res = scene_get_enclosure_id_in_closed_boundaries(scn, rwalk->vtx.P, &enc_id);
  if(res != RES_OK) goto error;
  res = scene_get_enclosure_medium(scn, scene_get_enclosure(scn, enc_id), &mdm);
  if(res != RES_OK) goto error;
  ASSERT(sdis_medium_get_type(mdm) == SDIS_SOLID);

  /* The caller defined a custom function to sample the solid path */
  if(mdm->shader.solid.sample_path != NULL) {
    res = XD(conductive_path_custom)(scn, enc_id, mdm, rwalk, rng, T);
    if(res != RES_OK) goto error;

  } else {
    switch(ctx->diff_algo) {
      case SDIS_DIFFUSION_DELTA_SPHERE:
        res = XD(conductive_path_delta_sphere)(scn, ctx, rwalk, rng, T);
        break;
      case SDIS_DIFFUSION_WOS:
        res = XD(conductive_path_wos)(scn, ctx, rwalk, rng, T);
        break;
      default: FATAL("Unreachable code.\n"); break;
    }
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
