/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

#include "sdis_heat_path.h"

/* Generate the radiative paths */
#define SDIS_SOLVE_DIMENSION 2
#include "sdis_heat_path_radiative_Xd.h"
#define SDIS_SOLVE_DIMENSION 3
#include "sdis_heat_path_radiative_Xd.h"

/* Generate the convective paths */
#define SDIS_SOLVE_DIMENSION 2
#include "sdis_heat_path_convective_Xd.h"
#define SDIS_SOLVE_DIMENSION 3
#include "sdis_heat_path_convective_Xd.h"

/* Generate the conductive paths */
#define SDIS_SOLVE_DIMENSION 2
#include "sdis_heat_path_conductive_Xd.h"
#define SDIS_SOLVE_DIMENSION 3
#include "sdis_heat_path_conductive_Xd.h"

/* Generate the boundary paths */
#define SDIS_SOLVE_DIMENSION 2
#include "sdis_heat_path_boundary_Xd.h"
#define SDIS_SOLVE_DIMENSION 3
#include "sdis_heat_path_boundary_Xd.h"

