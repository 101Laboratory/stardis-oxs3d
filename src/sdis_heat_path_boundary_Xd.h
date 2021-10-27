/* Copyright (C) 2016-2021 |Meso|Star> (contact@meso-star.com)
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
#include "sdis_heat_path_boundary_c.h"
#include "sdis_interface_c.h"
#include "sdis_medium_c.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(boundary_path)
  (struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct ssp_rng* rng,
   struct XD(temperature)* T)
{
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* mdm_front = NULL;
  struct sdis_medium* mdm_back = NULL;
  struct sdis_medium* mdm = NULL;
  double tmp;
  res_T res = RES_OK;
  ASSERT(scn && ctx && rwalk && rng && T);
  ASSERT(rwalk->mdm == NULL);
  ASSERT(!SXD_HIT_NONE(&rwalk->hit));

  XD(setup_interface_fragment)(&frag, &rwalk->vtx, &rwalk->hit, rwalk->hit_side);

  fX(normalize)(rwalk->hit.normal, rwalk->hit.normal);

  /* Retrieve the current interface */
  interf = scene_get_interface(scn, rwalk->hit.prim.prim_id);

  /* Check if the boundary temperature is known */
  tmp = interface_side_get_temperature(interf, &frag);
  if(tmp >= 0) {
    T->value += tmp;
    T->done = 1;

    if(ctx->green_path) {
      res = green_path_set_limit_interface_fragment
        (ctx->green_path, interf, &frag, rwalk->elapsed_time);
      if(res != RES_OK) goto error;
    }
    if(ctx->heat_path) {
      heat_path_get_last_vertex(ctx->heat_path)->weight = T->value;
    }
    goto exit;
  }

  /* Check if the boundary flux is known. Note that currently, only solid media
   * can have a flux as limit condition */
  mdm = interface_get_medium(interf, frag.side);
  if(sdis_medium_get_type(mdm) == SDIS_SOLID) {
    const double phi = interface_side_get_flux(interf, &frag);
    if(phi != SDIS_FLUX_NONE) {
      res = XD(solid_boundary_with_flux_path)
        (scn, ctx, &frag, phi, rwalk, rng, T);
      if(res != RES_OK) goto error;

      goto exit;
    }
  }

  mdm_front = interface_get_medium(interf, SDIS_FRONT);
  mdm_back = interface_get_medium(interf, SDIS_BACK);

  if(mdm_front->type == mdm_back->type) {
    res = XD(solid_solid_boundary_path)(scn, ctx, &frag, rwalk, rng, T);
  } else {
    res = XD(solid_fluid_boundary_picard1_path)(scn, ctx, &frag, rwalk, rng, T);
  }
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"

