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

#ifndef SG3D_S3D_HELPER_H__
#define SG3D_S3D_HELPER_H__

#include "sg3d.h"
#include <star/senc3d.h>

#include <rsys/rsys.h>

/* Get vertex indices for the itri_th triangle.
 * Suitable for use as get_indice callback in s3d_mesh_setup_indexed_vertices
 * calls. */
static FINLINE void
sg3d_sXd_geometry_get_indices
  (const unsigned itri,
   unsigned indices[SG3D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct sg3d_geometry* geometry = ctx;
  ASSERT(indices && geometry);
  SG3D(geometry_get_unique_triangle_vertices(geometry, itri, indices));
}

/* Get coordinates for the ivert_th vertex.
 * Suitable for use as s3d_vertex_data getter for S3D_POSITION s3d_attrib_usage
 * in s3d_mesh_setup_indexed_vertices calls. */
static FINLINE void
sg3d_sXd_geometry_get_position
  (const unsigned ivert,
   float coord[SG3D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct sg3d_geometry* geometry = ctx;
  double tmp[3];
  int i;
  ASSERT(coord && geometry);
  SG3D(geometry_get_unique_vertex(geometry, ivert, tmp));
  FOR_EACH(i, 0, SG3D_GEOMETRY_DIMENSION) coord[i] = (float)tmp[i];
}

#endif /* SG3D_S3D_HELPER_H__ */
