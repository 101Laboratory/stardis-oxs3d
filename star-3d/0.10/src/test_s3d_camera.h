/* Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_S3D_CAMERA_H
#define TEST_S3D_CAMERA_H

#include <rsys/float3.h>

struct camera {
  float pos[3];
  float x[3], y[3], z[3]; /* Basis */
};

static INLINE void
camera_init
  (struct camera* cam,
   const float pos[3],
   const float tgt[3],
   const float up[3],
   const float fov_x,
   const float proj_ratio)
{
  float f = 0.f;
  ASSERT(cam);

  f3_set(cam->pos, pos);
  f = f3_normalize(cam->z, f3_sub(cam->z, tgt, pos)); CHK(f != 0);
  f = f3_normalize(cam->x, f3_cross(cam->x, cam->z, up)); CHK(f != 0);
  f = f3_normalize(cam->y, f3_cross(cam->y, cam->z, cam->x)); CHK(f != 0);
  f3_divf(cam->z, cam->z, (float)tan(fov_x*0.5f));
  f3_divf(cam->y, cam->y, proj_ratio);
}

static INLINE void
camera_ray
  (const struct camera* cam,
   const float pixel[2],
   float org[3],
   float dir[3])
{
  float x[3], y[3], f;
  ASSERT(cam && pixel && org && dir);

  f3_mulf(x, cam->x, pixel[0]*2.f - 1.f);
  f3_mulf(y, cam->y, pixel[1]*2.f - 1.f);
  f3_add(dir, f3_add(dir, x, y), cam->z);
  f = f3_normalize(dir, dir); CHK(f != 0);
  f3_set(org, cam->pos);
}

#endif /* TEST_S3D_CAMERA_H */

