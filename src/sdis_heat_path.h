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

#ifndef SDIS_HEAT_PATH_H
#define SDIS_HEAT_PATH_H

#include <rsys/rsys.h>

struct rwalk_2d;
struct rwalk_3d;
struct rwalk_context;
struct sdis_scene;
struct ssp_rng;
struct temperature_2d;
struct temperature_3d;

/*******************************************************************************
 * Trace or pursue a radiative path
 ******************************************************************************/
extern LOCAL_SYM res_T
trace_radiative_path_2d
  (struct sdis_scene* scn,
   const float ray_dir[3],
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* temperature);

extern LOCAL_SYM res_T
trace_radiative_path_3d
  (struct sdis_scene* scn,
   const float ray_dir[3],
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* temperature);

extern LOCAL_SYM res_T
radiative_path_2d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* temperature);

extern LOCAL_SYM res_T
radiative_path_3d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* temperature);

/*******************************************************************************
 * Convective path
 ******************************************************************************/
extern LOCAL_SYM res_T
convective_path_2d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* temperature);

extern LOCAL_SYM res_T
convective_path_3d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* temperature);

/*******************************************************************************
 * Conductive path
 ******************************************************************************/
extern LOCAL_SYM res_T
conductive_path_2d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* temperature);

extern LOCAL_SYM res_T
conductive_path_3d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* temperature);

/*******************************************************************************
 * Boundary sub-path
 ******************************************************************************/
extern LOCAL_SYM res_T
boundary_path_2d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* temperature);

extern LOCAL_SYM res_T
boundary_path_3d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* temperature);

#endif /* SDIS_HEAT_PATH_H */

