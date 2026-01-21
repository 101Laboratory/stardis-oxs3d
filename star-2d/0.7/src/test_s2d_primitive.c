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
  struct s2d_attrib attr;
  struct s2d_primitive prim;
  struct s2d_vertex_data vdata;
  struct s2d_device* dev;
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  struct s2d_shape* shape;
  size_t nprims;
  unsigned square_id;
  float tmp[2];
  float length;
  float s;
  int i;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s2d_device_create(NULL, &allocator, 1, &dev) == RES_OK);
  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);
  CHK(s2d_shape_get_id(shape, &square_id) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);

  vdata.usage = S2D_POSITION;
  vdata.type = S2D_FLOAT2;
  vdata.get = line_segments_get_position;
  CHK(s2d_line_segments_setup_indexed_vertices
    (shape, square_nsegs, line_segments_get_ids, square_nverts, &vdata, 1,
     (void*)&square_desc) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_SAMPLE, &scnview) == RES_OK);
  CHK(s2d_scene_view_sample(scnview, 0, 0, &prim, &s) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  #define GET_ATTRIB s2d_primitive_get_attrib
  CHK(GET_ATTRIB(NULL, S2D_ATTRIBS_COUNT__, 2, NULL) == RES_BAD_ARG);
  CHK(GET_ATTRIB(&prim, S2D_ATTRIBS_COUNT__, 2, NULL) == RES_BAD_ARG);
  CHK(GET_ATTRIB(NULL, S2D_ATTRIBS_COUNT__, s, NULL) == RES_BAD_ARG);
  CHK(GET_ATTRIB(&prim, S2D_ATTRIBS_COUNT__, s, NULL) == RES_BAD_ARG);
  CHK(GET_ATTRIB(NULL, S2D_ATTRIBS_COUNT__, 2, &attr) == RES_BAD_ARG);
  CHK(GET_ATTRIB(&prim, S2D_ATTRIBS_COUNT__, 2, &attr) == RES_BAD_ARG);
  CHK(GET_ATTRIB(NULL, S2D_ATTRIBS_COUNT__, s, &attr) == RES_BAD_ARG);
  CHK(GET_ATTRIB(&prim, S2D_ATTRIBS_COUNT__, s, &attr) == RES_BAD_ARG);
  CHK(GET_ATTRIB(NULL, S2D_POSITION, 2, NULL) == RES_BAD_ARG);
  CHK(GET_ATTRIB(&prim, S2D_POSITION, 2, NULL) == RES_BAD_ARG);
  CHK(GET_ATTRIB(NULL, S2D_POSITION, s, NULL) == RES_BAD_ARG);
  CHK(GET_ATTRIB(&prim, S2D_POSITION, s, NULL) == RES_BAD_ARG);
  CHK(GET_ATTRIB(NULL, S2D_POSITION, 2, &attr) == RES_BAD_ARG);
  CHK(GET_ATTRIB(&prim, S2D_POSITION, 2, &attr) == RES_BAD_ARG);
  CHK(GET_ATTRIB(NULL, S2D_POSITION, s, &attr) == RES_BAD_ARG);
  CHK(GET_ATTRIB(&prim, S2D_POSITION, s, &attr) == RES_OK);
  CHK(attr.type == S2D_FLOAT2);
  CHK(attr.usage == S2D_POSITION);

  CHK(GET_ATTRIB(&prim, S2D_GEOMETRY_NORMAL, s, &attr) == RES_OK);
  f2_normalize(attr.value, attr.value);
  switch(prim.prim_id) {
    case 0: CHK(f2_eq_eps(attr.value, f2(tmp, 0.f, 1.f), 1.e-6f) == 1); break;
    case 1: CHK(f2_eq_eps(attr.value, f2(tmp, 1.f, 0.f), 1.e-6f) == 1); break;
    case 2: CHK(f2_eq_eps(attr.value, f2(tmp, 0.f,-1.f), 1.e-6f) == 1); break;
    case 3: CHK(f2_eq_eps(attr.value, f2(tmp,-1.f, 0.f), 1.e-6f) == 1); break;
    default: CHK(0 == 1); /* Invalid primitive id */
  }

  CHK(GET_ATTRIB(&S2D_PRIMITIVE_NULL, S2D_GEOMETRY_NORMAL, s, &attr) 
    == RES_BAD_ARG);
  #undef GET_ATTRIB

  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 4);

  CHK(s2d_primitive_compute_length(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_primitive_compute_length(&prim, NULL) == RES_BAD_ARG);
  CHK(s2d_primitive_compute_length(NULL, &length) == RES_BAD_ARG);
  CHK(s2d_primitive_compute_length(&prim, &length) == RES_OK);
  CHK(eq_epsf(length, 2, 1.e-6f) == 1);

  CHK(s2d_primitive_sample(NULL, 2.f, NULL) == RES_BAD_ARG);
  CHK(s2d_primitive_sample(&prim, 2.f, NULL) == RES_BAD_ARG);
  CHK(s2d_primitive_sample(NULL, 0.5f, NULL) == RES_BAD_ARG);
  CHK(s2d_primitive_sample(&prim, 0.5f, NULL) == RES_BAD_ARG);
  CHK(s2d_primitive_sample(NULL, 2.f, &s) == RES_BAD_ARG);
  CHK(s2d_primitive_sample(&prim, 2.f, &s) == RES_BAD_ARG);
  CHK(s2d_primitive_sample(NULL, 0.5f, &s) == RES_BAD_ARG);
  CHK(s2d_primitive_sample(&prim, 0.5f, &s) == RES_OK);
  CHK(eq_epsf(s, 0.5f, 1.e-6f));

  FOR_EACH(i, 0, NSAMPS) {
    CHK(s2d_primitive_sample(&prim, rand_canonic(), &s) == RES_OK);
    CHK(s >= 0.f);
    CHK(s <= 1.f);
  }

  CHK(s2d_scene_view_get_primitive(scnview, 0, &prim) == RES_OK);

  #define GET_VERTEX_ATTR s2d_segment_get_vertex_attrib
  CHK(GET_VERTEX_ATTR(NULL, 0, S2D_POSITION, &attr) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 2, S2D_POSITION, &attr) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 0, S2D_GEOMETRY_NORMAL, &attr) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 0, S2D_POSITION, NULL) == RES_BAD_ARG);
  CHK(GET_VERTEX_ATTR(&prim, 0, S2D_POSITION, &attr) == RES_OK);
  CHK(attr.type == S2D_FLOAT2);
  CHK(f2_eq_eps(attr.value, square_verts + square_ids[0]*2, 1.e-6f));
  CHK(GET_VERTEX_ATTR(&prim, 1, S2D_POSITION, &attr) == RES_OK);
  CHK(attr.type == S2D_FLOAT2);
  CHK(f2_eq_eps(attr.value, square_verts + square_ids[1]*2, 1.e-6f));

  CHK(s2d_device_ref_put(dev) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_shape_ref_put(shape) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

