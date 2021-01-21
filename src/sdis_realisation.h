/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_REALISATION_H
#define SDIS_REALISATION_H

#include "sdis.h"
#include "sdis_estimator_c.h"

#include <rsys/rsys.h>

/* Forward declarations */
struct green_path_handle;
struct sdis_heat_path;
struct sdis_scene;
struct ssp_rng;
struct htable_primitive_ids;
struct bound_flux_result;

enum flux_flag {
  FLUX_FLAG_CONVECTIVE = BIT(FLUX_CONVECTIVE),
  FLUX_FLAG_RADIATIVE = BIT(FLUX_RADIATIVE),
  FLUX_FLAGS_ALL = FLUX_FLAG_CONVECTIVE | FLUX_FLAG_RADIATIVE
};

/*******************************************************************************
 * Realisation at a given position and time IN a medium
 ******************************************************************************/
extern LOCAL_SYM res_T
probe_realisation_2d
  (const size_t irealisation, /* For debug */
   struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct sdis_medium* medium,
   const double position[2],
   const double time,
   struct green_path_handle* green_path, /* May be NULL */
   struct sdis_heat_path* heat_path, /* May be NULL */
   double* weight);

extern LOCAL_SYM res_T
probe_realisation_3d
  (const size_t irealisation, /* For debug */
   struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct sdis_medium* medium,
   const double position[3],
   const double time,
   struct green_path_handle* green_path, /* May be NULL */
   struct sdis_heat_path* heat_path, /* May be NULL */
   double* weight);

/*******************************************************************************
 * Realisation at a given position and time ON a given side of a boundary
 ******************************************************************************/
extern LOCAL_SYM res_T
boundary_realisation_2d
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const size_t iprim,
   const double u[1],
   const double time,
   const enum sdis_side side,
   struct green_path_handle* green_path, /* May be NULL */
   struct sdis_heat_path* heat_path, /* May be NULL */
   double* weight);

extern LOCAL_SYM res_T
boundary_realisation_3d
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const size_t iprim,
   const double uv[2],
   const double time,
   const enum sdis_side side,
   struct green_path_handle* green_path, /* May be NULL */
   struct sdis_heat_path* heat_path, /* May be NULL */
   double* weight);

extern LOCAL_SYM res_T
boundary_flux_realisation_2d
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const size_t iprim,
   struct htable_primitive_ids* self,
   const double uv[1],
   const double time,
   const enum sdis_side solid_side,
   const int flux_mask, /* Combination of enum flux_flag */
   struct bound_flux_result* result);

extern LOCAL_SYM res_T
boundary_flux_realisation_3d
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const size_t iprim,
   struct htable_primitive_ids* self,
   const double uv[2],
   const double time,
   const enum sdis_side solid_side,
   const int flux_mask, /* Combination of enum flux_flag */
   struct bound_flux_result* result);

/*******************************************************************************
 * Realisation along a given ray at a given time. Available only in 3D.
 ******************************************************************************/
extern LOCAL_SYM res_T
ray_realisation_3d
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct sdis_medium* medium,
   const double position[3],
   const double direction[3],
   const double time,
   struct sdis_heat_path* heat_path, /* May be NULL */
   double* weight);

#endif /* SDIS_REALISATION_H */
