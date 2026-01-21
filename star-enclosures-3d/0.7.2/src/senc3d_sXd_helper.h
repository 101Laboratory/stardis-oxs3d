/* Copyright (C) 2018-2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SENC3D_S3D_WRAPPER_H
#define SENC3D_S3D_WRAPPER_H

#include "senc3d.h"

#include <rsys/rsys.h>

/* Get vertex indices for the itri_th triangle.
 * Suitable for use as get_indice callback in s3d_mesh_setup_indexed_vertices
 * calls. */
static FINLINE void
senc3d_sXd_scene_get_indices
  (const unsigned itri,
   unsigned indices[SENC3D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct senc3d_scene* scene = ctx;
  ASSERT(indices && scene);
  SENC3D(scene_get_triangle(scene, itri, indices));
}

/* Get coordinates for the ivert_th vertex.
 * Suitable for use as s3d_vertex_data getter for S3D_POSITION s3d_attrib_usage
 * in s3d_mesh_setup_indexed_vertices calls. */
static FINLINE void
senc3d_sXd_scene_get_position
  (const unsigned ivert,
   float coord[SENC3D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct senc3d_scene* scene = ctx;
  double tmp[SENC3D_GEOMETRY_DIMENSION];
  int i;
  ASSERT(coord && scene);
  SENC3D(scene_get_vertex(scene, ivert, tmp));
  FOR_EACH(i, 0, SENC3D_GEOMETRY_DIMENSION) coord[i] = (float)tmp[i];
}

/* Get vertex indices for the itri_th triangle of the enclosure.
 * Suitable for use as get_indice callback in s3d_mesh_setup_indexed_vertices
 * calls. */
static FINLINE void
senc3d_sXd_enclosure_get_indices
  (const unsigned itri,
   unsigned indices[SENC3D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct senc3d_enclosure* enclosure = ctx;
  ASSERT(indices && ctx);
  SENC3D(enclosure_get_triangle(enclosure, itri, indices));
}

/* Get coordinates for the ivert_th vertex of the enclosure.
 * Suitable for use as s3d_vertex_data getter for S3D_POSITION s3d_attrib_usage
 * in s3d_mesh_setup_indexed_vertices calls. */
static FINLINE void
senc3d_sXd_enclosure_get_position
  (const unsigned ivert,
   float coord[SENC3D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct senc3d_enclosure* enclosure = ctx;
  double tmp[SENC3D_GEOMETRY_DIMENSION];
  int i;
  ASSERT(coord && ctx);
  SENC3D(enclosure_get_vertex(enclosure, ivert, tmp));
  FOR_EACH(i, 0, SENC3D_GEOMETRY_DIMENSION) coord[i] = (float)tmp[i];
}

#endif /* SENC3D_S3D_WRAPPER_H */
