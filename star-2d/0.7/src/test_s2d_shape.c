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

static int
filter_none
  (const struct s2d_hit* hit,
   const float org[2],
   const float dir[],
   const float range[2],
   void* ray_data,
   void* filter_data)
{
  (void)hit, (void)org, (void)dir, (void)range, (void)ray_data, (void)filter_data;
  return 0;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s2d_device* dev;
  struct s2d_shape* shape;
  struct s2d_shape* shape_copy;
  struct s2d_vertex_data vdata[4];
  struct s2d_attrib attr;
  const unsigned nsegs = square_nsegs;
  const unsigned nverts = square_nverts;
  unsigned n;
  unsigned ids[2];
  unsigned id;
  void* data = (void*)&square_desc;
  char c;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s2d_device_create(NULL, &allocator, 0, &dev) == RES_OK);

  CHK(s2d_shape_create_line_segments(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_shape_create_line_segments(dev, NULL) == RES_BAD_ARG);
  CHK(s2d_shape_create_line_segments(NULL, &shape) == RES_BAD_ARG);
  CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);

  CHK(s2d_shape_ref_get(NULL) == RES_BAD_ARG);
  CHK(s2d_shape_ref_get(shape) == RES_OK);
  CHK(s2d_shape_ref_put(NULL) == RES_BAD_ARG);
  CHK(s2d_shape_ref_put(shape) == RES_OK);
  CHK(s2d_shape_ref_put(shape) == RES_OK);

  CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);

  CHK(s2d_shape_get_id(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_shape_get_id(shape, NULL) == RES_BAD_ARG);
  CHK(s2d_shape_get_id(NULL, &id) == RES_BAD_ARG);
  CHK(s2d_shape_get_id(shape, &id) == RES_OK);
  CHK(id != S2D_INVALID_ID);

  CHK(s2d_shape_is_attached(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_shape_is_attached(shape, NULL) == RES_BAD_ARG);
  CHK(s2d_shape_is_attached(NULL, &c) == RES_BAD_ARG);
  CHK(s2d_shape_is_attached(shape, &c) == RES_OK);
  CHK(c == 0);

  vdata[0].type = S2D_FLOAT2;
  vdata[0].usage = S2D_POSITION;
  vdata[0].get = line_segments_get_position;

  #define SETUP s2d_line_segments_setup_indexed_vertices
  #define square_get_ids line_segments_get_ids
  CHK(SETUP(NULL, 0, NULL, 0, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, NULL, 0, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, NULL, 0, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, NULL, 0, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, square_get_ids, 0, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, square_get_ids, 0, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, square_get_ids, 0, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, square_get_ids, 0, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, NULL, nverts, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, NULL, nverts, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, NULL, nverts, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, NULL, nverts, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, square_get_ids, nverts, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, square_get_ids, nverts, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, square_get_ids, nverts, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, square_get_ids, nverts, NULL, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, NULL, 0, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, NULL, 0, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, NULL, 0, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, NULL, 0, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, square_get_ids, 0, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, square_get_ids, 0, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, square_get_ids, 0, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, square_get_ids, 0, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, NULL, nverts, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, NULL, nverts, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, NULL, nverts, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, NULL, nverts, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, square_get_ids, nverts, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, square_get_ids, nverts, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, square_get_ids, nverts, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, square_get_ids, nverts, vdata, 0, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, NULL, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, NULL, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, NULL, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, NULL, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, square_get_ids, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, square_get_ids, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, square_get_ids, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, square_get_ids, 0, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, NULL, nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, NULL, nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, NULL, nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, NULL, nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, square_get_ids, nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, square_get_ids, nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, square_get_ids, nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, square_get_ids, nverts, NULL, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, NULL, 0, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, NULL, 0, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, NULL, 0, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, NULL, 0, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, square_get_ids, 0, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, square_get_ids, 0, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, square_get_ids, 0, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, square_get_ids, 0, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, NULL, nverts, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, NULL, nverts, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, NULL, nverts, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, NULL, nverts, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, square_get_ids, nverts, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, 0, square_get_ids, nverts, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(NULL, nsegs, square_get_ids, nverts, vdata, 1, data) == RES_BAD_ARG);
  CHK(SETUP(shape, nsegs, square_get_ids, nverts, vdata, 1, data) == RES_OK);

  vdata[0] = S2D_VERTEX_DATA_NULL;
  CHK(SETUP(shape, nsegs, square_get_ids, nverts, vdata, 1, data) == RES_BAD_ARG);

  vdata[0].type = S2D_FLOAT2;
  vdata[0].usage = S2D_POSITION;
  vdata[0].get = S2D_KEEP;
  CHK(SETUP(shape, nsegs, square_get_ids, nverts, vdata, 1, data) == RES_OK);

  vdata[0].get = line_segments_get_position;
  CHK(SETUP(shape, nsegs, square_get_ids, nverts, vdata, 1, data) == RES_OK);
  #undef square_get_ids
  #undef SETUP

  CHK(s2d_line_segments_get_vertices_count(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_vertices_count(shape, NULL) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_vertices_count(NULL, &n) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_vertices_count(shape, &n) == RES_OK);
  CHK(n == nverts);

  CHK(s2d_line_segments_get_segments_count(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_segments_count(shape, NULL) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_segments_count(NULL, &n) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_segments_count(shape, &n) == RES_OK);
  CHK(n == nsegs);

  #define GET_ATTR s2d_line_segments_get_vertex_attrib
  CHK(GET_ATTR(NULL, nverts, S2D_ATTRIBS_COUNT__, NULL) == RES_BAD_ARG);
  CHK(GET_ATTR(shape, nverts, S2D_ATTRIBS_COUNT__, NULL) == RES_BAD_ARG);
  CHK(GET_ATTR(NULL, 0, S2D_ATTRIBS_COUNT__, NULL) == RES_BAD_ARG);
  CHK(GET_ATTR(shape, 0, S2D_ATTRIBS_COUNT__, NULL) == RES_BAD_ARG);
  CHK(GET_ATTR(NULL, nverts, S2D_POSITION, NULL) == RES_BAD_ARG);
  CHK(GET_ATTR(shape, nverts, S2D_POSITION, NULL) == RES_BAD_ARG);
  CHK(GET_ATTR(NULL, 0, S2D_POSITION, NULL) == RES_BAD_ARG);
  CHK(GET_ATTR(shape, 0, S2D_POSITION, NULL) == RES_BAD_ARG);
  CHK(GET_ATTR(NULL, nverts, S2D_ATTRIBS_COUNT__, &attr) == RES_BAD_ARG);
  CHK(GET_ATTR(shape, nverts, S2D_ATTRIBS_COUNT__, &attr) == RES_BAD_ARG);
  CHK(GET_ATTR(NULL, 0, S2D_ATTRIBS_COUNT__, &attr) == RES_BAD_ARG);
  CHK(GET_ATTR(shape, 0, S2D_ATTRIBS_COUNT__, &attr) == RES_BAD_ARG);
  CHK(GET_ATTR(NULL, nverts, S2D_POSITION, &attr) == RES_BAD_ARG);
  CHK(GET_ATTR(shape, nverts, S2D_POSITION, &attr) == RES_BAD_ARG);
  CHK(GET_ATTR(NULL, 0, S2D_POSITION, &attr) == RES_BAD_ARG);

  FOR_EACH(id, 0, nverts) {
    float pos[2];
    line_segments_get_position(id, pos, data);
    CHK(GET_ATTR(shape, id, S2D_POSITION, &attr) == RES_OK);
    CHK(attr.type == S2D_FLOAT2);
    CHK(attr.usage == S2D_POSITION);
    CHK(f2_eq_eps(attr.value, pos, 1.e-6f) == 1);
  }
  #undef GET_ATTR

  CHK(s2d_line_segments_get_segment_indices(NULL, nsegs, NULL) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_segment_indices(shape, nsegs, NULL) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_segment_indices(NULL, nsegs, ids) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_segment_indices(shape, nsegs, ids) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_segment_indices(NULL, 0, NULL) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_segment_indices(shape, 0, NULL) == RES_BAD_ARG);
  CHK(s2d_line_segments_get_segment_indices(NULL, 0, ids) == RES_BAD_ARG);

  FOR_EACH(id, 0, nsegs) {
    unsigned indices[2];
    line_segments_get_ids(id, indices, data);
    CHK(s2d_line_segments_get_segment_indices(shape, id, ids) == RES_OK);
    CHK(ids[0] == indices[0]);
    CHK(ids[1] == indices[1]);
  }

  CHK(s2d_shape_is_enabled(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_shape_is_enabled(shape, NULL) == RES_BAD_ARG);
  CHK(s2d_shape_is_enabled(NULL, &c) == RES_BAD_ARG);
  CHK(s2d_shape_is_enabled(shape, &c) == RES_OK);
  CHK(c != 0);

  CHK(s2d_shape_enable(NULL, 0) == RES_BAD_ARG);
  CHK(s2d_shape_enable(shape, 0) == RES_OK);
  CHK(s2d_shape_is_enabled(shape, &c) == RES_OK);
  CHK(c == 0);

  CHK(s2d_shape_flip_contour(NULL) == RES_BAD_ARG);
  CHK(s2d_shape_flip_contour(shape) == RES_OK);
  CHK(s2d_shape_flip_contour(shape) == RES_OK);

  #define SET_FILTER_FUNC s2d_line_segments_set_hit_filter_function
  #define GET_FILTER_DATA s2d_line_segments_get_hit_filter_data
  CHK(SET_FILTER_FUNC(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(SET_FILTER_FUNC(shape, NULL, NULL) == RES_OK);
  CHK(SET_FILTER_FUNC(NULL, filter_none, NULL) == RES_BAD_ARG);
  CHK(SET_FILTER_FUNC(shape, filter_none, NULL) == RES_OK);

  CHK(GET_FILTER_DATA(NULL, NULL) == RES_BAD_ARG);
  CHK(GET_FILTER_DATA(shape, NULL) == RES_BAD_ARG);
  CHK(GET_FILTER_DATA(NULL, &data) == RES_BAD_ARG);
  CHK(GET_FILTER_DATA(shape, &data) == RES_OK);
  CHK(data == NULL);

  CHK(SET_FILTER_FUNC(shape, NULL, NULL) == RES_OK);
  CHK(GET_FILTER_DATA(shape, &data) == RES_OK);
  CHK(data == NULL);
  CHK(SET_FILTER_FUNC(shape, filter_none, (void*)(uintptr_t)0xDEADBEEF) == RES_OK);
  CHK(GET_FILTER_DATA(shape, &data) == RES_OK);
  CHK((uintptr_t)data == 0xDEADBEEF);
  #undef SET_FILTER_FUNC
  #undef GET_FILTER_DATA

  CHK(s2d_shape_create_line_segments(dev, &shape_copy) == RES_OK);
  CHK(s2d_line_segments_copy(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_line_segments_copy(shape, NULL) == RES_BAD_ARG);
  CHK(s2d_line_segments_copy(NULL, shape_copy) == RES_BAD_ARG);
  CHK(s2d_line_segments_copy(shape, shape_copy) == RES_OK);

  CHK(s2d_shape_ref_put(shape) == RES_OK);
  CHK(s2d_shape_ref_put(shape_copy) == RES_OK);
  CHK(s2d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

