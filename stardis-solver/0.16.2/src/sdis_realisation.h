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

#ifndef SDIS_REALISATION_H
#define SDIS_REALISATION_H

#include "sdis.h"
#include "sdis_estimator_c.h"

#include <rsys/rsys.h>

/* Forward declarations */
struct bound_flux_result;
struct green_path_handle;
struct rwalk;
struct sdis_heat_path;
struct sdis_scene;
struct ssp_rng;
struct temperature;

enum flux_flag {
  FLUX_FLAG_CONVECTIVE = BIT(FLUX_CONVECTIVE),
  FLUX_FLAG_RADIATIVE = BIT(FLUX_RADIATIVE),
  FLUX_FLAGS_ALL = FLUX_FLAG_CONVECTIVE | FLUX_FLAG_RADIATIVE
};

/*******************************************************************************
 * Helper function used to sample a coupled path
 ******************************************************************************/
extern LOCAL_SYM res_T
sample_coupled_path_2d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

extern LOCAL_SYM res_T
sample_coupled_path_3d
  (struct sdis_scene* scn,
   struct rwalk_context* ctx,
   struct rwalk* rwalk,
   struct ssp_rng* rng,
   struct temperature* T);

/*******************************************************************************
 * Realisation at a given position and time IN a medium
 ******************************************************************************/
struct probe_realisation_args {
  struct ssp_rng* rng;
  unsigned enc_id; /* Enclosure into which the realisation starts */
  double position[3]; /* Probe position */
  double time; /* Observation time */
  size_t picard_order; /* Picard order to estimate radiative temperature */
  struct green_path_handle* green_path; /* May be NULL */
  struct sdis_heat_path* heat_path; /* May be NULL */
  size_t irealisation; /* Id of the realisation (for debug) */
  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */
};
#define PROBE_REALISATION_ARGS_NULL__ {                                        \
  NULL, /* RNG */                                                              \
  ENCLOSURE_ID_NULL, /* Enclosure */                                           \
  {0,0,0}, /* Position */                                                      \
  -1, /* Observation time */                                                   \
  0, /* Picard order */                                                        \
  NULL, /* Green path */                                                       \
  NULL, /* Heat path */                                                        \
  SIZE_MAX, /* Realisation ID */                                               \
  SDIS_DIFFUSION_NONE /* Diffusion algorithm */                                \
}
static const struct probe_realisation_args PROBE_REALISATION_ARGS_NULL =
  PROBE_REALISATION_ARGS_NULL__;

extern LOCAL_SYM res_T
probe_realisation_2d
  (struct sdis_scene* scn,
   struct probe_realisation_args* args,
   double* weight);

extern LOCAL_SYM res_T
probe_realisation_3d
  (struct sdis_scene* scn,
   struct probe_realisation_args* args,
   double* weight);

/*******************************************************************************
 * Realisation at a given position and time ON a given side of a boundary
 ******************************************************************************/
struct boundary_realisation_args {
  struct ssp_rng* rng;
  size_t iprim; /* Index of the geometruc primitive */
  double uv[2]; /* Parametric coordinate into the geometric primitive */
  double time; /* Observation time */
  size_t picard_order; /* Picard order to estimate radiative temperature */
  enum sdis_side side; /* Side of the geometric primitive */
  struct green_path_handle* green_path; /* May be NULL */
  struct sdis_heat_path* heat_path; /* May be NULL */
  size_t irealisation; /* Id of the realisation (for debug) */
  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */
};
#define BOUNDARY_REALISATION_ARGS_NULL__ {                                     \
  NULL, /* RNG */                                                              \
  SIZE_MAX,  /* Primitive ID */                                                \
  {0,0}, /* Parametric coordinates */                                          \
  -1, /* Observation time */                                                   \
  0, /* Picard order */                                                        \
  SDIS_SIDE_NULL__, /* Interface side */                                       \
  NULL, /* Green path */                                                       \
  NULL, /* Heat path */                                                        \
  SIZE_MAX, /* Realisation ID */                                               \
  SDIS_DIFFUSION_NONE /* Diffusion algorithm */                                \
}
static const struct boundary_realisation_args BOUNDARY_REALISATION_ARGS_NULL =
  BOUNDARY_REALISATION_ARGS_NULL__;

extern LOCAL_SYM res_T
boundary_realisation_2d
  (struct sdis_scene* scn,
   struct boundary_realisation_args* args,
   double* weight);

extern LOCAL_SYM res_T
boundary_realisation_3d
  (struct sdis_scene* scn,
   struct boundary_realisation_args* args,
   double* weight);

/*******************************************************************************
 * Realisation at a given position and time ON a given side of a boundary
 ******************************************************************************/
struct boundary_flux_realisation_args {
  struct ssp_rng* rng;
  size_t iprim; /* Index of the geometruc primitive */
  double uv[2]; /* Parametric coordinate into the geometric primitive */
  double time; /* Observation time */
  size_t picard_order; /* Picard order to estimate radiative temperature */
  enum sdis_side solid_side; /* Side of the geometric primitive */
  int flux_mask; /* Combination of enum flux_flag */
  size_t irealisation; /* Id of the realisation (for debug) */
  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */
};
#define BOUNDARY_FLUX_REALISATION_ARGS_NULL__ {                                \
  NULL, /* RNG */                                                              \
  SIZE_MAX, /* Primitive ID */                                                 \
  {0,0}, /* Parametric coordinates */                                          \
  -1, /* Observation time */                                                   \
  0, /* Picard order */                                                        \
  SDIS_SIDE_NULL__, /* Interface side */                                       \
  0, /* Flux mask */                                                           \
  SIZE_MAX, /* Realisation ID */                                               \
  SDIS_DIFFUSION_NONE /* Diffusion algorithm */                                \
}
static const struct boundary_flux_realisation_args
BOUNDARY_FLUX_REALISATION_ARGS_NULL = BOUNDARY_FLUX_REALISATION_ARGS_NULL__;

extern LOCAL_SYM res_T
boundary_flux_realisation_2d
  (struct sdis_scene* scn,
   struct boundary_flux_realisation_args* args,
   struct bound_flux_result* result);

extern LOCAL_SYM res_T
boundary_flux_realisation_3d
  (struct sdis_scene* scn,
   struct boundary_flux_realisation_args* args,
   struct bound_flux_result* result);

/*******************************************************************************
 * Realisation along a given ray at a given time. Available only in 3D
 ******************************************************************************/
struct ray_realisation_args {
  struct ssp_rng* rng;
  unsigned enc_id; /* Enclosure into which the realisation starts */
  double position[3]; /* Ray position */
  double direction[3]; /* Ray direction */
  double time; /* Observation time */
  size_t picard_order; /* Picard order to estimate radiative temperature */
  struct sdis_heat_path* heat_path; /* May be NULL */
  size_t irealisation; /* Id of the realisation (for debug) */
  enum sdis_diffusion_algorithm diff_algo; /* Diffusion algorithm to be used */
};
#define RAY_REALISATION_ARGS_NULL__ {                                          \
  NULL, /* RNG */                                                              \
  ENCLOSURE_ID_NULL, /* Enclosure */                                           \
  {0,0,0}, /* Position */                                                      \
  {0,0,0}, /* Direction */                                                     \
  -1, /* Observation time */                                                   \
  0, /* Picard order */                                                        \
  NULL, /* Heat path */                                                        \
  SIZE_MAX, /* Realisation ID */                                               \
  SDIS_DIFFUSION_NONE /* Diffusion algorithm */                                \
}
static const struct ray_realisation_args RAY_REALISATION_ARGS_NULL =
  RAY_REALISATION_ARGS_NULL__;

extern LOCAL_SYM res_T
ray_realisation_3d
  (struct sdis_scene* scn,
   struct ray_realisation_args* args,
   double* weight);

#endif /* SDIS_REALISATION_H */
