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

#include <stdio.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sg3d_device* dev;
  struct sg3d_geometry* geom;
  struct context ctx = CONTEXT_NULL__;
  struct sg3d_geometry_add_callbacks callbacks = SG3D_ADD_CALLBACKS_NULL__;
  unsigned property[12];
  unsigned i;
  const unsigned property_count = sizeof(property) / sizeof(*property);
  unsigned count;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sg3d_device_create(NULL, &allocator, 1, &dev));
  OK(sg3d_geometry_create(dev, &geom));

  FOR_EACH(i, 0, property_count) property[i] = SG3D_UNSPECIFIED_PROPERTY;

  callbacks.get_indices = get_indices;
  callbacks.get_position = get_position;

  /* A 3D cube.
   * 2 enclosures (inside, outside) sharing the same triangles,
   * but opposite sides */
  ctx.positions = box_vertices;
  ctx.indices = cube_indices;

  /* Add geometry with no properties */
  ctx.front_media = property;
  ctx.back_media = medium1;
  ctx.intface = property;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == ntriangles);

  /* Add same geometry with no properties on front/intface */
  callbacks.get_properties = get_properties;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 2 * ntriangles);

  OK(sg3d_geometry_dump_as_c_code(geom, stdout, "front_unspecified",
    SG3D_C_DUMP_STATIC | SG3D_C_DUMP_CONST));

  /* Add same geometry, front/intface properties are defined for odd triangles */
  FOR_EACH(i, 0, sizeof(property) / sizeof(*property))
    property[i] = (i % 2) ? 0 : SG3D_UNSPECIFIED_PROPERTY;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == ntriangles / 2);
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 3 * ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  FOR_EACH(i, 0, count) {
    unsigned prop[SG3D_PROP_TYPES_COUNT__];
    OK(sg3d_geometry_get_unique_triangle_properties(geom, i, prop));
    CHK(prop[SG3D_FRONT] == ((i % 2) ? 0 : SG3D_UNSPECIFIED_PROPERTY)
      && prop[SG3D_BACK] == 1
      && prop[SG3D_INTFACE] == ((i % 2) ? 0 : SG3D_UNSPECIFIED_PROPERTY));
  }

  /* Same information again, using a reversed box */
  ctx.reverse_vrtx = 1;
  SWAP(const unsigned*, ctx.front_media, ctx.back_media);
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == ntriangles / 2);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 4 * ntriangles);

  OK(sg3d_geometry_dump_as_c_code(geom, stdout, "front_half_unspecified",
    SG3D_C_DUMP_STATIC | SG3D_C_DUMP_CONST));

  /* Define properties for remaining triangles, using reversed box */
  FOR_EACH(i, 0, sizeof(property) / sizeof(*property))
    property[i] = (i % 2) ? SG3D_UNSPECIFIED_PROPERTY : 0;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 5 * ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  FOR_EACH(i, 0, count) {
    unsigned prop[3];
    OK(sg3d_geometry_get_unique_triangle_properties(geom, i, prop));
    CHK(prop[SG3D_FRONT] == 0 && prop[SG3D_BACK] == 1
      && prop[SG3D_INTFACE] == 0);
  }

  OK(sg3d_geometry_dump_as_c_code(geom, stdout, "all_defined",
    SG3D_C_DUMP_STATIC | SG3D_C_DUMP_CONST));

  /* Define incoherent properties for some triangles */
  FOR_EACH(i, 0, sizeof(property) / sizeof(*property))
    property[i] = (i % 2);
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == ntriangles / 2);
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 6 * ntriangles);

  OK(sg3d_geometry_ref_put(geom));
  OK(sg3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
