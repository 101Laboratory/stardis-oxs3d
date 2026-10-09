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
#error "The sgX3d.h file must be included priorly to this file."
#endif

/* Star-geometry-XD macros generic to the SGXD_DIM */
#undef SGXD
#undef sgXd
#undef SGXD_

/* Function names that require additional dedicated macros */
#undef sgXd_geometry_get_added_primitives_count
#undef sgXd_geometry_get_unique_primitives_count
#undef sgXd_geometry_get_unique_primitive_vertices
#undef sgXd_geometry_get_unique_primitive_properties
#undef sgXd_geometry_get_unique_primitive_user_id
#undef sgXd_geometry_get_unique_primitives_with_unspecified_side_count
#undef sgXd_geometry_get_unique_primitives_with_unspecified_interface_count
#undef sgXd_geometry_get_unique_primitives_with_properties_conflict_count

#undef STAR_GEOMETRY3D_X_H__
