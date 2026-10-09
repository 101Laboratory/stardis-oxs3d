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

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s2d_vertex_data attrib;
  struct s2d_device* dev;
  struct s2d_device* dev2;
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  struct s2d_shape* shape;
  struct s2d_primitive prim;
  size_t nprims;
  size_t i;
  float lower[2], upper[2];
  float tmp[2];
  float length;
  float area;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s2d_device_create(NULL, &allocator, 1, &dev) == RES_OK);

  CHK(s2d_scene_create(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_create(dev, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_create(NULL, &scn) == RES_BAD_ARG);
  CHK(s2d_scene_create(dev, &scn) == RES_OK);

  CHK(s2d_scene_ref_get(NULL) == RES_BAD_ARG);
  CHK(s2d_scene_ref_get(scn) == RES_OK);
  CHK(s2d_scene_ref_put(NULL) == RES_BAD_ARG);
  CHK(s2d_scene_ref_put(scn) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);

  CHK(s2d_scene_attach_shape(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_attach_shape(scn, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_attach_shape(NULL, shape) == RES_BAD_ARG);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);

  CHK(s2d_scene_detach_shape(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_detach_shape(scn, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_detach_shape(NULL, shape) == RES_BAD_ARG);
  CHK(s2d_scene_detach_shape(scn, shape) == RES_OK);
  CHK(s2d_scene_detach_shape(scn, shape) == RES_BAD_ARG);

  CHK(s2d_scene_clear(NULL) == RES_BAD_ARG);
  CHK(s2d_scene_clear(scn) == RES_OK);
  CHK(s2d_scene_clear(scn) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s2d_scene_clear(scn) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &scnview) == RES_OK);
  CHK(s2d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 0);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s2d_scene_detach_shape(scn, shape) == RES_OK);

  attrib.type = S2D_FLOAT2;
  attrib.usage = S2D_POSITION;
  attrib.get = line_segments_get_position;
  CHK(s2d_line_segments_setup_indexed_vertices
    (shape, square_nsegs, line_segments_get_ids, square_nverts, &attrib, 1,
     (void*)&square_desc) == RES_OK);

  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 4);

  FOR_EACH(i, 0, nprims) {
    struct s2d_attrib attr;

    CHK(s2d_scene_view_get_primitive(scnview, (unsigned)i, &prim) == RES_OK);
    CHK(s2d_primitive_get_attrib(&prim, S2D_GEOMETRY_NORMAL, 0, &attr) == RES_OK);
    f2_normalize(attr.value, attr.value);
    switch(i) {
      case 0: f2_eq_eps(attr.value, f2(tmp, 0.f, 1.f), 1.e-6f); break;
      case 1: f2_eq_eps(attr.value, f2(tmp, 1.f, 0.f), 1.e-6f); break;
      case 2: f2_eq_eps(attr.value, f2(tmp, 0.f,-1.f), 1.e-6f); break;
      case 3: f2_eq_eps(attr.value, f2(tmp,-1.f, 0.f), 1.e-6f); break;
      default: FATAL("Unreachable code.\n");
    }
  }

  CHK(s2d_scene_view_compute_contour_length(scnview, &length) == RES_OK);
  CHK(eq_epsf(length, 8.f, 1.e-6f));

  CHK(s2d_scene_view_compute_area(scnview, &area) == RES_OK);
  CHK(eq_epsf(area, 4.f, 1.e-6f));

  CHK(s2d_scene_view_get_aabb(scnview, lower, upper) == RES_OK);
  CHK(f2_eq_eps(lower, f2(tmp, 9.f, 9.f), 1.e-6f));
  CHK(f2_eq_eps(upper, f2(tmp, 11.f, 11.f), 1.e-6f));

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_clear(scn) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_SAMPLE, &scnview) == RES_OK);
  CHK(s2d_scene_view_compute_contour_length(scnview, &length) == RES_OK);
  CHK(length == 0.f);
  CHK(s2d_scene_view_compute_area(scnview, &area) == RES_OK);
  CHK(area == 0.f);
  CHK(s2d_scene_view_get_aabb(scnview, lower, upper) == RES_OK);
  CHK(lower[0] > upper[0]);
  CHK(lower[1] > upper[1]);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_get_device(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_get_device(scn, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_get_device(NULL, &dev2) == RES_BAD_ARG);
  CHK(s2d_scene_get_device(scn, &dev2) == RES_OK);
  CHK(dev2 == dev);

  CHK(s2d_shape_ref_put(shape) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);
  CHK(s2d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

