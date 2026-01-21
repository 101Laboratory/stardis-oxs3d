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

#define NSAMPS 4096

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s2d_vertex_data vdata;
  struct s2d_device* dev;
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  struct s2d_shape* shape;
  struct s2d_primitive prim;
  float s;
  unsigned square_id;
  int i;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s2d_device_create(NULL, &allocator, 1, &dev) == RES_OK);
  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);

  CHK(s2d_shape_get_id(shape, &square_id) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_SAMPLE, &scnview) == RES_OK);

  CHK(s2d_scene_view_sample(NULL, 1, 1, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 1, 1, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 0, 1, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 0, 1, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 1, 0, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 1, 0, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 0, 0, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 0, 0, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 1, 1, &prim, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 1, 1, &prim, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 0, 1, &prim, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 0, 1, &prim, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 1, 0, &prim, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 1, 0, &prim, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 0, 0, &prim, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 0, 0, &prim, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 1, 1, NULL, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 1, 1, NULL, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 0, 1, NULL,&s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 0, 1, NULL, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 1, 0, NULL, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 1, 0, NULL, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 0, 0, NULL, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 0, 0, NULL, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 1, 1, &prim, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 1, 1, &prim, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 0, 1, &prim, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 0, 1, &prim, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 1, 0, &prim, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 1, 0, &prim, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(NULL, 0, 0, &prim, &s) == RES_BAD_ARG);
  CHK(s2d_scene_view_sample(scnview, 0, 0, &prim, &s) == RES_OK);
  CHK(S2D_PRIMITIVE_EQ(&prim, &S2D_PRIMITIVE_NULL) == 1);

  vdata.usage = S2D_POSITION;
  vdata.type = S2D_FLOAT2;
  vdata.get = line_segments_get_position;
  CHK(s2d_line_segments_setup_indexed_vertices
    (shape, square_nsegs, line_segments_get_ids, square_nverts, &vdata, 1,
     (void*)&square_desc) == RES_OK);

  CHK(s2d_scene_view_sample(scnview, 0, 0, &prim, &s) == RES_OK);
  CHK(S2D_PRIMITIVE_EQ(&prim, &S2D_PRIMITIVE_NULL) == 1);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_SAMPLE, &scnview) == RES_OK);
  CHK(s2d_scene_view_sample(scnview, 0, 0, &prim, &s) == RES_OK);
  CHK(S2D_PRIMITIVE_EQ(&prim, &S2D_PRIMITIVE_NULL) == 0);

  CHK(prim.prim_id < 4);
  CHK(prim.geom_id == square_id);
  CHK(s == 0);

  /* Should take effect int the next created scene view */
  CHK(s2d_shape_flip_contour(shape) == RES_OK);

  FOR_EACH(i, 0, NSAMPS) {
    struct s2d_attrib attr_position;
    struct s2d_attrib attr_normal;
    const float u = rand_canonic();
    const float v = rand_canonic();
    float N[2], P[2], tmp[2];

    CHK(s2d_scene_view_sample(scnview, u, v, &prim, &s) == RES_OK);
    CHK(S2D_PRIMITIVE_EQ(&prim, &S2D_PRIMITIVE_NULL) == 0);
    CHK(prim.prim_id < 4);
    CHK(prim.geom_id == square_id);

    CHK(s2d_primitive_get_attrib
      (&prim, S2D_POSITION, s, &attr_position) == RES_OK);
    CHK(s2d_primitive_get_attrib
      (&prim, S2D_GEOMETRY_NORMAL, s, &attr_normal) == RES_OK);

    f2_normalize(attr_normal.value, attr_normal.value);

    switch(prim.prim_id) {
      case 0: f2(P, -1.f*s + (1-s)*1.f, -1.f); f2(N, 0.f, 1.f); break;
      case 1: f2(P, -1.f, 1.f*s + (1-s)*-1.f); f2(N, 1.f, 0.f); break;
      case 2: f2(P, 1.f*s + (1-s)* -1.f, 1.f); f2(N, 0.f,-1.f); break;
      case 3: f2(P, 1.f, -1.f*s + (1-s)* 1.f); f2(N,-1.f, 0.f); break;
      default: CHK(0 == 1); break; /* Invalid primitive id */
    }
    f2_add(P, P, f2_splat(tmp, 10));
    CHK(f2_eq_eps(P, attr_position.value, 1.e-6f));
    CHK(f2_eq_eps(N, attr_normal.value, 1.e-6f));
  }

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_SAMPLE, &scnview) == RES_OK);
  FOR_EACH(i, 0, NSAMPS) {
    struct s2d_attrib attr;
    const float u = rand_canonic();
    const float v = rand_canonic();
    float N[2];

    CHK(s2d_scene_view_sample(scnview, u, v, &prim, &s) == RES_OK);
    CHK(S2D_PRIMITIVE_EQ(&prim, &S2D_PRIMITIVE_NULL) == 0);
    CHK(prim.prim_id < 4);
    CHK(prim.geom_id == square_id);

    CHK(s2d_primitive_get_attrib(&prim, S2D_GEOMETRY_NORMAL, s, &attr) == RES_OK);
    f2_normalize(attr.value, attr.value);

    switch(prim.prim_id) {
      case 0: f2(N, 0.f,-1.f); break;
      case 1: f2(N,-1.f, 0.f); break;
      case 2: f2(N, 0.f, 1.f); break;
      case 3: f2(N, 1.f, 0.f); break;
      default: CHK(0 == 1); break; /* Invalid primitive id */
    }

    CHK(f2_eq_eps(N, attr.value, 1.e-6f) == 1);
  }

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_device_ref_put(dev) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);
  CHK(s2d_shape_ref_put(shape) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

