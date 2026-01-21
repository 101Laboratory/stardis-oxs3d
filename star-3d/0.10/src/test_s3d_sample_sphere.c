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
  struct s3d_attrib attr0;
  struct s3d_attrib attr1;
  struct s3d_primitive prim0;
  struct s3d_primitive prim1;
  struct s3d_device* dev;
  struct s3d_shape* sphere0;
  struct s3d_shape* sphere1;
  struct s3d_scene* scn;
  struct s3d_scene_view* view;
  unsigned sphere0_id;
  unsigned sphere1_id;
  float center[3];
  float st0[2];
  float st1[2];
  int N = 10000;
  int i;
  float sum;
  float E, V, SE;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);

  CHK(s3d_shape_create_sphere(dev, &sphere0) == RES_OK);
  CHK(s3d_shape_create_sphere(dev, &sphere1) == RES_OK);
  CHK(s3d_shape_get_id(sphere0, &sphere0_id) == RES_OK);
  CHK(s3d_shape_get_id(sphere1, &sphere1_id) == RES_OK);

  CHK(s3d_sphere_setup(sphere0, f3(center,-1.5, 0, 0), 2) == RES_OK);
  CHK(s3d_sphere_setup(sphere1, f3(center, 1.5, 0, 0), 2) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere1) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &view) == RES_OK);
  CHK(s3d_scene_view_sample(view, 0, 0, 0, &prim0, st0) == RES_OK);
  CHK(prim0.prim_id == 0);
  CHK(prim0.geom_id == sphere0_id || prim0.geom_id == sphere1_id);
  CHK(prim0.inst_id == S3D_INVALID_ID);

  CHK(s3d_scene_view_sample(view, 0, 0, 0, &prim1, st1) == RES_OK);
  CHK(S3D_PRIMITIVE_EQ(&prim0, &prim1));
  CHK(f2_eq(st0, st1));

  CHK(s3d_primitive_get_attrib(&prim0, S3D_ATTRIB_0, st0, &attr0) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim0, S3D_ATTRIB_1, st0, &attr0) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim0, S3D_ATTRIB_2, st0, &attr0) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim0, S3D_ATTRIB_3, st0, &attr0) == RES_BAD_ARG);
  CHK(s3d_primitive_get_attrib(&prim0, S3D_POSITION, st0, &attr0) == RES_OK);
  CHK(s3d_primitive_get_attrib
    (&prim0, S3D_GEOMETRY_NORMAL, st0, &attr1) == RES_OK);

  if(prim0.geom_id == sphere0_id) {
    f3_sub(attr0.value, attr0.value, f3(center,-1.5, 0, 0));
  } else {
    f3_sub(attr0.value, attr0.value, f3(center, 1.5, 0, 0));
  }
  f3_mulf(attr1.value, attr1.value, 2.f);
  CHK(f3_eq_eps(attr0.value, attr1.value, 1.e-3f));

  /* Check that 50 percents of samples lie onto "sphere0" */
  sum = 0;
  FOR_EACH(i, 0, N) {
    const float u = rand_canonic();
    const float v = rand_canonic();
    const float w = rand_canonic();

    CHK(s3d_scene_view_sample(view, u, v, w, &prim0, st0) == RES_OK);
    if(prim0.geom_id == sphere0_id) {
      sum += 1;
    }

    CHK(s3d_primitive_get_attrib(&prim0, S3D_POSITION, st0, &attr0) == RES_OK);
    CHK(s3d_primitive_get_attrib
      (&prim0, S3D_GEOMETRY_NORMAL, st0, &attr1) == RES_OK);

    if(prim0.geom_id == sphere0_id) {
      f3_sub(attr0.value, attr0.value, f3(center,-1.5, 0, 0));
    } else {
      f3_sub(attr0.value, attr0.value, f3(center, 1.5, 0, 0));
    }
    f3_mulf(attr1.value, attr1.value, 2.f);
    CHK(f3_eq_eps(attr0.value, attr1.value, 1.e-3f));
  }
  E = sum / (float)N;
  V = sum / (float)N - E*E;
  SE = (float)sqrt(V/(float)N);
  CHK(eq_epsf(E, 0.5, 2*SE));

  CHK(s3d_device_ref_put(dev) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_shape_ref_put(sphere0) == RES_OK);
  CHK(s3d_shape_ref_put(sphere1) == RES_OK);
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

