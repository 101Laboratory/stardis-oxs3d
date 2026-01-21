/* Copyright (C) 2019, 2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#include "sg3d.h"
#include "test_sg3d_utils.h"

#include <rsys/double3.h>

#include <stdio.h>

/*
            cube_2                             cube_3

                                      +-------------------------+
                                      |                         B
        +-------------+               |     +-------------+     |
     m1 |     m0      M            m1 |  m1 |      m0     M     |
        | +------+    |               | m0  | +------+    |     |
        | |  m1  S    |               |     | |  m1  S    |     |
        | | N <--|    |               |     | | N <--|    |     |
        | +------+    |               |     | +------+    |     |
        |        N <--|               |     |        N <--|     |
        +-------------+               |     +-------------+     |
                                      |                    N <--|
                                      +-------------------------+
 */

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sg3d_device* dev;
  struct sg3d_geometry* geom;
  struct context ctx = CONTEXT_NULL__;
  struct sg3d_geometry_add_callbacks callbacks = SG3D_ADD_CALLBACKS_NULL__;
  unsigned count;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sg3d_device_create(NULL, &allocator, 1, &dev));
  OK(sg3d_geometry_create(dev, &geom));
  SG3D(device_ref_put(dev));

  callbacks.get_indices = get_indices;
  callbacks.get_properties = get_properties;
  callbacks.get_position = get_position;

  ctx.positions = box_vertices;
  ctx.indices = cube_indices;
  ctx.front_media = medium0;
  ctx.back_media = medium1;
  ctx.intface = intface0;

  /* First cube (front: 0, back: 1), right-handed normal outside */
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));

  d3(ctx.offset, -1, -1, -1);
  d3_splat(ctx.scale, 3);
  ctx.reverse_vrtx = 1;

  /* Second cube (front: 0, back: 1), right-handed normal inside */
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));

  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_dump_as_c_code(geom, stdout, "cube_in_cube_2",
    SG3D_C_DUMP_CONST | SG3D_C_DUMP_STATIC));

  d3(ctx.offset, -4, -4, -4);
  d3_splat(ctx.scale, 10);
  ctx.reverse_vrtx = 1;
  ctx.reverse_med = 1;
  ctx.front_media = medium1;
  ctx.back_media = medium0;

  /* Third cube (front: 0, back: 1), right-handed normal inside */
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));

  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_dump_as_c_code(geom, stdout, "cube_in_cube_3",
    SG3D_C_DUMP_CONST | SG3D_C_DUMP_STATIC));

  SG3D(geometry_ref_put(geom));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
