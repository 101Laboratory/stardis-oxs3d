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

#include <star/s2d.h>
#include <star/s3d.h>
#include <rsys/rsys.h>

/* Forward declarations */
struct rwalk_2d;
struct rwalk_3d;
struct sdis_scene;
struct sdis_medium;

/*******************************************************************************
 * Sample a reinjection step
 ******************************************************************************/
struct sample_reinjection_step_args_2d {
  struct ssp_rng* rng; /* Random number generator to use */
  const struct sdis_medium* solid; /* Solid in which to reinject */
  struct rwalk_2d* rwalk; /* Current state of the random walk */
  double distance; /* Maximum Reinjection distance */
  enum sdis_side side; /* Side of the boundary to re-inject */
};

struct sample_reinjection_step_args_3d {
  struct ssp_rng* rng; /* Random number generator to use */
  const struct sdis_medium* solid; /* Medium in which to reinject */
  struct rwalk_3d* rwalk; /* Current random walk state */
  double distance; /* Maximum Reinjection distance */
  enum sdis_side side; /* Side of the boundary to re-inject */
};

struct reinjection_step_2d {
  struct s2d_hit hit; /* Intersection along the reinjection direction */
  float direction[2]; /* Reinjection direction */
  float distance; /* Reinjection distance */
};

struct reinjection_step_3d {
  struct s3d_hit hit; /* Intersection along the reinjection direction */
  float direction[3]; /* Reinjection direction */
  float distance; /* Reinjection distance */
};

#define SAMPLE_REINJECTION_STEP_ARGS_NULL___2d \
  {NULL, NULL, NULL, -1, SDIS_SIDE_NULL__}
#define SAMPLE_REINJECTION_STEP_ARGS_NULL___3d \
  {NULL, NULL, NULL, -1, SDIS_SIDE_NULL__}
static const struct sample_reinjection_step_args_2d
SAMPLE_REINJECTION_STEP_ARGS_NULL_2d = SAMPLE_REINJECTION_STEP_ARGS_NULL___2d;
static const struct sample_reinjection_step_args_3d
SAMPLE_REINJECTION_STEP_ARGS_NULL_3d = SAMPLE_REINJECTION_STEP_ARGS_NULL___3d;

#define REINJECTION_STEP_NULL___2d {S2D_HIT_NULL__, {0,0}, 0}
#define REINJECTION_STEP_NULL___3d {S3D_HIT_NULL__, {0,0,0}, 0}
static const struct reinjection_step_2d 
REINJECTION_STEP_NULL_2d = REINJECTION_STEP_NULL___2d;
static const struct reinjection_step_3d
REINJECTION_STEP_NULL_3d = REINJECTION_STEP_NULL___3d;

extern LOCAL_SYM res_T
sample_reinjection_step_solid_fluid_2d
  (const struct sdis_scene* scn,
   const struct sample_reinjection_step_args_2d* args,
   struct reinjection_step_2d* step);

extern LOCAL_SYM res_T
sample_reinjection_step_solid_fluid_3d
  (const struct sdis_scene* scn,
   const struct sample_reinjection_step_args_3d* args,
   struct reinjection_step_3d *step);

extern LOCAL_SYM res_T
sample_reinjection_step_solid_solid_2d
  (const struct sdis_scene* scn,
   const struct sample_reinjection_step_args_2d* args_front,
   const struct sample_reinjection_step_args_2d* args_back,
   struct reinjection_step_2d* step_front,
   struct reinjection_step_2d* step_back);

extern LOCAL_SYM res_T
sample_reinjection_step_solid_solid_3d
  (const struct sdis_scene* scn,
   const struct sample_reinjection_step_args_3d* args_front,
   const struct sample_reinjection_step_args_3d* args_back,
   struct reinjection_step_3d* step_front,
   struct reinjection_step_3d* step_back);

/*******************************************************************************
 * Reinject the random walk into a solid
 ******************************************************************************/
struct solid_reinjection_args_2d {
  const struct reinjection_step_2d* reinjection; /* Reinjection to do */
  const struct rwalk_context* rwalk_ctx;
  struct rwalk_2d* rwalk; /* Current state of the random walk */
  struct ssp_rng* rng; /* Random number generator */
  struct temperature_2d* T;
  double fp_to_meter;
};

struct solid_reinjection_args_3d {
  const struct reinjection_step_3d* reinjection; /* Reinjection to do */
  const struct rwalk_context* rwalk_ctx;
  struct rwalk_3d* rwalk; /* Current state of the random walk */
  struct ssp_rng* rng; /* Random number generator */
  struct temperature_3d* T;
  double fp_to_meter;
};

#define SOLID_REINJECTION_ARGS_NULL___2d {NULL,NULL,NULL,NULL,NULL,0}
#define SOLID_REINJECTION_ARGS_NULL___3d {NULL,NULL,NULL,NULL,NULL,0}
static const struct solid_reinjection_args_2d SOLID_REINJECTION_ARGS_NULL_2d =
  SOLID_REINJECTION_ARGS_NULL___2d;
static const struct solid_reinjection_args_3d SOLID_REINJECTION_ARGS_NULL_3d =
  SOLID_REINJECTION_ARGS_NULL___3d;

extern LOCAL_SYM res_T
solid_reinjection_2d
  (struct sdis_medium* solid,
   struct solid_reinjection_args_2d* args);

extern LOCAL_SYM res_T
solid_reinjection_3d
  (struct sdis_medium* solid,
   struct solid_reinjection_args_3d* args);

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
