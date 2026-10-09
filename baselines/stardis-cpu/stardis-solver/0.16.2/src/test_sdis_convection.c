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

#include <rsys/double3.h>
#include <rsys_math.h>

/*
 * The scene is composed of an unit fluid cube/square whose temperature is
 * unknown. The convection coefficient with the surrounding solid is H
 * everywhere the temperature of the -/+X, -/+Y and -/+Z faces are fixed to T0
 * and T1, T2, T3, T4 and T5, respectively.  This test computes the temperature
 * of the fluid Tf at an observation time t. This temperature is equal to:
 *
 *    Tf(t) = Tf(0) * e^(-nu*t) + Tinf*(1-e^(-nu*t))
 *
 *    nu = (Sum_{i=0..5}(H*Si)) / (RHO*CP*V)
 *    Tinf = (Sum_{i=0..5}(H*Si*Ti) / (Sum_{i=0..5}(H*Si));
 *
 * with Si surface of the faces (i.e. one), V the volume of the cube (i.e.
 * one), RHO the volumic mass of the fluid and CP its calorific capacity.
 *
 *           3D                  2D
 *
 *             (1,1,1)             (1,1)
 *       +---------+           +-----T3----+
 *      /'  T3    /|T4         |           |
 *     +---------+ |           |   H _\    |
 *     | ' H _\  |T1          T0    / /    T1
 *     |T0  / /  | |           |    \__/   |
 *     | +..\__/.|.+           |           |
 *   T5|,  T2    |/            +-----------+
 *     +---------+           (0,0)
 * (0,0,0)
 */

#define N 100000 /* #realisations */

#define Tf_0 280.0

#define T0 300.0
#define T1 310.0
#define T2 320.0
#define T3 330.0
#define T4 340.0
#define T5 350.0

#define H 10.0
#define RHO 25.0
#define CP 2.0

/*******************************************************************************
 * Media
 ******************************************************************************/
static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* is_stationary)
{
  CHK(vtx != NULL);
  if(*((int*)sdis_data_cget(is_stationary))) {
    return SDIS_TEMPERATURE_NONE;
  } else {
    return vtx->time <= 0 ? Tf_0 : SDIS_TEMPERATURE_NONE;
  }
}

static double
fluid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* is_stationary)
{
  (void)is_stationary;
  CHK(vtx != NULL);
  return RHO;
}

static double
fluid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* is_stationary)
{
  (void)is_stationary;
  CHK(vtx != NULL);
  return CP;
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
struct interf {
  double temperature;
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
  CHK(frag && data);
  return H;
}

static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  (void)source_id;
  CHK(frag && data);
  return 0;
}

static double
interface_get_specular_fraction
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  (void)source_id;
  CHK(frag && data);
  return 0;
}

static struct sdis_interface*
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const struct sdis_interface_shader* interf_shader,
   const double temperature)
{
  struct sdis_data* data;
  struct sdis_interface* interf;
  struct interf* interf_props;

  OK(sdis_data_create
    (dev, sizeof(struct interf), ALIGNOF(struct interf), NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = temperature;
  OK(sdis_interface_create
    (dev, front, back, interf_shader, data, &interf));
  OK(sdis_data_ref_put(data));
  return interf;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc mc_time = SDIS_MC_NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_data* is_stationary = NULL;
  struct sdis_interface* interf_T0 = NULL;
  struct sdis_interface* interf_T1 = NULL;
  struct sdis_interface* interf_T2 = NULL;
  struct sdis_interface* interf_T3 = NULL;
  struct sdis_interface* interf_T4 = NULL;
  struct sdis_interface* interf_T5 = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_scene* square_scn = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_estimator* estimator2 = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interf_shader = DUMMY_INTERFACE_SHADER;
  struct sdis_interface* box_interfaces[12/*#triangles*/];
  struct sdis_interface* square_interfaces[4/*#segments*/];
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  double ref;
  double Tinf;
  double nu;
  size_t nreals;
  size_t nfails;
  int i;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  /* Create the fluid medium */
  OK(sdis_data_create(dev, sizeof(int), ALIGNOF(int), NULL, &is_stationary));
  *((int*)sdis_data_get(is_stationary)) = 0;
  fluid_shader.temperature = fluid_get_temperature;
  fluid_shader.calorific_capacity = fluid_get_calorific_capacity;
  fluid_shader.volumic_mass = fluid_get_volumic_mass;
  OK(sdis_fluid_create(dev, &fluid_shader, is_stationary, &fluid));

  /* Create the solid_medium */
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;
  interf_shader.front.emissivity = interface_get_emissivity;
  interf_shader.front.specular_fraction = interface_get_specular_fraction;
  interf_shader.convection_coef_upper_bound = H;

  /* Create the interfaces */
  interf_T0 = create_interface(dev, fluid, solid, &interf_shader, T0);
  interf_T1 = create_interface(dev, fluid, solid, &interf_shader, T1);
  interf_T2 = create_interface(dev, fluid, solid, &interf_shader, T2);
  interf_T3 = create_interface(dev, fluid, solid, &interf_shader, T3);
  interf_T4 = create_interface(dev, fluid, solid, &interf_shader, T4);
  interf_T5 = create_interface(dev, fluid, solid, &interf_shader, T5);

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  /* Map the interfaces to their box triangles */
  box_interfaces[0] = box_interfaces[1] = interf_T5; /* Front */
  box_interfaces[2] = box_interfaces[3] = interf_T0; /* Left */
  box_interfaces[4] = box_interfaces[5] = interf_T4; /* Back */
  box_interfaces[6] = box_interfaces[7] = interf_T1; /* Right */
  box_interfaces[8] = box_interfaces[9] = interf_T3; /* Top */
  box_interfaces[10]= box_interfaces[11]= interf_T2; /* Bottom */

  /* Map the interfaces to their square segments */
  square_interfaces[0] = interf_T2; /* Bottom */
  square_interfaces[1] = interf_T0; /* Left */
  square_interfaces[2] = interf_T3; /* Top */
  square_interfaces[3] = interf_T1; /* Right */

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
  OK(sdis_interface_ref_put(interf_T0));
  OK(sdis_interface_ref_put(interf_T1));
  OK(sdis_interface_ref_put(interf_T2));
  OK(sdis_interface_ref_put(interf_T3));
  OK(sdis_interface_ref_put(interf_T4));
  OK(sdis_interface_ref_put(interf_T5));

  solve_args.nrealisations = N;
  solve_args.position[0] = 0.25;
  solve_args.position[1] = 0.25;
  solve_args.position[2] = 0.25;

  /* Test in 3D for various time values. */
  nu = (6 * H) / (RHO*CP);
  Tinf = (H*(T0 + T1 + T2 + T3 + T4 + T5)) / (6 * H);
  printf(">>> Temperature of the box at (%g %g %g)\n\n",
    SPLIT3(solve_args.position));
  FOR_EACH(i, 0, 5) {
    double time = i ? (double)i / nu : INF;
    ref = Tf_0 * exp(-nu * time) + Tinf * (1 - exp(-nu * time));

    solve_args.time_range[0] = time;
    solve_args.time_range[1] = time;

    /* Setup stationary state */
    *((int*)sdis_data_get(is_stationary)) = IS_INF(time);

    /* Solve in 3D */
    OK(sdis_solve_probe(box_scn, &solve_args, &estimator));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &mc_time));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    CHK(nfails + nreals == N);
    printf("Temperature at %g = %g ~ %g +/- %g\n", time, ref, T.E, T.SE);
    printf("Time per realisation (in usec) = %g +/- %g\n", mc_time.E, mc_time.SE);
    if(nfails)
      printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    CHK(eq_eps(T.E, ref, T.SE * 3));

    OK(sdis_solve_probe_green_function(box_scn, &solve_args, &green));
    OK(sdis_green_function_solve(green, &estimator2));
    check_green_function(green);
    check_estimator_eq(estimator, estimator2);
    check_green_serialization(green, box_scn);
    OK(sdis_estimator_ref_put(estimator2));
    OK(sdis_green_function_ref_put(green));

    OK(sdis_estimator_ref_put(estimator));
    printf("\n");
  }

  /* Test in 2D for various time values. */
  nu = (4 * H) / (RHO*CP);
  Tinf = (H * (T0 + T1 + T2 + T3)) / (4 * H);
  printf(">>> Temperature of the square at (%g %g)\n\n",
    SPLIT2(solve_args.position));
  FOR_EACH(i, 0, 5) {
    double time = i ? (double)i / nu : INF;
    ref = Tf_0 * exp(-nu * time) + Tinf * (1 - exp(-nu * time));

    solve_args.time_range[0] = time;
    solve_args.time_range[1] = time;

    /* Setup stationnary state */
    *((int*)sdis_data_get(is_stationary)) = IS_INF(time);

    /* Solve in 2D */
    OK(sdis_solve_probe(square_scn, &solve_args, &estimator));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    CHK(nfails + nreals == N);
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &mc_time));
    printf("Temperature at %g = %g ~ %g +/- %g\n", time, ref, T.E, T.SE);
    printf("Time per realisation (in usec) = %g +/- %g\n", mc_time.E, mc_time.SE);
    if(nfails)
      printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    CHK(eq_eps(T.E, ref, T.SE * 3));

    OK(sdis_solve_probe_green_function(square_scn, &solve_args, &green));
    OK(sdis_green_function_solve(green, &estimator2));
    check_green_function(green);
    check_estimator_eq(estimator, estimator2);
    check_green_serialization(green, square_scn);
    OK(sdis_estimator_ref_put(estimator2));
    OK(sdis_green_function_ref_put(green));

    OK(sdis_estimator_ref_put(estimator));
    printf("\n");
  }

  OK(sdis_scene_ref_put(box_scn));
  OK(sdis_scene_ref_put(square_scn));
  OK(sdis_device_ref_put(dev));
  OK(sdis_data_ref_put(is_stationary));

  CHK(mem_allocated_size() == 0);
  return 0;
}

