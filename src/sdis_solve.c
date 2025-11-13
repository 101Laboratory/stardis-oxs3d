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

#include "sdis.h"

/* Generate the probe solvers */
#define SDIS_XD_DIMENSION 2
#include "sdis_solve_probe_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_solve_probe_Xd.h"

/* Generate the probe boundary solvers */
#define SDIS_XD_DIMENSION 2
#include "sdis_solve_probe_boundary_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_solve_probe_boundary_Xd.h"

/* Generate the boundary solvers */
#define SDIS_XD_DIMENSION 2
#include "sdis_solve_boundary_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_solve_boundary_Xd.h"

/* Generate the medium solvers */
#define SDIS_XD_DIMENSION 2
#include "sdis_solve_medium_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_solve_medium_Xd.h"

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_solve_probe
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_args* args,
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_2d(scn, args, NULL, out_estimator);
  } else {
    return solve_probe_3d(scn, args, NULL, out_estimator);
  }
}

res_T
sdis_solve_probe_list
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_list_args* args,
   struct sdis_estimator_buffer** out_buf)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_list_2d(scn, args, out_buf);
  } else {
    return solve_probe_list_3d(scn, args, out_buf);
  }
}

res_T
sdis_solve_probe_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_args* args,
   struct sdis_green_function** out_green)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_2d(scn, args, out_green, NULL);
  } else {
    return solve_probe_3d(scn, args, out_green, NULL);
  }
}

res_T
sdis_solve_probe_boundary
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_args* args,
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_boundary_2d(scn, args, NULL, out_estimator);
  } else {
    return solve_probe_boundary_3d(scn, args, NULL, out_estimator);
  }
}

res_T
sdis_solve_probe_boundary_list
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_list_args* args,
   struct sdis_estimator_buffer** out_buf)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_boundary_list_2d(scn, args, out_buf);
  } else {
    return solve_probe_boundary_list_3d(scn, args, out_buf);
  }
}

res_T
sdis_solve_probe_boundary_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_args* args,
   struct sdis_green_function** green)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_boundary_2d(scn, args, green, NULL);
  } else {
    return solve_probe_boundary_3d(scn, args, green, NULL);
  }
}

res_T
sdis_solve_boundary
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_args* args,
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_boundary_2d(scn, args, NULL, out_estimator);
  } else {
    return solve_boundary_3d(scn, args, NULL, out_estimator);
  }
}

res_T
sdis_solve_boundary_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_args* args,
   struct sdis_green_function** green)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_boundary_2d(scn, args, green, NULL);
  } else {
    return solve_boundary_3d(scn, args, green, NULL);
  }
}

res_T
sdis_solve_probe_boundary_flux
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_flux_args* args,
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_boundary_flux_2d(scn, args, out_estimator);
  } else {
    return solve_probe_boundary_flux_3d(scn, args, out_estimator);
  }
}

res_T
sdis_solve_boundary_flux
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_flux_args* args,
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_boundary_flux_2d(scn, args, out_estimator);
  } else {
    return solve_boundary_flux_3d(scn, args, out_estimator);
  }
}

res_T
sdis_solve_medium
  (struct sdis_scene* scn,
   const struct sdis_solve_medium_args* args,
   struct sdis_estimator** estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_medium_2d(scn, args, NULL, estimator);
  } else {
    return solve_medium_3d(scn, args, NULL, estimator);
  }
}

res_T
sdis_solve_medium_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_medium_args* args,
   struct sdis_green_function** green)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_medium_2d(scn, args, green, NULL);
  } else {
    return solve_medium_3d(scn, args, green, NULL);
  }
}

res_T
sdis_compute_power
  (struct sdis_scene* scn,
   const struct sdis_compute_power_args* args,
   struct sdis_estimator** estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return compute_power_2d(scn, args, estimator);
  } else {
    return compute_power_3d(scn, args, estimator);
  }
}
