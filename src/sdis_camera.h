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

#ifndef SDIS_CAMERA_H
#define SDIS_CAMERA_H

#include <rsys/double3.h>
#include <rsys/ref_count.h>

struct sdis_device;

struct sdis_camera {
  /* Orthogonal camera frame */
  double axis_x[3];
  double axis_y[3];
  double axis_z[3];

  double position[3];
  double fov_x; /* Field of view in radians */
  double rcp_proj_ratio; /* height / width */

  ref_T ref;
  struct sdis_device* dev;
};

static FINLINE void
camera_ray
  (const struct sdis_camera* cam,
   const double sample[2], /* In [0, 1[ */
   double org[3],
   double dir[3])
{
  double x[3], y[3], len;
  (void)len; /* Avoid warning in debug */
  ASSERT(cam && sample && org && dir);
  ASSERT(sample[0] >= 0.0 || sample[0] < 1.0);
  ASSERT(sample[1] >= 0.0 || sample[1] < 1.0);

  d3_muld(x, cam->axis_x, sample[0]*2.0 - 1.0);
  d3_muld(y, cam->axis_y, sample[1]*2.0 - 1.0);
  d3_add(dir, d3_add(dir, x, y), cam->axis_z);
  len = d3_normalize(dir, dir);
  ASSERT(len >= 1.e-6);
  d3_set(org, cam->position);
}

#endif /* SDIS_CAMERA_H */

