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

#include "sdis_heat_path_boundary_c.h"
#include "sdis_interface_c.h"
#include "sdis_log.h"
#include "sdis_scene_c.h"

#include <rsys/cstr.h> /* res_to_cstr */

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Generic helper functions
 ******************************************************************************/
static INLINE res_T
XD(check_handle_external_net_flux_args)
  (const struct sdis_device* dev,
   const char* func_name,
   const struct XD(handle_external_net_flux_args)* args)
{
  sdis_interface_sample_external_sources_T functor = NULL;
  res_T res = RES_OK;

  /* Handle bugs */
  ASSERT(dev && func_name && args);
  ASSERT(args->interf && args->frag);
  ASSERT(!SXD_HIT_NONE(args->hit));

  functor = interface_side_get_external_sources_sampling_functor
    (args->interf, args->frag);

  if(functor && args->picard_order != 0) {
    res = RES_BAD_ARG;
    log_err(dev,
      "%s: Impossible to process external fluxes when Picard order is not "
      "equal to 1; Picard order is currently set to %lu.\n",
      func_name, (unsigned long)args->picard_order);
    return res;
  }

  return RES_OK;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
XD(handle_external_net_flux)
  (const struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct XD(handle_external_net_flux_args)* args,
   struct XD(temperature)* T)
{
  /* Sampling external sources */
  struct sdis_external_sources_sample sample = SDIS_EXTERNAL_SOURCES_SAMPLE_NULL;
  sdis_interface_sample_external_sources_T sample_sources = NULL;

  /* Ray tracing */
  struct hit_filter_data filter_data = HIT_FILTER_DATA_NULL;
  struct sXd(hit) hit = SXD_HIT_NULL;
  float ray_org[DIM] = {0};
  float ray_dir[DIM] = {0};
  float ray_range[2] = {0};

  /* External flux */
  double incident_direct_flux = 0; /* [W/m^2/sr] TODO ? */

  /* Miscellaneous */
  res_T res = RES_OK;
  ASSERT(scn && args && T);

  res = XD(check_handle_external_net_flux_args)(scn->dev, FUNC_NAME, args);
  if(res != RES_OK) goto error;

  /* Retrieve the functor to sample external sources */
  sample_sources = interface_side_get_external_sources_sampling_functor
    (args->interf, args->frag);

  /* No external sources <=> no external fluxes. Nothing to do */
  if(!sample_sources) goto exit;

  /* Sample an external sources */
  res = sample_sources(args->frag, rng, &sample, args->interf->data);
  if(res != RES_OK) {
    log_err(scn->dev,
      "%s: error when sampling external sources -- %s.\n",
      FUNC_NAME, res_to_cstr(res));
    goto error;
  }

  /* Check whether the sampled external source is occluded or not */
  filter_data.XD(hit) = *args->hit;
  fX_set_dX(ray_org, args->frag->P);
  fX_set_dX(ray_dir, sample.dir);
  ray_range[0] = 0;
  ray_range[1] = (float)sample.distance;
  SXD(scene_view_trace_ray
    (scn->sXd(view), ray_org, ray_dir, ray_range, &filter_data, &hit));

  /* If the source is not occluded, compute its direct contribution */
  if(!SXD_HIT_NONE(&hit)) {
    const double cos_theta = dX(dot)(args->frag->Ng, sample.dir);
    incident_direct_flux = fabs(cos_theta) * sample.radiance / sample.pdf;
  }

  /* TODO handle diffuse contribution */
  /* TODO update the weight */

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
