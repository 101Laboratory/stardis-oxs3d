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

#ifndef STAR_GEOMETRY3D_X_H__
#define STAR_GEOMETRY3D_X_H__

#if !defined(SGXD_DIM) || (SGXD_DIM != 2 && SGXD_DIM != 3)
#error "SGXD_DIM must be defined; admissible values are 2 and 3"
#endif

#include <star/sg3d.h>

/* Star-geometry-XD macros generic to the SGXD_DIM */
#ifndef SGXD
#define SGXD CONCAT(CONCAT(SGX, SGXD_DIM), D)
#endif
#ifndef sgXd
#define sgXd(Name) CONCAT(CONCAT(CONCAT(sg, SGXD_DIM), d_), Name)
#endif
#ifndef SGXD_
#define SGXD_(Name) CONCAT(CONCAT(CONCAT(SGX, SGXD_DIM), D_), Name)
#endif

/* Function names that require additional dedicated macros */
#define sgXd_geometry_get_added_primitives_count \
  sg3d_geometry_get_added_triangles_count
#define sgXd_geometry_get_unique_primitives_count \
  sg3d_geometry_get_unique_triangles_count
#define sgXd_geometry_get_unique_primitive_vertices \
  sg3d_geometry_get_unique_triangle_vertices
#define sgXd_geometry_get_unique_primitive_properties \
  sg3d_geometry_get_unique_triangle_properties
#define sgXd_geometry_get_unique_primitive_user_id \
  sg3d_geometry_get_unique_triangle_user_id
#define sgXd_geometry_get_unique_primitives_with_unspecified_side_count \
  sg3d_geometry_get_unique_triangles_with_unspecified_side_count
#define sgXd_geometry_get_unique_primitives_with_unspecified_interface_count \
  sg3d_geometry_get_unique_triangles_with_unspecified_interface_count
#define sgXd_geometry_get_unique_primitives_with_properties_conflict_count \
  sg3d_geometry_get_unique_triangles_with_properties_conflict_count

#endif /* STAR_GEOMETRY3D_X_H__ */
