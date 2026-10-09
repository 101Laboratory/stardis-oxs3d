/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SENC2D_S2D_WRAPPER_H
#define SENC2D_S2D_WRAPPER_H

#include "senc2d.h"

#include <rsys/rsys.h>

/* Get vertex indices for the iseg_th segment.
 * Suitable for use as get_indice callback in s2d_mesh_setup_indexed_vertices
 * calls. */
static FINLINE void
senc2d_sXd_scene_get_indices
  (const unsigned iseg,
   unsigned indices[SENC2D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct senc2d_scene* scene = ctx;
  ASSERT(indices && scene);
  SENC2D(scene_get_segment(scene, iseg, indices));
}

/* Get coordinates for the ivert_th vertex.
 * Suitable for use as s2d_vertex_data getter for S2D_POSITION s2d_attrib_usage
 * in s2d_mesh_setup_indexed_vertices calls. */
static FINLINE void
senc2d_sXd_scene_get_position
  (const unsigned ivert,
   float coord[SENC2D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct senc2d_scene* scene = ctx;
  double tmp[SENC2D_GEOMETRY_DIMENSION];
  int i;
  ASSERT(coord && scene);
  SENC2D(scene_get_vertex(scene, ivert, tmp));
  FOR_EACH(i, 0, SENC2D_GEOMETRY_DIMENSION) coord[i] = (float)tmp[i];
}

/* Get vertex indices for the iseg_th segment of the enclosure.
 * Suitable for use as get_indice callback in s2d_mesh_setup_indexed_vertices
 * calls. */
static FINLINE void
senc2d_sXd_enclosure_get_indices
  (const unsigned iseg,
   unsigned indices[SENC2D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct senc2d_enclosure* enclosure = ctx;
  ASSERT(indices && ctx);
  SENC2D(enclosure_get_segment(enclosure, iseg, indices));
}

/* Get coordinates for the ivert_th vertex of the enclosure.
 * Suitable for use as s2d_vertex_data getter for S2D_POSITION s2d_attrib_usage
 * in s2d_mesh_setup_indexed_vertices calls. */
static FINLINE void
senc2d_sXd_enclosure_get_position
  (const unsigned ivert,
   float coord[SENC2D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct senc2d_enclosure* enclosure = ctx;
  double tmp[SENC2D_GEOMETRY_DIMENSION];
  int i;
  ASSERT(coord && ctx);
  SENC2D(enclosure_get_vertex(enclosure, ivert, tmp));
  FOR_EACH(i, 0, SENC2D_GEOMETRY_DIMENSION) coord[i] = (float)tmp[i];
}

#endif /* SENC2D_S2D_WRAPPER_H */
