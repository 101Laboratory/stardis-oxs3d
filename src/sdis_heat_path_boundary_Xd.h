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
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T)
{
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* mdm_front = NULL;
  struct sdis_medium* mdm_back = NULL;
  double tmp;
  res_T res = RES_OK;
  ASSERT(scn && ctx && rwalk && rng && T);
  ASSERT(rwalk->enc_id == ENCLOSURE_ID_NULL);
  ASSERT(!SXD_HIT_NONE(&rwalk->XD(hit)));

  XD(setup_interface_fragment)
    (&frag, &rwalk->vtx, &rwalk->XD(hit), rwalk->hit_side);

  fX(normalize)(rwalk->XD(hit).normal, rwalk->XD(hit).normal);

  /* Retrieve the current interface */
  interf = scene_get_interface(scn, rwalk->XD(hit).prim.prim_id);

  /* Check if the boundary temperature is known */
  tmp = interface_side_get_temperature(interf, &frag);
  if(SDIS_TEMPERATURE_IS_KNOWN(tmp)) {
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

  mdm_front = interface_get_medium(interf, SDIS_FRONT);
  mdm_back = interface_get_medium(interf, SDIS_BACK);

  if(mdm_front->type == mdm_back->type) {
    res = XD(solid_solid_boundary_path)(scn, ctx, &frag, rwalk, rng, T);
  } else if(ctx->nbranchings == ctx->max_branchings) {
    res = XD(solid_fluid_boundary_picard1_path)(scn, ctx, &frag, rwalk, rng, T);
  } else {
    ASSERT(ctx->nbranchings < ctx->max_branchings);
    res = XD(solid_fluid_boundary_picardN_path)(scn, ctx, &frag, rwalk, rng, T);
  }
  if(res != RES_OK) goto error;

  if(T->done) goto exit;

  /* Handling limit boundary condition, i.e. the trajectory originates from a
   * boundary and the medium temperature is known (e.g. Robin's condition). To
   * simplify data description, we allow in such a situation, to define several
   * medium on the same enclosure, each with a fixed temperature, i.e. different
   * conditions are defined for the different interfaces that detour the
   * enclosure. As a result, no path can be sampled in this enclosure, which is
   * beyond the system boundary. The boundary medium must therefore be
   * interrogated from the interface.
   *
   * Note that we check this boundary condition with convective paths to handle
   * Robin's boundary conditions. But we also make this check when passing
   * through conduction when there's no reason why a solid should have a fixed
   * temperature and not its boundary: it should be a Dirichlet condition.
   * Although it's not physical, such systems can still be defined
   * computationally, and in fact it's also a handy way of testing
   * border cases */
  if(T->func == XD(convective_path) || T->func == XD(conductive_path)) {
    res = XD(query_medium_temperature_from_boundary)(scn, ctx, rwalk, T);
    if(res != RES_OK) goto error;
    if(T->done) goto exit; /* That's all folks */
  }

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
