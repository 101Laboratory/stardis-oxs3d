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

#ifndef SDIS_HEAT_PATH_CONDUCTIVE_C_H
#define SDIS_HEAT_PATH_CONDUCTIVE_C_H

#include <rsys/rsys.h>

/* Forward declarations */
struct rwalk;
struct rwalk_context;
struct sdis_device;
struct sdis_medium;
struct sdis_scene;
struct solid_props;
struct ssp_rng;
struct temperature;

extern LOCAL_SYM res_T
check_solid_constant_properties
  (struct sdis_device* dev,
   const int evaluate_green,
   const int use_wos_diffusion,
   const struct solid_props* props_ref,
   const struct solid_props* props);

/*******************************************************************************
 * Conductive paths using custom user algorithm
 ******************************************************************************/
extern LOCAL_SYM res_T
conductive_path_custom_2d
  (struct sdis_scene* scn,
   const unsigned enc_id, /* Enclosure in which path is sampled */
   const struct sdis_medium* mdm, /* Medium in which path is sampled */
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
conductive_path_custom_3d
  (struct sdis_scene* scn,
   const unsigned enc_id, /* Enclosure in which path is sampled */
   const struct sdis_medium* mdm, /* Medium in which path is sampled */
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

/*******************************************************************************
 * Conductive paths using the delta sphere algorithm
 ******************************************************************************/
extern LOCAL_SYM res_T
conductive_path_delta_sphere_2d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
conductive_path_delta_sphere_3d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

/*******************************************************************************
 * Conductive paths using the walk on sphere algorithm
 ******************************************************************************/
extern LOCAL_SYM res_T
conductive_path_wos_2d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
conductive_path_wos_3d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

#endif /* SDIS_HEAT_PATH_CONDUCTIVE_C_H */
