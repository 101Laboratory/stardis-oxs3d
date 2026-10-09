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

#ifndef SG3D_SENC3D_HELPER_H__
#define SG3D_SENC3D_HELPER_H__

#include "sg3d.h"
#include <star/senc3d.h>

#include <rsys/rsys.h>

/* Get vertex indices for the itri_th triangle.
 * Suitable for use as get_indices callback in senc_scene_create calls. */
static FINLINE void
sg3d_sencXd_geometry_get_indices
  (const unsigned itri,
   unsigned indices[SG3D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct sg3d_geometry* geometry = ctx;
  ASSERT(indices && geometry);
  SG3D(geometry_get_unique_triangle_vertices(geometry, itri, indices));
}

/* Get vertex indices for the itri_th triangle.
 * Suitable for use as get_media callback in senc_scene_create calls. */
static FINLINE void
sg3d_sencXd_geometry_get_media
  (const unsigned itri,
   unsigned media[2],
   void* ctx)
{
  const struct sg3d_geometry* geometry = ctx;
  unsigned tmp[SG3D_PROP_TYPES_COUNT__];
  ASSERT(media && geometry);
  SG3D(geometry_get_unique_triangle_properties(geometry, itri, tmp));
  media[SENC3D_FRONT] = (tmp[SG3D_FRONT] == SG3D_UNSPECIFIED_PROPERTY)
    ? SENC3D_UNSPECIFIED_MEDIUM : tmp[SG3D_FRONT];
  media[SENC3D_BACK] = (tmp[SG3D_BACK] == SG3D_UNSPECIFIED_PROPERTY)
    ? SENC3D_UNSPECIFIED_MEDIUM : tmp[SG3D_BACK];
}

/* Get vertex indices for the itri_th triangle.
 * Suitable for use as get_position callback in senc_scene_create calls. */
static FINLINE void
sg3d_sencXd_geometry_get_position
  (const unsigned ivert,
   double coord[SG3D_GEOMETRY_DIMENSION],
   void* ctx)
{
  const struct sg3d_geometry* geometry = ctx;
  ASSERT(coord && geometry);
  SG3D(geometry_get_unique_vertex(geometry, ivert, coord));
}

#endif /* SG2_SENC3D_HELPER_H__ */
