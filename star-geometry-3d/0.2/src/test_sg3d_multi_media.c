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

static const unsigned multi_media[12] = { 4, 4, 3, 3, 3, 3, 2, 2, 2, 1, 1, 1 };
static const unsigned multi_intface[12] = { 4, 4, 3, 3, 3, 3, 2, 2, 2, 1, 1, 1 };

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

  ctx.positions = cube_vertices;
  ctx.indices = cube_indices;
  ctx.front_media = medium0;
  ctx.back_media = multi_media;
  ctx.intface = multi_intface;

  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));

  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_dump_as_c_code(geom, stdout, "multi_media",
    SG3D_C_DUMP_CONST | SG3D_C_DUMP_STATIC));

  OK(sg3d_geometry_dump_as_vtk(geom, stdout));

  SG3D(geometry_ref_put(geom));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
