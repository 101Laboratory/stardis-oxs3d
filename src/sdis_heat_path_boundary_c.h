/* Copyright (C) 2016-2021 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_HEAT_PATH_BOUNDARY_C_H
#define SDIS_HEAT_PATH_BOUNDARY_C_H

#include <rsys/rsys.h>

/* Forward declarations */
struct rwalk_2d;
struct rwalk_3d;
struct s2d_hit;
struct s3d_hit;
struct sdis_scene;
struct sdis_medium;

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
extern LOCAL_SYM res_T
select_reinjection_dir_2d
  (const struct sdis_scene* scn,
   const struct sdis_medium* mdm, /* Medium into which the reinjection occurs */
   struct rwalk_2d* rwalk, /* Current random walk state */
   const float dir0[2], /* Challenged direction */
   const float dir1[2], /* Challanged direction */
   const double delta, /* Max reinjection distance */
   float reinject_dir[2], /* Selected direction */
   float* reinject_dst, /* Effective reinjection distance */
   int can_move, /* Define of the random wal pos can be moved or not */
   int* move_pos, /* Define if the current random walk was moved. May be NULL */
   struct s2d_hit* reinject_hit); /* Hit along the reinjection dir */

extern LOCAL_SYM res_T
select_reinjection_dir_3d
  (const struct sdis_scene* scn,
   const struct sdis_medium* mdm, /* Medium into which the reinjection occurs */
   struct rwalk_3d* rwalk, /* Current random walk state */
   const float dir0[3], /* Challenged direction */
   const float dir1[3], /* Challanged direction */
   const double delta, /* Max reinjection distance */
   float reinject_dir[3], /* Selected direction */
   float* reinject_dst, /* Effective reinjection distance */
   int can_move, /* Define of the random wal pos can be moved or not */
   int* move_pos, /* Define if the current random walk was moved. May be NULL */
   struct s3d_hit* reinject_hit); /* Hit along the reinjection dir */

extern LOCAL_SYM res_T
select_reinjection_dir_and_check_validity_2d
  (const struct sdis_scene* scn,
   const struct sdis_medium* mdm, /* Medium into which the reinjection occurs */
   struct rwalk_2d* rwalk, /* Current random walk state */
   const float dir0[2], /* Challenged direction */
   const float dir1[2], /* Challanged direction */
   const double delta, /* Max reinjection distance */
   float out_reinject_dir[2], /* Selected direction */
   float* out_reinject_dst, /* Effective reinjection distance */
   int can_move, /* Define of the random wal pos can be moved or not */
   int* move_pos, /* Define if the current random walk was moved. May be NULL */
   int* is_valid, /* Define if the reinjection defines a valid pos */
   struct s2d_hit* out_reinject_hit); /* Hit along the reinjection dir */

extern LOCAL_SYM res_T
select_reinjection_dir_and_check_validity_3d
  (const struct sdis_scene* scn,
   const struct sdis_medium* mdm, /* Medium into which the reinjection occurs */
   struct rwalk_3d* rwalk, /* Current random walk state */
   const float dir0[3], /* Challenged direction */
   const float dir1[3], /* Challanged direction */
   const double delta, /* Max reinjection distance */
   float out_reinject_dir[3], /* Selected direction */
   float* out_reinject_dst, /* Effective reinjection distance */
   int can_move, /* Define of the random wal pos can be moved or not */
   int* move_pos, /* Define if the current random walk was moved. May be NULL */
   int* is_valid, /* Define if the reinjection defines a valid pos */
   struct s3d_hit* out_reinject_hit); /* Hit along the reinjection dir */

/* Check that the interface fragment is consistent with the current state of
 * the random walk */
extern LOCAL_SYM int
check_rwalk_fragment_consistency_2d
  (const struct rwalk_2d* rwalk,
   const struct sdis_interface_fragment* frag);

extern LOCAL_SYM int
check_rwalk_fragment_consistency_3d
  (const struct rwalk_3d* rwalk,
   const struct sdis_interface_fragment* frag);

/*******************************************************************************
 * Boundary sub-paths 
 ******************************************************************************/
extern LOCAL_SYM res_T
solid_boundary_with_flux_path_2d
  (const struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   const double phi,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* T);

extern LOCAL_SYM res_T
solid_boundary_with_flux_path_3d
  (const struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   const double phi,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* T);

extern LOCAL_SYM res_T
solid_fluid_boundary_path_2d
  (const struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* T);

extern LOCAL_SYM res_T
solid_fluid_boundary_path_3d
  (const struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* T);

extern LOCAL_SYM res_T
solid_solid_boundary_path_2d
  (const struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* T);

extern LOCAL_SYM res_T
solid_solid_boundary_path_3d
  (const struct sdis_scene* scn,
   const struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* T);

#endif /* SDIS_HEAT_PATH_BOUNDARY_C_H */
