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

#ifndef S3D_SPHERE_H
#define S3D_SPHERE_H

#include "s3d_c.h"

#include <rsys/ref_count.h>
#include <rsys/float3.h>

struct sphere {
  float pos[3];
  float radius;
  struct s3d_device* dev;
  struct hit_filter filter;
  ref_T ref;
};

extern LOCAL_SYM res_T
sphere_create
  (struct s3d_device* dev,
   struct sphere** sphere);

extern LOCAL_SYM void
sphere_ref_get
  (struct sphere* sphere);

extern LOCAL_SYM void
sphere_ref_put
  (struct sphere* sphere);

static INLINE int
sphere_is_degenerated(const struct sphere* sphere)
{
  ASSERT(sphere);
  return sphere->radius < 0;
}

static INLINE void
sphere_compute_aabb
  (const struct sphere* sphere,
   float lower[3],
   float upper[3])
{
  ASSERT(sphere && lower && upper);
  if(sphere_is_degenerated(sphere)) {
    f3_splat(lower, FLT_MAX);
    f3_splat(upper,-FLT_MAX);
  } else {
    f3_subf(lower, sphere->pos, sphere->radius);
    f3_addf(upper, sphere->pos, sphere->radius);
  }
}

static INLINE float
sphere_compute_area(const struct sphere* sphere)
{
  float r;
  ASSERT(sphere);
  r = sphere->radius;
  return (float)(4*PI*r*r);
}

static INLINE float
sphere_compute_volume(const struct sphere* sphere)
{
  float r;
  ASSERT(sphere);
  r = sphere->radius;
  return (float)(4*PI*r*r*r/3);
}

static FINLINE void
sphere_normal_to_uv(const float normal[3], float uv[2])
{
  float u, v, cos_theta;
  ASSERT(normal && uv && f3_is_normalized(normal));

  cos_theta = normal[2];

  v = (1.f - cos_theta) * 0.5f;
  if(absf(cos_theta) == 1) {
    u = 0;
  } else if(eq_epsf(normal[0], 0.f, 1.e-6f)) {
    u = normal[1] > 0 ? 0.25f : 0.75f;
  } else {
    double phi = atan2f(normal[1], normal[0]); /* phi in [-PI, PI] */
    if(phi < 0) phi = 2*PI + phi; /* phi in [0, 2PI] */
    u = (float)(phi / (2*PI));
  }
  uv[0] = u;
  uv[1] = v;
}

#endif /* S3D_SPHERE_H */
