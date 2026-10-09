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

#ifndef SG3D_SDIS3D_HELPER_H__
#define SG3D_SDIS3D_HELPER_H__

#include "sg3d.h"

#include <rsys/rsys.h>

struct sdis_interface;

/* The type to used as the void* parameter in the sdis_scene_create call */
struct sg3d_sdisXd_scene_create_context {
  struct sg3d_geometry* geometry;
  struct sdis_interface* (*app_interface_getter)(const size_t itri, void* data);
  void* app_interface_data;
};

/* Get vertex indices for the itri_th triangle.
 * Suitable for use as get_indices callback in sdis_scene_create calls. */
static FINLINE void
sg3d_sdisXd_geometry_get_indices
  (const size_t itri,
   size_t indices[SG3D_GEOMETRY_DIMENSION],
   void* ctx__)
{
  const struct sg3d_sdisXd_scene_create_context* ctx = ctx__;
  unsigned i, tmp[3];
  ASSERT(indices && ctx && ctx->geometry && itri <= UINT_MAX);
  SG3D(geometry_get_unique_triangle_vertices(ctx->geometry, (unsigned)itri, tmp));
  FOR_EACH(i, 0, SG3D_GEOMETRY_DIMENSION) indices[i] = tmp[i];
}

/* Get vertex indices for the itri_th triangle.
 * Suitable for use as get_position callback in sdis_scene_create calls. */
static FINLINE void
sg3d_sdisXd_geometry_get_position
  (const size_t ivert,
   double coord[SG3D_GEOMETRY_DIMENSION],
   void* ctx__)
{
  const struct sg3d_sdisXd_scene_create_context* ctx = ctx__;
  ASSERT(coord && ctx && ctx->geometry && ivert <= UINT_MAX);
  SG3D(geometry_get_unique_vertex(ctx->geometry, (unsigned)ivert, coord));
}

/* Get vertex indices for the itri_th triangle.
 * Suitable for use as get_indices callback in sdis_scene_create calls. */
static FINLINE void
sg3d_sdisXd_geometry_get_interface
  (const size_t itri,
   struct sdis_interface** bound,
   void* ctx__)
{
  const struct sg3d_sdisXd_scene_create_context* ctx = ctx__;
  ASSERT(bound && ctx && ctx->app_interface_getter && itri < UINT_MAX);
  *bound = ctx->app_interface_getter(itri, ctx->app_interface_data);
}

#endif /* SG3D_SDIS3D_HELPER_H__ */
