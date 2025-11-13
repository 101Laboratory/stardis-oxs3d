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
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_green.h"

#include <star/ssp.h>

res_T
time_rewind
  (struct sdis_scene* scn,
   const double mu,
   const double t0,
   struct ssp_rng* rng,
   struct rwalk* rwalk,
   const struct rwalk_context* ctx,
   struct temperature* T)
{
  const struct enclosure* enc = NULL;
  struct sdis_medium* mdm = NULL;
  double temperature;
  double tau;
  res_T res = RES_OK;
  ASSERT(scn && rwalk && ctx && rng && T);

  /* Get the current medium */
  enc = scene_get_enclosure(scn, rwalk->enc_id);
  res = scene_get_enclosure_medium(scn, enc, &mdm);
  if(res != RES_OK) goto error;

  /* Sample the time using the upper bound. */
  tau = ssp_ran_exp(rng, mu);

  /* Increment the elapsed time */
  ASSERT(rwalk->vtx.time >= t0);
  rwalk->elapsed_time += MMIN(tau, rwalk->vtx.time - t0);

  if(IS_INF(rwalk->vtx.time)) goto exit; /* Steady computation */

  /* Time rewind */
  rwalk->vtx.time = MMAX(rwalk->vtx.time - tau, t0); /* Time rewind */

  /* The path does not reach the limit condition */
  if(rwalk->vtx.time > t0) goto exit;

  /* Fetch the initial temperature */
  temperature = medium_get_temperature(mdm, &rwalk->vtx);
  if(SDIS_TEMPERATURE_IS_UNKNOWN(temperature)) {
    log_err(mdm->dev, "the path reaches the limit condition but the "
      "%s temperature remains unknown -- position=%g, %g, %g\n",
      medium_type_to_string(sdis_medium_get_type(mdm)),
      SPLIT3(rwalk->vtx.P));
    res = RES_BAD_ARG;
    goto error;
  }

  /* Update temperature */
  T->value += temperature;
  T->done = 1;

  if(ctx->heat_path) {
    /* Update the registered vertex data */
    struct sdis_heat_vertex* vtx;
    vtx = heat_path_get_last_vertex(ctx->heat_path);
    vtx->time = rwalk->vtx.time;
    vtx->weight = T->value;
  }

  if(ctx->green_path) {
    res = green_path_set_limit_vertex
      (ctx->green_path, mdm, &rwalk->vtx, rwalk->elapsed_time);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
check_primitive_uv_2d(struct sdis_device* dev, const double param_coord[])
{
  double u;
  res_T res = RES_OK;
  ASSERT(dev && param_coord);

  u = param_coord[0];

  if(u < 0 || 1 < u) {
    log_err(dev,
      "%s: invalid parametric coordinates u=%g; it must be in [0, 1].\n",
      FUNC_NAME, u);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
check_primitive_uv_3d(struct sdis_device* dev, const double param_coords[])
{
  double u, v, w;
  res_T res = RES_OK;
  ASSERT(dev && param_coords);

  u = param_coords[0];
  v = param_coords[1];
  w = CLAMP(1 - u - v, 0, 1);

  if(u < 0 || 1 < u || v < 0 || 1 < v || !eq_eps(u + v + w, 1, 1.e-6)) {
    log_err(dev,
      "%s: invalid parametric coordinates u=%g; v=%g. "
      "u + v + (1-u-v) must be equal to 1 with u and v in [0, 1].\n",
      FUNC_NAME, u, v);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}
