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

#ifndef SDIS_HEAT_PATH_BOUNDARY_C_H
#define SDIS_HEAT_PATH_BOUNDARY_C_H

#include <star/s2d.h>
#include <star/s3d.h>
#include <rsys/rsys.h>

/* Forward declarations */
struct rwalk;
struct sdis_scene;
struct sdis_medium;

/*******************************************************************************
 * Sample a reinjection step
 ******************************************************************************/
struct sample_reinjection_step_args {
  struct ssp_rng* rng; /* Random number generator to use */
  struct rwalk* rwalk; /* Current state of the random walk */
  double distance; /* Maximum Reinjection distance */
  unsigned solid_enc_id; /* Enclosured Id of the solid in which to reinject */
  enum sdis_side side; /* Side of the boundary to re-inject */
};

#define SAMPLE_REINJECTION_STEP_ARGS_NULL__ \
  {NULL, NULL, -1, ENCLOSURE_ID_NULL, SDIS_SIDE_NULL__}
static const struct sample_reinjection_step_args
SAMPLE_REINJECTION_STEP_ARGS_NULL = SAMPLE_REINJECTION_STEP_ARGS_NULL__;

struct reinjection_step  {
  struct s2d_hit hit_2d; /* 2D Intersection along the reinjection direction */
  struct s3d_hit hit_3d; /* 3D Intersection along the reinjection direction */
  float direction[3]; /* Reinjection direction */
  float distance; /* Reinjection distance */
};

#define REINJECTION_STEP_NULL__ {S2D_HIT_NULL__, S3D_HIT_NULL__, {0,0,0}, 0}
static const struct reinjection_step REINJECTION_STEP_NULL =
  REINJECTION_STEP_NULL__;

extern LOCAL_SYM res_T
sample_reinjection_step_solid_fluid_2d
  (struct sdis_scene* scn,
   const struct sample_reinjection_step_args* args,
   struct reinjection_step* step);

extern LOCAL_SYM res_T
sample_reinjection_step_solid_fluid_3d
  (struct sdis_scene* scn,
   const struct sample_reinjection_step_args* args,
   struct reinjection_step *step);

extern LOCAL_SYM res_T
sample_reinjection_step_solid_solid_2d
  (struct sdis_scene* scn,
   const struct sample_reinjection_step_args* args_front,
   const struct sample_reinjection_step_args* args_back,
   struct reinjection_step* step_front,
   struct reinjection_step* step_back);

extern LOCAL_SYM res_T
sample_reinjection_step_solid_solid_3d
  (struct sdis_scene* scn,
   const struct sample_reinjection_step_args* args_front,
   const struct sample_reinjection_step_args* args_back,
   struct reinjection_step* step_front,
   struct reinjection_step* step_back);

/*******************************************************************************
 * Reinject the random walk into a solid
 ******************************************************************************/
struct solid_reinjection_args {
  const struct reinjection_step* reinjection; /* Reinjection to do */
  struct rwalk_context* rwalk_ctx;
  struct rwalk* rwalk; /* Current state of the random walk */
  struct ssp_rng* rng; /* Random number generator */
  struct temperature* T;
  double fp_to_meter;
};

#define SOLID_REINJECTION_ARGS_NULL__ {NULL,NULL,NULL,NULL,NULL,0}
static const struct solid_reinjection_args SOLID_REINJECTION_ARGS_NULL =
  SOLID_REINJECTION_ARGS_NULL__;

extern LOCAL_SYM res_T
solid_reinjection_2d
  (struct sdis_scene* scn,
   const unsigned solid_enc_id,
   struct solid_reinjection_args* args);

extern LOCAL_SYM res_T
solid_reinjection_3d
  (struct sdis_scene* scn,
   const unsigned solid_enc_id,
   struct solid_reinjection_args* args);

/*******************************************************************************
 * Handle net flux
 ******************************************************************************/
struct handle_net_flux_args {
  struct sdis_interface* interf;
  const struct sdis_interface_fragment* frag;
  struct green_path_handle* green_path;

  size_t picard_order;
  double h_cond; /* Convective coefficient, i.e. lambda/delta */
  double h_conv; /* Condutive coefficient */
  double h_radi; /* Radiative coefficient */
};
#define HANDLE_NET_FLUX_ARGS_NULL__ {NULL,NULL,NULL,0,0,0,0}
static const struct handle_net_flux_args HANDLE_NET_FLUX_ARGS_NULL =
  HANDLE_NET_FLUX_ARGS_NULL__;

extern LOCAL_SYM res_T
handle_net_flux_2d
  (const struct sdis_scene* scn,
   const struct handle_net_flux_args* args,
   struct temperature* T);

extern LOCAL_SYM res_T
handle_net_flux_3d
  (const struct sdis_scene* scn,
   const struct handle_net_flux_args* args,
   struct temperature* T);

/*******************************************************************************
 * Handle external flux
 ******************************************************************************/
struct handle_external_net_flux_args {
  struct sdis_interface* interf;
  const struct sdis_interface_fragment* frag;
  const struct s2d_hit* hit_2d;
  const struct s3d_hit* hit_3d;

  struct green_path_handle* green_path; /* Store the propagator */
  struct sdis_heat_path* heat_path; /* Save paths */

  size_t picard_order;
  double h_cond; /* Convective coefficient, i.e. lambda/delta */
  double h_conv; /* Condutive coefficient */
  double h_radi; /* Radiative coefficient */
};

struct handle_external_net_flux_args_3d {
  struct sdis_interface* interf;
  const struct sdis_interface_fragment* frag;
  const struct s3d_hit* hit;

  struct green_path_handle* green_path; /* Store the propagator */
  struct sdis_heat_path* heat_path; /* Save paths */

  size_t picard_order;
  double h_cond; /* Convective coefficient, i.e. lambda/delta */
  double h_conv; /* Condutive coefficient */
  double h_radi; /* Radiative coefficient */
};

#define HANDLE_EXTERNAL_NET_FLUX_ARGS_NULL__ {NULL,NULL,NULL,NULL,NULL,NULL,0,0,0,0}
static const struct handle_external_net_flux_args
HANDLE_EXTERNAL_NET_FLUX_ARGS_NULL = HANDLE_EXTERNAL_NET_FLUX_ARGS_NULL__;

extern LOCAL_SYM res_T
handle_external_net_flux_2d
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct handle_external_net_flux_args* args,
   struct temperature* T);

extern LOCAL_SYM res_T
handle_external_net_flux_3d
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct handle_external_net_flux_args* args,
   struct temperature* T);

/*******************************************************************************
 * Miscellaneous functions
 ******************************************************************************/
extern LOCAL_SYM res_T
check_Tref_2d
  (const struct sdis_scene* scn,
   const double pos[2],
   const double Tref,
   const char* call_func_name);

extern LOCAL_SYM res_T
check_Tref_3d
  (const struct sdis_scene* scn,
   const double pos[3],
   const double Tref,
   const char* call_func_name);

/* Query medium temperature from the boundary. This medium can be different
 * from the medium of the enclosure. Hence this function, which queries the
 * medium on the path coming from a boundary. If the temperature is known, T is
 * set to done. */
extern LOCAL_SYM res_T
query_medium_temperature_from_boundary_2d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct temperature* T);

extern LOCAL_SYM res_T
query_medium_temperature_from_boundary_3d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct temperature* T);

/* Move the submitted position away from the primitive boundaries to avoid
 * numerical issues leading to inconsistent random walks. */
extern LOCAL_SYM void
move_away_primitive_boundaries_2d
  (const struct s2d_hit* hit,
   const double delta,
   double position[2]); /* Position to move */

extern LOCAL_SYM void
move_away_primitive_boundaries_3d
  (const struct s3d_hit* hit,
   const double delta,
   double position[3]); /* Position to move */

/*******************************************************************************
 * Boundary sub-paths
 ******************************************************************************/
extern LOCAL_SYM res_T
solid_boundary_with_flux_path_2d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   const double phi,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
solid_boundary_with_flux_path_3d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   const double phi,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
solid_fluid_boundary_picard1_path_2d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
solid_fluid_boundary_picard1_path_3d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
solid_fluid_boundary_picardN_path_2d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
solid_fluid_boundary_picardN_path_3d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
solid_solid_boundary_path_2d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
solid_solid_boundary_path_3d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   const struct sdis_interface_fragment* frag,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

#endif /* SDIS_HEAT_PATH_BOUNDARY_C_H */
