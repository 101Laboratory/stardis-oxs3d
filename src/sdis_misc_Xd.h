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

#include "sdis_heat_path.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_green.h"

#include <star/ssp.h>

#include "sdis_Xd_begin.h"

res_T
XD(time_rewind)
  (struct sdis_medium* mdm,
   struct ssp_rng* rng,
   const double dist_in_meter,
   const struct rwalk_context* ctx,
   struct XD(rwalk)* rwalk,
   struct XD(temperature)* T)
{
  double temperature;
  double lambda, rho, cp;
  double tau, mu, t0;
  res_T res = RES_OK;
  ASSERT(mdm && rng && ctx && rwalk && dist_in_meter > 0);
  ASSERT(sdis_medium_get_type(mdm) == SDIS_SOLID);
  ASSERT(T->done == 0);

  /* Fetch physical properties */
  lambda = solid_get_thermal_conductivity(mdm, &rwalk->vtx);
  rho = solid_get_volumic_mass(mdm, &rwalk->vtx);
  cp = solid_get_calorific_capacity(mdm, &rwalk->vtx);
  t0 = solid_get_t0(mdm); /* Limit time */

  /* Sample the time to reroll */
  mu = (2*DIM*lambda)/(rho*cp*dist_in_meter*dist_in_meter);
  tau = ssp_ran_exp(rng, mu);

  /* Increment the elapsed time */
  ASSERT(rwalk->vtx.time >= t0);
  rwalk->elapsed_time += MMIN(tau, rwalk->vtx.time - t0);

  if(IS_INF(rwalk->vtx.time)) goto exit; /* Steady computation */

  /* Time rewind */
  rwalk->vtx.time = MMAX(rwalk->vtx.time - tau, t0);

  /* The path does not reach the limit condition */
  if(rwalk->vtx.time > t0) goto exit;

  /* Fetch initial temperature */
  temperature = solid_get_temperature(mdm, &rwalk->vtx);
  if(temperature < 0) {
    log_err(mdm->dev, "%s: the path reaches the limit condition but the "
      "temperature remains unknown.\n", FUNC_NAME);
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
    res = green_path_set_limit_vertex(ctx->green_path, mdm, &rwalk->vtx,
      rwalk->elapsed_time);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#include "sdis_Xd_end.h"
