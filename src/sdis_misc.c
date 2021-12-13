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

#include "sdis.h"

/* Generate the generic functions */
#define SDIS_XD_DIMENSION 2
#include "sdis_misc_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_misc_Xd.h"

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
