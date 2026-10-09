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

#include "s3d.h"
#include "test_s3d_utils.h"

#include <rsys/float2.h>
#include <rsys/float3.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_primitive prim0;
  struct s3d_primitive prim1;
  struct s3d_hit hit;
  struct s3d_device* dev;
  struct s3d_scene* scn;
  struct s3d_shape* sphere0;
  struct s3d_shape* sphere1;
  struct s3d_scene_view* view;
  float center[3] = {0, 0, 0};
  float radius;
  float org[3];
  float dir[3];
  float range[2];
  float N[3], P[3], tmp[3];
  float lower[3];
  float upper[3];
  float area;
  float volume;
  size_t nprims;
  unsigned sphere0_id;
  unsigned sphere1_id;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);

  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);

  CHK(s3d_shape_create_sphere(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_shape_create_sphere(dev, NULL) == RES_BAD_ARG);
  CHK(s3d_shape_create_sphere(NULL, &sphere0) == RES_BAD_ARG);
  CHK(s3d_shape_create_sphere(dev, &sphere0) == RES_OK);

  CHK(s3d_sphere_setup(NULL, NULL, -1) == RES_BAD_ARG);
  CHK(s3d_sphere_setup(sphere0, NULL, -1) == RES_BAD_ARG);
  CHK(s3d_sphere_setup(NULL, center, -1) == RES_BAD_ARG);
  CHK(s3d_sphere_setup(sphere0, center, -1) == RES_BAD_ARG);
  CHK(s3d_sphere_setup(NULL, NULL, 1) == RES_BAD_ARG);
  CHK(s3d_sphere_setup(sphere0, NULL, 1) == RES_BAD_ARG);
  CHK(s3d_sphere_setup(NULL, center, 1) == RES_BAD_ARG);
  CHK(s3d_sphere_setup(sphere0, center, 1) == RES_OK);
  CHK(s3d_sphere_setup(sphere0, center, 0) == RES_BAD_ARG);
  CHK(s3d_shape_ref_put(sphere0) == RES_OK);

  CHK(s3d_shape_create_sphere(dev, &sphere0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere0) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);

  f3(org, 0, 0, 100);
  f3(dir, 0, 0, -1);
  f2(range, 0, FLT_MAX);

  CHK(s3d_scene_view_trace_ray(view, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit));

  radius = 2;
  CHK(s3d_sphere_setup(sphere0, center, 2) == RES_OK);
  CHK(s3d_scene_view_trace_ray(view, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit));
  CHK(s3d_scene_view_ref_put(view) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  CHK(s3d_scene_view_trace_ray(view, org, dir, range, NULL, &hit) == RES_OK);
  CHK(!S3D_HIT_NONE(&hit));
  CHK(eq_epsf(hit.distance, 100 - radius, 1.e-3f));
  f3_normalize(N, hit.normal);
  f3_add(P, org, f3_mulf(P, dir, hit.distance));
  CHK(f3_eq_eps(N, f3(tmp, 0, 0, 1), 1.e-3f));
  CHK(f3_eq_eps(P, f3(tmp, 0, 0, radius), 1.e-3f));

  CHK(s3d_scene_view_compute_area(view, &area) == RES_OK);
  CHK(eq_epsf(area, (float)(4*PI*radius*radius), 1.e-6f));
  CHK(s3d_scene_view_compute_volume(view, &volume) == RES_OK);
  CHK(eq_epsf(volume, (float)(4.0/3.0*PI*radius*radius*radius), 1.e-6f));

  CHK(s3d_shape_flip_surface(sphere0) == RES_OK);
  CHK(s3d_scene_view_compute_area(view, &area) == RES_OK);
  CHK(eq_epsf(area, (float)(4*PI*radius*radius), 1.e-6f));
  CHK(s3d_scene_view_compute_volume(view, &volume) == RES_OK);
  CHK(eq_epsf(volume, (float)(4.0/3.0*PI*radius*radius*radius), 1.e-6f));

  CHK(s3d_scene_view_ref_put(view) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);

  CHK(s3d_scene_view_compute_area(view, &area) == RES_OK);
  CHK(eq_epsf(area, (float)(4*PI*radius*radius), 1.e-6f));
  CHK(s3d_scene_view_compute_volume(view, &volume) == RES_OK);
  CHK(eq_epsf(volume, (float)(-4.0/3.0*PI*radius*radius*radius), 1.e-6f));

  CHK(s3d_shape_flip_surface(sphere0) == RES_OK);
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  center[0] = 4;
  CHK(s3d_shape_create_sphere(dev, &sphere1) == RES_OK);
  CHK(s3d_sphere_setup(sphere1, center, radius) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere1) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_GET_PRIMITIVE, &view) == RES_OK);

  CHK(s3d_scene_view_compute_area(view, &area) == RES_OK);
  CHK(eq_epsf(area, (float)(2*4*PI*radius*radius), 1.e-6f));
  CHK(s3d_scene_view_compute_volume(view, &volume) == RES_OK);
  CHK(eq_epsf(volume, (float)(2*4.0/3.0*PI*radius*radius*radius), 1.e-6f));

  CHK(s3d_shape_get_id(sphere0, &sphere0_id) == RES_OK);
  CHK(s3d_shape_get_id(sphere1, &sphere1_id) == RES_OK);
  CHK(sphere0_id != sphere1_id);

  CHK(s3d_scene_view_primitives_count(view, &nprims) == RES_OK);
  CHK(nprims == 2);

  CHK(s3d_scene_view_get_aabb(view, lower, upper) == RES_OK);
  CHK(f3_eq_eps(lower, f3_splat(tmp, -2), 1.e-6f));
  CHK(f3_eq_eps(upper, f3(tmp, 6, 2, 2), 1.e-6f));

  CHK(s3d_scene_view_get_primitive(view, 0, &prim0) == RES_OK);
  CHK(s3d_scene_view_get_primitive(view, 1, &prim1) == RES_OK);
  CHK(prim0.prim_id == 0);
  CHK(prim1.prim_id == 0);
  CHK(prim0.geom_id == sphere0_id || prim0.geom_id == sphere1_id);
  CHK(prim1.geom_id == sphere0_id || prim1.geom_id == sphere1_id);
  CHK(prim0.geom_id != prim1.geom_id);
  CHK(prim0.inst_id == S3D_INVALID_ID);
  CHK(prim1.inst_id == S3D_INVALID_ID);
  CHK(prim0.scene_prim_id == 0);
  CHK(prim1.scene_prim_id == 1);

  CHK(s3d_shape_ref_put(sphere0) == RES_OK);
  CHK(s3d_shape_ref_put(sphere1) == RES_OK);
  CHK(s3d_scene_view_ref_put(view) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
