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
#include "test_sdis_utils.h"

#include <rsys_math.h>

/*
 * The scene is composed of a solid cube/square whose temperature is unknown.
 * The convection coefficient with the surrounding fluid is null exepted for
 * the +X face whose value is 'H'. The Temperature of the -X face is fixed to
 * Tb. This test computes the temperature on the +X face and check that it is
 * equal to:
 *
 *    T = (H*Tf + LAMBDA/A * Tb) / (H+LAMBDA/A)
 *
 * with Tf the temperature of the surrounding fluid, lambda the conductivity of
 * the cube and A the size of the cube/square, i.e. 1.
 *
 *          3D                        2D
 *
 *       ///// (1,1,1)             ///// (1,1)
 *       +-------+                 +-------+
 *      /'      /|    _\           |       |    _\
 *     +-------+ |   / /  Tf      Tb       |   / /   Tf
 *    Tb +.....|.+   \__/          |       |   \__/
 *     |,      |/                  +-------+
 *     +-------+                 (0,0) /////
 * (0,0,0) /////
 */

#define N 10000 /* #realisations */
#define N_dump 10 /* #dumped paths */

#define Tf 310.0
#define Tb 300.0
#define H 0.5
#define LAMBDA 0.1

/*******************************************************************************
 * Media
 ******************************************************************************/
struct fluid {
  double temperature;
};

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct fluid*)sdis_data_cget(data))->temperature;
}

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return 2.0;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return LAMBDA;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return 25.0;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return 1.0/20.0;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  if(vtx->time > 0) return SDIS_TEMPERATURE_NONE;
  return Tf;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double temperature;
  double hc;
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && data);
  return interf->temperature;
}

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && data);
  return interf->hc;
}

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static void
check_estimator
  (const struct sdis_estimator* estimator,
   const size_t nrealisations, /* #realisations */
   const double ref)
{
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  size_t nreals;
  size_t nfails;
  CHK(estimator && nrealisations);

  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_time(estimator, &time));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  printf("%g ~ %g +/- %g\n", ref, T.E, T.SE);
  printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
  printf("#failures = %lu/%lu\n\n",
    (unsigned long)nfails, (unsigned long)nrealisations);
  CHK(nfails + nreals == nrealisations);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, ref, 3*T.SE));
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  FILE* fp = NULL;
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_interface* interf_adiabatic = NULL;
  struct sdis_interface* interf_Tb = NULL;
  struct sdis_interface* interf_H = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_scene* square_scn = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_estimator* estimator2 = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* box_interfaces[12 /*#triangles*/];
  struct sdis_interface* square_interfaces[4/*#segments*/];
  struct sdis_solve_probe_boundary_args probe_args =
    SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT;
  struct sdis_solve_boundary_args bound_args = SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT;
  struct ssp_rng* rng = NULL;
  struct interf* interf_props = NULL;
  struct fluid* fluid_param;
  double pos[3];
  double ref;
  size_t prims[4];
  enum sdis_side sides[4];
  int is_master_process = 0;
  (void)argc, (void)argv;

  create_default_device(&argc, &argv, &is_master_process, &dev);

  /* Temporary file used to dump heat paths */
  CHK((fp = tmpfile()) != NULL);

  /* Create the fluid medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tf;
  fluid_shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid));
  OK(sdis_data_ref_put(data));

  /* Create the solid medium */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;
  interf_shader.front.emissivity = NULL;
  interf_shader.front.specular_fraction = NULL;
  interf_shader.back = SDIS_INTERFACE_SIDE_SHADER_NULL;

  /* Create the adiabatic interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->hc = 0;
  interf_props->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_adiabatic));
  OK(sdis_data_ref_put(data));

  /* Create the Tb interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->hc = 0;
  interf_props->temperature = Tb;
  OK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_Tb));
  OK(sdis_data_ref_put(data));

  /* Create the H interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->hc = H;
  interf_props->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_H));
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  /* Map the interfaces to their box triangles */
  box_interfaces[0] = box_interfaces[1] = interf_adiabatic; /* Front */
  box_interfaces[2] = box_interfaces[3] = interf_Tb;        /* Left */
  box_interfaces[4] = box_interfaces[5] = interf_adiabatic; /* Back */
  box_interfaces[6] = box_interfaces[7] = interf_H;         /* Right */
  box_interfaces[8] = box_interfaces[9] = interf_adiabatic; /* Top */
  box_interfaces[10]= box_interfaces[11]= interf_adiabatic; /* Bottom */

  /* Map the interfaces to their square segments */
  square_interfaces[0] = interf_adiabatic; /* Bottom */
  square_interfaces[1] = interf_Tb; /* Lef */
  square_interfaces[2] = interf_adiabatic; /* Top */
  square_interfaces[3] = interf_H; /* Right */

  /* Create the box scene */
  scn_args.get_indices = box_get_indices;
  scn_args.get_interface = box_get_interface;
  scn_args.get_position = box_get_position;
  scn_args.nprimitives = box_ntriangles;
  scn_args.nvertices = box_nvertices;
  scn_args.context = box_interfaces;
  OK(sdis_scene_create(dev, &scn_args, &box_scn));

  /* Create the square scene */
  scn_args.get_indices = square_get_indices;
  scn_args.get_interface = square_get_interface;
  scn_args.get_position = square_get_position;
  scn_args.nprimitives = square_nsegments;
  scn_args.nvertices = square_nvertices;
  scn_args.context = square_interfaces;
  OK(sdis_scene_2d_create(dev, &scn_args, &square_scn));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_adiabatic));
  OK(sdis_interface_ref_put(interf_Tb));
  OK(sdis_interface_ref_put(interf_H));

  ref = (H*Tf + LAMBDA * Tb) / (H + LAMBDA);

  #define SOLVE sdis_solve_probe_boundary
  #define GREEN sdis_solve_probe_boundary_green_function

  probe_args.nrealisations = N;
  probe_args.uv[0] = 0.3;
  probe_args.uv[1] = 0.3;
  probe_args.iprim = 6;
  probe_args.time_range[0] = INF;
  probe_args.time_range[1] = INF;
  probe_args.side = SDIS_FRONT;

  BA(SOLVE(NULL, &probe_args, &estimator));
  BA(SOLVE(box_scn, NULL, &estimator));
  BA(SOLVE(box_scn, &probe_args, NULL));
  probe_args.nrealisations = 0;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.nrealisations = N;
  probe_args.iprim = 12;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.iprim = 6;
  probe_args.side = SDIS_SIDE_NULL__;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.side = SDIS_FRONT;
  probe_args.time_range[0] = probe_args.time_range[1] = -1;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.time_range[0] = 1;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.time_range[1] = 0;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.time_range[0] = probe_args.time_range[1] = INF;
  probe_args.picard_order = 0;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.picard_order = 1;

  OK(SOLVE(box_scn, &probe_args, &estimator));
  OK(sdis_scene_get_boundary_position
    (box_scn, probe_args.iprim, probe_args.uv, pos));
  if(is_master_process) {
    printf("Boundary temperature of the box at (%g %g %g) = ", SPLIT3(pos));
    check_estimator(estimator, N, ref);
  }

  /* Check RNG type */
  probe_args.rng_state = NULL;
  probe_args.rng_type = SSP_RNG_TYPE_NULL;
  BA(SOLVE(box_scn, &probe_args, &estimator2));
  probe_args.rng_type =
    SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT.rng_type == SSP_RNG_THREEFRY
    ? SSP_RNG_MT19937_64 : SSP_RNG_THREEFRY;
  OK(SOLVE(box_scn, &probe_args, &estimator2));
  if(is_master_process) {
    struct sdis_mc T, T2;
    check_estimator(estimator2, N, ref);
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Check RNG state */
  OK(ssp_rng_create(NULL, SSP_RNG_THREEFRY, &rng));
  OK(ssp_rng_discard(rng, 31415926535)); /* Move the RNG state  */
  probe_args.rng_state = rng;
  probe_args.rng_type = SSP_RNG_TYPE_NULL;
  OK(SOLVE(box_scn, &probe_args, &estimator2));
  OK(ssp_rng_ref_put(rng));
  if(is_master_process) {
    struct sdis_mc T, T2;
    check_estimator(estimator2, N, ref);
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  probe_args.rng_state = SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT.rng_state;
  probe_args.rng_type = SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT.rng_type;

  BA(GREEN(NULL, &probe_args, &green));
  BA(GREEN(box_scn, NULL, &green));
  BA(GREEN(box_scn, &probe_args, NULL));
  probe_args.nrealisations = 0;
  BA(GREEN(box_scn, &probe_args, &green));
  probe_args.nrealisations = N;
  probe_args.iprim = 12;
  BA(GREEN(box_scn, &probe_args, &green));
  probe_args.iprim = 6;
  probe_args.side = SDIS_SIDE_NULL__;
  BA(GREEN(box_scn, &probe_args, &green));
  probe_args.side = SDIS_FRONT;
  OK(GREEN(box_scn, &probe_args, &green));

  if(!is_master_process) {
    CHK(estimator == NULL);
    CHK(green == NULL);
  } else {
    check_green_function(green);
    OK(sdis_green_function_solve(green, &estimator2));
    check_estimator(estimator2, N, ref);
    check_green_serialization(green, box_scn);

    OK(sdis_green_function_ref_put(green));
    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Dump paths */
  probe_args.nrealisations = N_dump;
  probe_args.register_paths = SDIS_HEAT_PATH_ALL;
  OK(SOLVE(box_scn, &probe_args, &estimator));
  if(is_master_process) {
    dump_heat_paths(fp, estimator);
    OK(sdis_estimator_ref_put(estimator));
  }

  /* The external fluid cannot have an unknown temperature */
  fluid_param->temperature = SDIS_TEMPERATURE_NONE;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  fluid_param->temperature = Tf;

  probe_args.nrealisations = N;
  probe_args.register_paths = SDIS_HEAT_PATH_NONE;
  probe_args.uv[0] = 0.5;
  probe_args.iprim = 4;

  BA(SOLVE(square_scn, &probe_args, &estimator));
  probe_args.iprim = 3;
  OK(SOLVE(square_scn, &probe_args, &estimator));

  OK(GREEN(square_scn, &probe_args, &green));
  if(is_master_process) {
    check_green_function(green);
    OK(sdis_green_function_solve(green, &estimator2));
    check_estimator(estimator2, N, ref);
    check_green_serialization(green, square_scn);

    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
    OK(sdis_green_function_ref_put(green));
  }

  /* The external fluid cannot have an unknown temperature */
  fluid_param->temperature = SDIS_TEMPERATURE_NONE;
  BA(SOLVE(square_scn, &probe_args, &estimator));
  fluid_param->temperature = Tf;

  /* Right-side temperature at initial time */
  probe_args.time_range[0] = 0;
  probe_args.time_range[1] = 0;

  probe_args.iprim = 6;
  OK(SOLVE(box_scn, &probe_args, &estimator));
  if(is_master_process) {
    check_estimator(estimator, N, Tf);
    OK(sdis_estimator_ref_put(estimator));
  }

  probe_args.iprim = 3;
  OK(SOLVE(square_scn, &probe_args, &estimator));
  if(is_master_process) {
    check_estimator(estimator, N, Tf);
    OK(sdis_estimator_ref_put(estimator));
  }

  #undef F
  #undef SOLVE
  #undef GREEN

  sides[0] = SDIS_FRONT;
  sides[1] = SDIS_FRONT;
  sides[2] = SDIS_FRONT;
  sides[3] = SDIS_FRONT;

  bound_args.nrealisations = N;
  bound_args.sides = sides;
  bound_args.primitives = prims;
  bound_args.nprimitives = 2;
  bound_args.time_range[0] = INF;
  bound_args.time_range[1] = INF;

  #define SOLVE sdis_solve_boundary
  #define GREEN sdis_solve_boundary_green_function
  prims[0] = 6;
  prims[1] = 7;

  BA(SOLVE(NULL, &bound_args, &estimator));
  BA(SOLVE(box_scn, NULL, &estimator));
  BA(SOLVE(box_scn, &bound_args, NULL));
  bound_args.nrealisations = 0;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.nrealisations = N;
  bound_args.primitives = NULL;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.primitives = prims;
  bound_args.sides = NULL;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.sides = sides;
  bound_args.nprimitives = 0;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.nprimitives = 2;
  prims[0] = 12;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  prims[0] = 6;
  sides[0] = SDIS_SIDE_NULL__;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  sides[0] = SDIS_FRONT;
  bound_args.time_range[0] = bound_args.time_range[1] = -1;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.time_range[0] = 1;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.time_range[1] = 0;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.time_range[0] = bound_args.time_range[1] = INF;
  bound_args.picard_order = 0;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.picard_order = 1;

  /* Average temperature on the right side of the box */
  OK(SOLVE(box_scn, &bound_args, &estimator));
  if(is_master_process) {
    printf("Average temperature of the right side of the box = ");
    check_estimator(estimator, N, ref);
  }

  /* Check RNG type */
  bound_args.rng_state = NULL;
  bound_args.rng_type = SSP_RNG_TYPE_NULL;
  BA(SOLVE(box_scn, &bound_args, &estimator2));
  bound_args.rng_type =
    SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT.rng_type == SSP_RNG_THREEFRY
    ? SSP_RNG_MT19937_64 : SSP_RNG_THREEFRY;
  OK(SOLVE(box_scn, &bound_args, &estimator2));
  if(is_master_process) {
    struct sdis_mc T, T2;
    check_estimator(estimator2, N, ref);
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Check RNG state */
  OK(ssp_rng_create(NULL, SSP_RNG_THREEFRY, &rng));
  OK(ssp_rng_discard(rng, 31415926535)); /* Move the RNG state  */
  bound_args.rng_state = rng;
  bound_args.rng_type = SSP_RNG_TYPE_NULL;
  OK(SOLVE(box_scn, &bound_args, &estimator2));
  OK(ssp_rng_ref_put(rng));
  if(is_master_process) {
    struct sdis_mc T, T2;
    check_estimator(estimator2, N, ref);
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Restore args */
  bound_args.rng_state = SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT.rng_state;
  bound_args.rng_type = SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT.rng_type;

  BA(GREEN(NULL, &bound_args, &green));
  BA(GREEN(box_scn, NULL, &green));
  BA(GREEN(box_scn, &bound_args, NULL));
  bound_args.nrealisations = 0;
  BA(GREEN(box_scn, &bound_args, &green));
  bound_args.nrealisations = N;
  bound_args.primitives = NULL;
  BA(GREEN(box_scn, &bound_args, &green));
  bound_args.primitives = prims;
  bound_args.sides = NULL;
  BA(GREEN(box_scn, &bound_args, &green));
  bound_args.sides = sides;
  bound_args.nprimitives = 0;
  BA(GREEN(box_scn, &bound_args, &green));
  bound_args.nprimitives = 2;
  prims[0] = 12;
  BA(GREEN(box_scn, &bound_args, &green));
  prims[0] = 6;
  sides[0] = SDIS_SIDE_NULL__;
  BA(GREEN(box_scn, &bound_args, &green));
  sides[0] = SDIS_FRONT;

  OK(GREEN(box_scn, &bound_args, &green));
  if(!is_master_process) {
    CHK(estimator == NULL);
    CHK(green == NULL);
  } else {
    check_green_function(green);
    OK(sdis_green_function_solve(green, &estimator2));
    check_estimator(estimator2, N, ref);
    check_green_serialization(green, box_scn);

    OK(sdis_green_function_ref_put(green));
    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Dump path */
  bound_args.nrealisations = N_dump;
  bound_args.register_paths = SDIS_HEAT_PATH_ALL;

  /* Check simulation error handling when paths are registered */
  fluid_param->temperature = SDIS_TEMPERATURE_NONE;
  BA(SOLVE(box_scn, &bound_args, &estimator));

  /* Dump path */
  fluid_param->temperature = Tf;
  OK(SOLVE(box_scn, &bound_args, &estimator));
  if(is_master_process) {
    dump_heat_paths(fp, estimator);
    OK(sdis_estimator_ref_put(estimator));
  }

  /* Switch in 2D */
  bound_args.nrealisations = N;
  bound_args.register_paths = SDIS_HEAT_PATH_NONE;
  bound_args.nprimitives = 1;
  prims[0] = 4;
  BA(SOLVE(square_scn, &bound_args, &estimator));

  /* Average temperature on the right side of the square */
  prims[0] = 3;
  OK(SOLVE(square_scn, &bound_args, &estimator));
  if(is_master_process) {
    printf("Average temperature of the right side of the square = ");
    check_estimator(estimator, N, ref);
  }

  OK(GREEN(square_scn, &bound_args, &green));
  if(is_master_process) {
    check_green_function(green);
    OK(sdis_green_function_solve(green, &estimator2));
    check_estimator(estimator2, N, ref);
    check_green_serialization(green, square_scn);

    OK(sdis_green_function_ref_put(green));
    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Dump path */
  bound_args.nrealisations = N_dump;
  bound_args.register_paths = SDIS_HEAT_PATH_ALL;
  OK(SOLVE(square_scn, &bound_args, &estimator));
  if(is_master_process) {
    dump_heat_paths(fp, estimator);
    OK(sdis_estimator_ref_put(estimator));
  }

  bound_args.register_paths = SDIS_HEAT_PATH_NONE;
  bound_args.nrealisations = N;

  /* Average temperature on the left+right sides of the box */
  prims[0] = 2;
  prims[1] = 3;
  prims[2] = 6;
  prims[3] = 7;

  ref = (ref + Tb) / 2;

  bound_args.nprimitives = 4;
  OK(SOLVE(box_scn, &bound_args, &estimator));
  if(is_master_process) {
    printf("Average temperature of the left+right sides of the box = ");
    check_estimator(estimator, N, ref);
  }

  OK(GREEN(box_scn, &bound_args, &green));
  if(is_master_process) {
    check_green_function(green);
    OK(sdis_green_function_solve(green, &estimator2));
    check_estimator(estimator2, N, ref);
    check_green_serialization(green, box_scn);

    OK(sdis_green_function_ref_put(green));
    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Average temperature on the left+right sides of the square */
  prims[0] = 1;
  prims[1] = 3;
  bound_args.nprimitives = 2;
  OK(SOLVE(square_scn, &bound_args, &estimator));
  if(is_master_process) {
    printf("Average temperature of the left+right sides of the square = ");
    check_estimator(estimator, N, ref);
  }

  OK(GREEN(square_scn, &bound_args, &green));
  if(is_master_process) {
    check_green_function(green);
    OK(sdis_green_function_solve(green, &estimator2));
    check_estimator(estimator2, N, ref);
    check_green_serialization(green, square_scn);

    OK(sdis_green_function_ref_put(green));
    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Right-side temperature at initial time */
  bound_args.time_range[0] = 0;
  bound_args.time_range[1] = 0;

  prims[0] = 6;
  prims[1] = 7;
  bound_args.nprimitives = 2;
  OK(SOLVE(box_scn, &bound_args, &estimator));
  if(is_master_process) {
    check_estimator(estimator, N, Tf);
    OK(sdis_estimator_ref_put(estimator));
  }

  prims[0] = 3;
  bound_args.nprimitives = 1;
  OK(SOLVE(square_scn, &bound_args, &estimator));
  if(is_master_process) {
    check_estimator(estimator, N, Tf);
    OK(sdis_estimator_ref_put(estimator));
  }

  #undef SOLVE
  #undef GREEN

  OK(sdis_scene_ref_put(box_scn));
  OK(sdis_scene_ref_put(square_scn));
  free_default_device(dev);

  CHK(fclose(fp) == 0);

  CHK(mem_allocated_size() == 0);
  return 0;
}

