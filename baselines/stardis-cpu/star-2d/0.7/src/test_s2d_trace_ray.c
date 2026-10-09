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

struct ray_data {
  struct s2d_primitive prim;
  float ray_org[2];
  float ray_dir[2];
  float ray_range[2];
};

static int
filter_hit
  (const struct s2d_hit* hit,
   const float org[2],
   const float dir[2],
   const float range[2],
   void* ray_data,
   void* filter_data)
{
  struct ray_data* data = ray_data;
  CHK(hit != NULL);
  CHK(org != NULL);
  CHK(dir != NULL);
  CHK(range != NULL);
  CHK((intptr_t)filter_data == 0xDEADBEEF);
  if(!ray_data) return 0;
  CHK(f2_eq(data->ray_org, org));
  CHK(f2_eq(data->ray_dir, dir));
  CHK(f2_eq(data->ray_range, range));
  return S2D_PRIMITIVE_EQ(&data->prim, &hit->prim);
}

int
main(int argc, char** argv)
{
  struct ray_data ray_data;
  struct s2d_device* dev;
  struct s2d_shape* shape;
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  struct s2d_vertex_data vdata;
  struct s2d_hit hit;
  struct s2d_primitive prim, prim2;
  struct mem_allocator allocator;
  float org[2], dir[3], range[2];
  float N[3] = {0.f, 0.f, 0.f};
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s2d_device_create(NULL, &allocator, 1, &dev) == RES_OK);
  CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);
  CHK(s2d_scene_create(dev, &scn) == RES_OK);

  vdata.type = S2D_FLOAT2;
  vdata.usage = S2D_POSITION;
  vdata.get = line_segments_get_position;
  CHK(s2d_line_segments_setup_indexed_vertices
    (shape, square_nsegs, line_segments_get_ids, square_nverts, &vdata, 1,
     (void*)&square_desc) == RES_OK);

  #define SET_FILTER_FUNC s2d_line_segments_set_hit_filter_function
  CHK(SET_FILTER_FUNC(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(SET_FILTER_FUNC(shape, NULL, NULL) == RES_OK);
  CHK(SET_FILTER_FUNC(NULL, filter_hit, NULL) == RES_BAD_ARG);
  CHK(SET_FILTER_FUNC(shape, filter_hit, NULL) == RES_OK);
  CHK(SET_FILTER_FUNC(shape, filter_hit, (void*)0xDEADBEEF) == RES_OK);
  #undef SET_FILTER_FUNC

  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);

  f2(org, 10.f, 10.f);
  f2(dir, 0.f, -1.f);
  f2(range, 0, FLT_MAX);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &scnview) == RES_OK);

  #define RT s2d_scene_view_trace_ray
  CHK(RT(NULL, NULL, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(scnview, NULL, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(NULL, org, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(scnview, org, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(NULL, NULL, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(scnview, NULL, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(NULL, org, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(scnview, org, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(NULL, NULL, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(scnview, NULL, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(NULL, org, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(scnview, org, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(NULL, NULL, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(scnview, NULL, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(NULL, org, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(scnview, org, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(RT(NULL, NULL, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(scnview, NULL, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(NULL, org, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(scnview, org, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(NULL, NULL, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(scnview, NULL, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(NULL, org, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(scnview, org, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(NULL, NULL, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(scnview, NULL, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(NULL, org, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(scnview, org, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(NULL, NULL, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(scnview, NULL, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(NULL, org, dir, range, NULL, &hit) == RES_BAD_ARG);

  f2(dir, 0.f, -1.f);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.prim_id == 0);
  f2_normalize(N, hit.normal);
  CHK(f2_eq_eps(N, f2(dir, 0.f, 1.f), 1.e-6f) == 1);
  CHK(eq_epsf(hit.u, 0.5f, 1.e-6f) == 1);
  CHK(eq_epsf(hit.distance, 1.0f, 1.e-6f) == 1);

  prim = hit.prim;

  f2(dir, 0.f, -1.f);
  range[1] = 0.5f;
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 1);
  range[1] = FLT_MAX;


  f2(dir, 0.f, -1.f);
  f2_set(ray_data.ray_org, org);
  f2_set(ray_data.ray_dir, dir);
  f2_set(ray_data.ray_range, range);
  ray_data.prim = prim;
  CHK(RT(scnview, org, dir, range, &ray_data, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 1);

  f2(dir, -1.f, 0.f);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.prim_id == 1);
  f2_normalize(N, hit.normal);
  CHK(f2_eq_eps(N, f2(dir, 1.f, 0.f), 1.e-6f) == 1);
  CHK(eq_epsf(hit.u, 0.5f, 1.e-6f) == 1);
  CHK(eq_epsf(hit.distance, 1.0f, 1.e-6f) == 1);

  f2(dir, 0.f, 1.f);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.prim_id == 2);
  f2_normalize(N, hit.normal);
  CHK(f2_eq_eps(N, f2(dir, 0.f, -1.f), 1.e-6f) == 1);
  CHK(eq_epsf(hit.u, 0.5f, 1.e-6f) == 1);
  CHK(eq_epsf(hit.distance, 1.0f, 1.e-6f) == 1);

  prim2 = hit.prim;

  f2(org, 10.f, 12.f);
  f2(dir, 0.f, -1.f);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(S2D_PRIMITIVE_EQ(&hit.prim, &prim2) == 1);

  f2_set(ray_data.ray_org, org);
  f2_set(ray_data.ray_dir, dir);
  f2_set(ray_data.ray_range, range);
  ray_data.prim = prim2;
  CHK(RT(scnview, org, dir, range, &ray_data, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(S2D_PRIMITIVE_EQ(&hit.prim, &prim) == 1);

  f2_splat(org, 10.f);
  f2(dir, 1.f, 0.f);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.prim_id == 3);
  f2_normalize(N, hit.normal);
  CHK(f2_eq_eps(N, f2(dir, -1.f, 0.f), 1.e-6f) == 1);
  CHK(eq_epsf(hit.u, 0.5f, 1.e-6f) == 1);
  CHK(eq_epsf(hit.distance, 1.0f, 1.e-6f) == 1);

  f2(range, 1.f, -1.f);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 1);

  f2(dir, 1.f, 1.f);
  f2(range, 0.f, FLT_MAX);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_BAD_ARG);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &scnview) == RES_OK);
  f2(dir, 0.75f, -1.f);
  f2_normalize(dir, dir);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(eq_epsf(hit.u, 0.125f, 1.e-6f) == 1);
  CHK(eq_epsf(hit.distance, 1.25, 1.E-6f) == 1);

  f2(dir, -1.f, 0.25f);
  f2_normalize(dir, dir);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(eq_epsf(hit.u, 0.625, 1.e-6f) == 1);
  CHK(eq_epsf(hit.distance, (float)sqrt(1.0625f), 1e-6f) == 1);

  f2(org, 8.75f, 10.f);
  f2(dir, -1, 0.f);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 1);

  f2(dir, 1, 0.f);
  CHK(RT(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.prim_id == 1);
  f2_normalize(N, hit.normal);
  CHK(f2_eq_eps(N, f2(dir, 1.f, 0.f), 1.e-6f) == 1);
  CHK(eq_epsf(hit.u, 0.5f, 1.e-6f) == 1);
  CHK(eq_epsf(hit.distance, 0.25f, 1.e-6f) == 1);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_device_ref_put(dev) == RES_OK);
  CHK(s2d_shape_ref_put(shape) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return 0;
}

