/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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
#include "sdis_heat_path_boundary_c.h"

/* Generate the helper routines */
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_boundary_Xd_c.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_boundary_Xd_c.h"

/* Generate the boundary path sub-routines */
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_boundary_Xd_solid_fluid_picard1.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_boundary_Xd_solid_fluid_picard1.h"
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_boundary_Xd_solid_fluid_picardN.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_boundary_Xd_solid_fluid_picardN.h"
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_boundary_Xd_solid_solid.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_boundary_Xd_solid_solid.h"

/* Generate the boundary path routines */
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_boundary_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_boundary_Xd.h"
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_boundary_Xd_handle_external_net_flux.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_boundary_Xd_handle_external_net_flux.h"
