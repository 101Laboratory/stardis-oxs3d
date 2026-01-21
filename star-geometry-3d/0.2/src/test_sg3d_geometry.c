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

static res_T
validate
  (const unsigned itri,
   const unsigned properties[SG3D_PROP_TYPES_COUNT__],
   void* context,
   int* properties_conflict)
{
  (void)itri; (void)properties; (void)context;
  *properties_conflict = 0;
  return RES_OK;
}

static res_T
merge_trg
  (const unsigned user_id,
   const unsigned itri,
   const int reversed_triangle,
   unsigned triangle_properties[SG3D_PROP_TYPES_COUNT__],
   const unsigned merged_properties[SG3D_PROP_TYPES_COUNT__],
   void* context,
   int* merge_conflict)
{
  ASSERT(triangle_properties && merged_properties && merge_conflict);
  (void)user_id; (void)reversed_triangle; (void)context;
  (void)triangle_properties; (void)merged_properties; (void)merge_conflict;
  *merge_conflict = (int)itri;
  return RES_OK;
}

static res_T
degenerated_triangle
  (const unsigned itri,
   void* context,
   int* abort)
{
  struct context* ctx = context;
  ASSERT(abort && ctx);
  (void)itri;
  *abort = *(int*)ctx->custom;
  return RES_OK;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sg3d_device* dev;
  struct sg3d_geometry* geom;
  double coord[3];
  unsigned indices[3];
  const unsigned degenerated1[3] = { 0, 0, 0 };
  const unsigned degenerated2[3] = { 0, 1, 2 };
  const double degenerated_vertices[3/*#vertices*/ * 3/*#coords per vertex*/]
    =  { 0.0, 0.0, 0.0,  1.0, 0.0, 0.0,  2.0, 0.0, 0.0 };
  unsigned properties[SG3D_PROP_TYPES_COUNT__];
  struct sg3d_geometry_add_callbacks callbacks = SG3D_ADD_CALLBACKS_NULL__;
  unsigned user_id;
  unsigned count, i;
  struct context ctx = CONTEXT_NULL__;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sg3d_device_create(NULL, &allocator, 1, &dev));

  BA(sg3d_geometry_create(NULL, &geom));
  BA(sg3d_geometry_create(dev, NULL));
  OK(sg3d_geometry_create(dev, &geom));

  BA(sg3d_geometry_ref_get(NULL));
  OK(sg3d_geometry_ref_get(geom));

  BA(sg3d_geometry_ref_put(NULL));
  OK(sg3d_geometry_ref_put(geom));
  OK(sg3d_geometry_ref_put(geom));

  OK(sg3d_geometry_create(dev, &geom));

  BA(sg3d_geometry_validate_properties(NULL, NULL, NULL));
  BA(sg3d_geometry_validate_properties(geom, NULL, NULL));
  BA(sg3d_geometry_validate_properties(NULL, validate, NULL));
  OK(sg3d_geometry_validate_properties(geom, validate, NULL));

  BA(sg3d_geometry_get_unique_vertices_count(NULL, NULL));
  BA(sg3d_geometry_get_unique_vertices_count(geom, NULL));
  BA(sg3d_geometry_get_unique_vertices_count(NULL, &count));
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));

  BA(sg3d_geometry_get_added_triangles_count(NULL, NULL));
  BA(sg3d_geometry_get_added_triangles_count(geom, NULL));
  BA(sg3d_geometry_get_added_triangles_count(NULL, &count));
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));

  BA(sg3d_geometry_get_unique_triangles_count(NULL, NULL));
  BA(sg3d_geometry_get_unique_triangles_count(geom, NULL));
  BA(sg3d_geometry_get_unique_triangles_count(NULL, &count));
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));

  BA(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(NULL, NULL));
  BA(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, NULL));
  BA(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(NULL, &count));
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));

  BA(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(NULL, NULL));
  BA(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, NULL));
  BA(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(NULL, &count));
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));

  BA(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(NULL, NULL));
  BA(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, NULL));
  BA(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(NULL, &count));
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));

  BA(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(NULL, NULL));
  BA(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, NULL));
  BA(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(NULL, &count));
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));

  BA(sg3d_geometry_dump_as_obj(NULL, NULL, 0));
  BA(sg3d_geometry_dump_as_obj(geom, NULL, 0));
  BA(sg3d_geometry_dump_as_obj(NULL, stdout, 0));
  BA(sg3d_geometry_dump_as_obj(NULL, NULL, SG3D_OBJ_DUMP_ALL));
  BA(sg3d_geometry_dump_as_obj(geom, stdout, 0));
  BA(sg3d_geometry_dump_as_obj(geom, NULL, SG3D_OBJ_DUMP_ALL));
  BA(sg3d_geometry_dump_as_obj(NULL, stdout, SG3D_OBJ_DUMP_ALL));
  /* BA because geometry is empty */
  BA(sg3d_geometry_dump_as_obj(geom, stdout, SG3D_OBJ_DUMP_ALL));

  BA(sg3d_geometry_dump_as_vtk(NULL, NULL));
  BA(sg3d_geometry_dump_as_vtk(geom, NULL));
  BA(sg3d_geometry_dump_as_vtk(NULL, stdout));
  /* BA because geometry is empty */
  BA(sg3d_geometry_dump_as_vtk(geom, stdout));

  BA(sg3d_geometry_dump_as_c_code(NULL, NULL, NULL, 0));
  BA(sg3d_geometry_dump_as_c_code(geom, NULL, NULL, 0));
  BA(sg3d_geometry_dump_as_c_code(NULL, stdout, NULL, 0));
  BA(sg3d_geometry_dump_as_c_code(NULL, NULL, "test", 0));
  BA(sg3d_geometry_dump_as_c_code(geom, NULL, "test", 0));
  BA(sg3d_geometry_dump_as_c_code(NULL, stdout, "test", 0));
  /* BA because geometry is empty */
  BA(sg3d_geometry_dump_as_c_code(geom, stdout, NULL, 0));
  BA(sg3d_geometry_dump_as_c_code(geom, stdout, "test", 0));

  BA(sg3d_geometry_add(NULL, 0, 0, &callbacks, NULL));
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 0);
  BA(sg3d_geometry_add(geom, nvertices, ntriangles, NULL, NULL));
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == ntriangles);
  /* Mandatory callbacks are NULL */
  callbacks.get_indices = NULL;
  callbacks.get_position = get_position;
  BA(sg3d_geometry_add(geom, 0, 0, &callbacks, NULL));
  callbacks.get_indices = get_indices;
  callbacks.get_position = NULL;
  BA(sg3d_geometry_add(geom, 0, 0, &callbacks, NULL));
  callbacks.get_indices = NULL;
  callbacks.get_position = NULL;
  BA(sg3d_geometry_add(geom, 0, 0, &callbacks, NULL));
  /* Add 0 items */
  callbacks.get_indices = get_indices;
  callbacks.get_position = get_position;
  OK(sg3d_geometry_add(geom, 0, 0, &callbacks, NULL));
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == ntriangles);

  /* A 3D cube.
   * 2 enclosures (inside, outside) sharing the same triangles,
   * but opposite sides */
  ctx.positions = cube_vertices;
  ctx.indices = cube_indices;
  ctx.front_media = medium0;
  ctx.back_media = medium1;
  ctx.intface = intface0;

  callbacks.get_indices = get_indices;
  callbacks.get_properties = get_properties;
  callbacks.get_position = get_position;

  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  OK(sg3d_geometry_dump_as_obj(geom, stdout, SG3D_OBJ_DUMP_ALL));
  OK(sg3d_geometry_dump_as_vtk(geom, stdout));
  OK(sg3d_geometry_dump_as_c_code(geom, stdout, NULL, 0));
  OK(sg3d_geometry_dump_as_c_code(geom, stdout, "test",
    SG3D_C_DUMP_STATIC | SG3D_C_DUMP_CONST));

  BA(sg3d_geometry_get_unique_vertex(NULL, ntriangles, NULL));
  BA(sg3d_geometry_get_unique_vertex(geom, ntriangles, NULL));
  BA(sg3d_geometry_get_unique_vertex(NULL, 0, NULL));
  BA(sg3d_geometry_get_unique_vertex(NULL, ntriangles, coord));
  BA(sg3d_geometry_get_unique_vertex(geom, 0, NULL));
  BA(sg3d_geometry_get_unique_vertex(geom, ntriangles, coord));
  BA(sg3d_geometry_get_unique_vertex(NULL, 0, coord));
  OK(sg3d_geometry_get_unique_vertex(geom, 0, coord));

  BA(sg3d_geometry_get_unique_triangle_vertices(NULL, ntriangles, NULL));
  BA(sg3d_geometry_get_unique_triangle_vertices(geom, ntriangles, NULL));
  BA(sg3d_geometry_get_unique_triangle_vertices(NULL, 0, NULL));
  BA(sg3d_geometry_get_unique_triangle_vertices(NULL, ntriangles, indices));
  BA(sg3d_geometry_get_unique_triangle_vertices(geom, 0, NULL));
  BA(sg3d_geometry_get_unique_triangle_vertices(geom, ntriangles, indices));
  BA(sg3d_geometry_get_unique_triangle_vertices(NULL, 0, indices));
  OK(sg3d_geometry_get_unique_triangle_vertices(geom, 0, indices));
  FOR_EACH(i, 0 , 3) CHK(indices[i] == cube_indices[i]);

  BA(sg3d_geometry_get_unique_triangle_properties(NULL, ntriangles, NULL));
  BA(sg3d_geometry_get_unique_triangle_properties(geom, ntriangles, NULL));
  BA(sg3d_geometry_get_unique_triangle_properties(NULL, 0, NULL));
  BA(sg3d_geometry_get_unique_triangle_properties(NULL, ntriangles, properties));
  BA(sg3d_geometry_get_unique_triangle_properties(geom, 0, NULL));
  BA(sg3d_geometry_get_unique_triangle_properties(geom, ntriangles, properties));
  BA(sg3d_geometry_get_unique_triangle_properties(NULL, 0, properties));
  OK(sg3d_geometry_get_unique_triangle_properties(geom, 0, properties));
  CHK(medium0[0] == properties[SG3D_FRONT]);
  CHK(medium1[0] == properties[SG3D_BACK]);
  CHK(intface0[0] == properties[SG3D_INTFACE]);

  BA(sg3d_geometry_get_unique_triangle_user_id(NULL, ntriangles, NULL));
  BA(sg3d_geometry_get_unique_triangle_user_id(geom, ntriangles, NULL));
  BA(sg3d_geometry_get_unique_triangle_user_id(NULL, 0, NULL));
  BA(sg3d_geometry_get_unique_triangle_user_id(NULL, ntriangles, &user_id));
  BA(sg3d_geometry_get_unique_triangle_user_id(geom, 0, NULL));
  BA(sg3d_geometry_get_unique_triangle_user_id(geom, ntriangles, &user_id));
  BA(sg3d_geometry_get_unique_triangle_user_id(NULL, 0, &user_id));
  OK(sg3d_geometry_get_unique_triangle_user_id(geom, 0, &user_id));
  /* Due to a failed attempt to add ntriangles triangles, user_id for the
   * first successfully added triangle is shifted */
  CHK(user_id == ntriangles);

  /* Conflicts with merge_trg callback */
  callbacks.merge_triangle = merge_trg;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  /* Due to merge_trg internals, all but the first triangle have conflict */
  CHK(count == ntriangles - 1);
  OK(sg3d_geometry_dump_as_obj(geom, stdout, SG3D_OBJ_DUMP_ALL));
  OK(sg3d_geometry_dump_as_vtk(geom, stdout));
  /* BA because of conflicts */
  BA(sg3d_geometry_dump_as_c_code(geom, stdout, "test", SG3D_C_DUMP_STATIC));
  OK(sg3d_geometry_ref_put(geom));

  /* Conflicts without merge_trg callback */
  OK(sg3d_geometry_create(dev, &geom));
  callbacks.merge_triangle = NULL;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  ctx.front_media = medium1_front0;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  FOR_EACH(i, 0, ntriangles) if(medium0[i] != medium1_front0[i]) count--;
  CHK(count == 0);
  OK(sg3d_geometry_dump_as_obj(geom, stdout, SG3D_OBJ_DUMP_ALL));
  OK(sg3d_geometry_dump_as_vtk(geom, stdout));
  /* BA because of conflicts */
  BA(sg3d_geometry_dump_as_c_code(geom, stdout, "test", SG3D_C_DUMP_CONST));

  /* Degenerated triangles: duplicated vertex */
  ctx.indices = degenerated1;
  /* Without callback : OK */
  OK(sg3d_geometry_add(geom, nvertices, 1, &callbacks, &ctx));
  /* With callback : OK */
  callbacks.degenerated_triangle = degenerated_triangle;
  ctx.custom = &i;
  i = 0;
  OK(sg3d_geometry_add(geom, nvertices, 1, &callbacks, &ctx));
  /* With callback : KO */
  i = 1;
  BA(sg3d_geometry_add(geom, nvertices, 1, &callbacks, &ctx));

  /* Degenerated triangles: flat triangle */
  ctx.indices = degenerated2;
  ctx.positions = degenerated_vertices;
  callbacks.degenerated_triangle = NULL;
  /* Without callback : OK */
  OK(sg3d_geometry_add(geom, nvertices, 1, &callbacks, &ctx));
  /* With callback : OK */
  callbacks.degenerated_triangle = degenerated_triangle;
  ctx.custom = &i;
  i = 0;
  OK(sg3d_geometry_add(geom, nvertices, 1, &callbacks, &ctx));
  /* With callback : KO */
  i = 1;
  BA(sg3d_geometry_add(geom, nvertices, 1, &callbacks, &ctx));

  OK(sg3d_geometry_ref_put(geom));
  OK(sg3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
