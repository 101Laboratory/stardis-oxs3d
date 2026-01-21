/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#include "s2d.h"
#include "test_s2d_utils.h"

#include <rsys/float2.h>
#include <rsys/float3.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s2d_device* dev;
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  struct s2d_shape* shape;
  struct s2d_vertex_data vdata;
  struct s2d_hit hit;
  float org[3];
  float dir[3];
  float range[2];
  float tmp[2];
  float dot;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s2d_device_create(NULL, &allocator, 1, &dev) == RES_OK);
  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);

  vdata.get = line_segments_get_position;
  vdata.type = S2D_FLOAT2;
  vdata.usage = S2D_POSITION;
  CHK(s2d_line_segments_setup_indexed_vertices
    (shape, square_nsegs, line_segments_get_ids, square_nverts, &vdata, 1,
     (void*)&square_desc) == RES_OK);

  f3_splat(org, 10.f);
  f3(dir, 1, 0, 0);
  f2(range, 0, FLT_MAX);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &scnview) == RES_OK);

  #define RT_3D s2d_scene_view_trace_ray_3d
  CHK(RT_3D(NULL, NULL, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(scnview, NULL, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(NULL, org, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(scnview, org, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(NULL, NULL, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(scnview, NULL, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(NULL, org, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(scnview, org, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(NULL, NULL, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(scnview, NULL, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(NULL, org, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(scnview, org, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(NULL, NULL, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(scnview, NULL, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(NULL, org, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(scnview, org, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT_3D(NULL, NULL, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(scnview, NULL, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(NULL, org, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(scnview, org, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(NULL, NULL, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(scnview, NULL, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(NULL, org, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(scnview, org, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(NULL, NULL, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(scnview, NULL, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(NULL, org, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(scnview, org, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(NULL, NULL, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(scnview, NULL, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(NULL, org, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT_3D(scnview, org, dir, range, NULL, &hit) == RES_OK);

  CHK(S2D_HIT_NONE(&hit) == 0);
  f2_normalize(hit.normal, hit.normal);
  CHK(f2_eq_eps(hit.normal, f2(tmp, -1.f, 0.f), 1.e-6f) == 1);
  CHK(eq_epsf(hit.distance, 1.f, 1.e-6f) == 1);
  CHK(eq_epsf(hit.u, 0.5f, 1.e-6f) == 1);

  f3_normalize(dir, f3(dir, 1.f, 0.f, 1.f));
  CHK(RT_3D(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  f2_normalize(hit.normal, hit.normal);
  CHK(f2_eq_eps(hit.normal, f2(tmp, -1.f, 0.f), 1.e-6f) == 1);
  CHK(eq_epsf(hit.u, 0.5f, 1.e-6f) == 1);

  dot = dir[0];
  CHK(eq_epsf(hit.distance, 1.f / dot, 1.e-6f) == 1);

  range[1] = 1.2f;
  CHK(RT_3D(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 1);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(RT_3D(scnview, org, dir, range, NULL, &hit) == RES_BAD_OP);
  #undef RT_3D

  CHK(s2d_scene_ref_put(scn) == RES_OK);
  CHK(s2d_shape_ref_put(shape) == RES_OK);
  CHK(s2d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return 0;
}
