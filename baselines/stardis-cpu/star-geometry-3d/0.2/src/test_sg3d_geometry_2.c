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

 /* Manage add_geometry behaviour */
struct add_geom_ctx {
  unsigned add_cpt, merge_cpt;
  res_T add_res, merge_res;
};

static res_T
add_trg
  (const unsigned unique_id,
   const unsigned iseg,
   void* context)
{
  struct context* ctx = context;
  struct add_geom_ctx* add_geom_ctx;
  ASSERT(ctx); (void)unique_id; (void)iseg;
  add_geom_ctx = ctx->custom;
  if(add_geom_ctx->add_res == RES_OK) ++add_geom_ctx->add_cpt;
  return add_geom_ctx->add_res;
}

static res_T
merge_trg
  (const unsigned unique_id,
   const unsigned itri,
   const int reversed_triangle,
   unsigned triangle_properties[SG3D_PROP_TYPES_COUNT__],
   const unsigned merged_properties[SG3D_PROP_TYPES_COUNT__],
   void* context,
   int* merge_conflict)
{
  struct context* ctx = context;
  struct add_geom_ctx* add_geom_ctx;
  int i;
  ASSERT(ctx && triangle_properties && merged_properties && merge_conflict);
  (void)unique_id; (void)itri; (void)reversed_triangle;
  (void)triangle_properties; (void)merged_properties;
  add_geom_ctx = ctx->custom;
  if(add_geom_ctx->merge_res == RES_OK) ++add_geom_ctx->merge_cpt;
  FOR_EACH(i, 0, SG3D_PROP_TYPES_COUNT__)
    if(!sg3d_compatible_property(triangle_properties[i], merged_properties[i]))
      *merge_conflict = 1;
  return add_geom_ctx->merge_res;
}

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
validate2
  (const unsigned itri,
   const unsigned properties[SG3D_PROP_TYPES_COUNT__],
   void* context,
   int* properties_conflict)
{
  (void)itri; (void)properties; (void)context;
  *properties_conflict = (itri % 2 == 0) ? 0 : (int)itri;
  return RES_OK;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sg3d_device* dev;
  struct sg3d_geometry* geom;
  struct context ctx = CONTEXT_NULL__;
  struct sg3d_geometry_add_callbacks callbacks = SG3D_ADD_CALLBACKS_NULL__;
  struct add_geom_ctx add_geom_ctx;
  unsigned property[12];
  unsigned i;
  const unsigned property_count = sizeof(property) / sizeof(*property);
  unsigned count;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sg3d_device_create(NULL, &allocator, 1, &dev));
  OK(sg3d_geometry_create(dev, &geom));

  /* A 3D cube.
   * 2 enclosures (inside, outside) sharing the same triangles,
   * but opposite sides */
  ctx.positions = cube_vertices;
  ctx.indices = cube_indices;
  ctx.custom = &add_geom_ctx;

  add_geom_ctx.add_cpt = add_geom_ctx.merge_cpt = 0;
  add_geom_ctx.add_res = add_geom_ctx.merge_res = RES_OK;

  /* Geometry with no media information on both sides */
  for(i = 0; i < property_count; i++) property[i] = SG3D_UNSPECIFIED_PROPERTY;
  ctx.front_media = property;
  ctx.back_media = property;
  ctx.intface = property;

  callbacks.get_indices = get_indices;
  callbacks.get_properties = get_properties;
  callbacks.get_position = get_position;
  callbacks.add_triangle = add_trg;
  callbacks.merge_triangle = merge_trg;

  /* If add fails, add geometry fails the same way */
  add_geom_ctx.add_res = RES_BAD_ARG;

  BA(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.add_cpt == 0);
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == 0);
  add_geom_ctx.add_res = RES_MEM_ERR;
  ME(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.add_cpt == 0);
  CHK(count == 0);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 2 * ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  /* Successful add geometry with add callback */
  add_geom_ctx.add_res = RES_OK;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.add_cpt == ntriangles);
  CHK(add_geom_ctx.merge_cpt == 0);
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 3 * ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  OK(sg3d_geometry_dump_as_c_code(geom, stdout, "test_unspecified",
    SG3D_C_DUMP_STATIC | SG3D_C_DUMP_CONST));

  /* Clear geometry */
  SG3D(geometry_ref_put(geom));
  OK(sg3d_geometry_create(dev, &geom));

  /* Successful add geometry without add callback */
  add_geom_ctx.add_cpt = 0;
  callbacks.add_triangle = NULL;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.add_cpt == 0);
  CHK(add_geom_ctx.merge_cpt == 0);
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  /* If merge fails, add geometry fails the same way */
  add_geom_ctx.merge_res = RES_BAD_ARG;
  callbacks.add_triangle = add_trg;
  BA(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.merge_cpt == 0);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 2 * ntriangles);
  add_geom_ctx.merge_res = RES_MEM_ERR;
  ME(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.merge_cpt == 0);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 3 * ntriangles);
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0); /* merge failed but with a no-conflict status */
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  /* Successful add geometry without merge callback */
  callbacks.merge_triangle = NULL;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.merge_cpt == 0);
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 4 * ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  /* Successful add geometry with merge callback */
  add_geom_ctx.merge_res = RES_OK;
  callbacks.merge_triangle = merge_trg;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.merge_cpt == ntriangles);
  add_geom_ctx.merge_cpt = 0;
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 5 * ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0); /* merge failed but with a no-conflict status */
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  /* Geometry with media information on both sides */
  ctx.front_media = medium0;
  ctx.back_media = medium1;

  /* Clear geometry */
  SG3D(geometry_ref_put(geom));
  OK(sg3d_geometry_create(dev, &geom));

  /* Successful add geometry with add callback
   * First half of the triangles, then all of them */
  add_geom_ctx.add_res = RES_OK;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles / 2, &callbacks, &ctx));
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == ntriangles / 2);
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.add_cpt == ntriangles);
  CHK(add_geom_ctx.merge_cpt == ntriangles / 2);
  add_geom_ctx.add_cpt = 0;
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == ntriangles + ntriangles / 2);
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == 0); /* media where defined */
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == ntriangles); /* interfaces where unspecified */
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_dump_as_vtk(geom, stdout));
  /* Second add was half duplicated, so numbering is shifted */
  FOR_EACH(i, 0, ntriangles) {
    unsigned id;
    OK(sg3d_geometry_get_unique_triangle_user_id(geom, i, &id));
    CHK(i < ntriangles / 2 ? id == i : id == i + ntriangles / 2);
  }

  /* Clear geometry */
  SG3D(geometry_ref_put(geom));
  OK(sg3d_geometry_create(dev, &geom));
  add_geom_ctx.merge_cpt = 0;

  /* Successful add geometry with add callback and no defined properties */
  add_geom_ctx.add_res = RES_OK;
  callbacks.get_properties = NULL;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.add_cpt == ntriangles);
  CHK(add_geom_ctx.merge_cpt == 0);
  add_geom_ctx.add_cpt = 0;
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == ntriangles); /* media where unspecified */
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == ntriangles); /* interfaces where unspecified */
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  /* Define interface */
  ctx.intface = intface0;

  /* Successful add geometry with merge callback and properties */
  add_geom_ctx.merge_res = RES_OK;
  callbacks.get_properties = get_properties;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.merge_cpt == ntriangles);
  add_geom_ctx.merge_cpt = 0;
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 2 * ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == 0); /* media where defined */
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == 0); /* interfaces where defined */
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  /* Geometry with incompatible media information on both sides */
  ctx.front_media = medium1;
  ctx.back_media = medium0;

  /* Add geometry without merge callback and conflicts */
  callbacks.merge_triangle = NULL;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.merge_cpt == 0);
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 3 * ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == 0); /* media where defined */
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == 0); /* interfaces where defined */
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  /* Incompatible interface */
  ctx.intface = intface1;

  /* Successful add geometry with merge callback */
  add_geom_ctx.merge_res = RES_OK;
  callbacks.merge_triangle = merge_trg;
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  CHK(add_geom_ctx.merge_cpt == ntriangles);
  add_geom_ctx.merge_cpt = 0;
  OK(sg3d_geometry_get_unique_vertices_count(geom, &count));
  CHK(count == nvertices);
  OK(sg3d_geometry_get_added_triangles_count(geom, &count));
  CHK(count == 4 * ntriangles);
  OK(sg3d_geometry_get_unique_triangles_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_side_count(geom, &count));
  CHK(count == 0); /* media where defined */
  OK(sg3d_geometry_get_unique_triangles_with_unspecified_interface_count(geom, &count));
  CHK(count == 0); /* interfaces where defined */
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == ntriangles);
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  /* Clear geometry */
  SG3D(geometry_ref_put(geom));
  OK(sg3d_geometry_create(dev, &geom));

  /* Successful add geometry with merge callback */
  OK(sg3d_geometry_add(geom, nvertices, ntriangles, &callbacks, &ctx));
  OK(sg3d_geometry_get_unique_triangles_with_merge_conflict_count(geom, &count));
  CHK(count == 0);

  OK(sg3d_geometry_validate_properties(geom, validate, NULL));
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);
  OK(sg3d_geometry_dump_as_obj(geom, stdout, SG3D_OBJ_DUMP_ALL));

  OK(sg3d_geometry_validate_properties(geom, validate, NULL));
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == 0);

  OK(sg3d_geometry_validate_properties(geom, validate2, NULL));
  OK(sg3d_geometry_get_unique_triangles_with_properties_conflict_count(geom, &count));
  CHK(count == ntriangles / 2);

  OK(sg3d_geometry_ref_put(geom));
  OK(sg3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
