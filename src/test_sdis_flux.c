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

#include <rsys/clock_time.h>
#include <rsys/double3.h>
#include <star/ssp.h>

/*
 * The scene is composed of a solid cube/square whose temperature is unknown.
 * The temperature is fixed at T0 on the +X face. The Flux of the -X face is
 * fixed to PHI. The flux on the other faces is null (i.e. adiabatic). This
 * test computes the temperature of a probe position pos into the solid and
 * check that at t=inf it is equal to:
 *
 *    T(pos) = T0 + (A-pos) * PHI/LAMBDA
 *
 * with LAMBDA the conductivity of the solid and A the size of cube/square.
 *
 *          3D                 2D
 *
 *       ///// (1,1,1)      ///// (1,1)
 *       +-------+          +-------+
 *      /'      /|          |       |
 *     +-------+ T0        PHI      T0
 *   PHI +.....|.+          |       |
 *     |,      |/           +-------+
 *     +-------+          (0,0) /////
 * (0,0,0) /////
 */

#define N 10000

#define PHI 10.0
#define T0 320.0
#define LAMBDA 0.1

/*******************************************************************************
 * Media
 ******************************************************************************/
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
  if(vtx->time > 0)
    return SDIS_TEMPERATURE_NONE;
  else
    return T0;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double temperature;
  double phi;
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
interface_get_flux
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && data);
  return interf->phi;
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
solve
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct interf* interf)
{
  char dump[128];
  struct time t0, t1, t2;
  struct sdis_estimator* estimator;
  struct sdis_estimator* estimator2;
  struct sdis_green_function* green;
  struct sdis_mc T;
  struct sdis_mc time;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  size_t nreals;
  size_t nfails;
  double ref = SDIS_TEMPERATURE_NONE;
  const int nsimuls = 4;
  int isimul;
  enum sdis_scene_dimension dim;
  ASSERT(scn && rng && interf);

  OK(sdis_scene_get_dimension(scn, &dim));

  FOR_EACH(isimul, 0, nsimuls) {
    int steady = (isimul % 2) == 0;

    /* Restore phi value */
    interf->phi = PHI;

    solve_args.position[0] = ssp_rng_uniform_double(rng, 0.1, 0.9);
    solve_args.position[1] = ssp_rng_uniform_double(rng, 0.1, 0.9);
    solve_args.position[2] =
      dim == SDIS_SCENE_2D ? 0 : ssp_rng_uniform_double(rng, 0.1, 0.9);

    solve_args.nrealisations = N;
    if(steady)
      solve_args.time_range[0] = solve_args.time_range[1] = INF;
    else {
      solve_args.time_range[0] = 100 * (double)isimul;
      solve_args.time_range[1] = 4 * solve_args.time_range[0];
    }

    time_current(&t0);
    OK(sdis_solve_probe(scn, &solve_args, &estimator));
    time_sub(&t0, time_current(&t1), &t0);
    time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));

    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    switch(dim) {
    case SDIS_SCENE_2D:
      if(steady) {
        ref = T0 + (1 - solve_args.position[0]) * interf->phi / LAMBDA;
        printf("Steady temperature at (%g, %g) with Phi=%g = %g ~ %g +/- %g\n",
          SPLIT2(solve_args.position), interf->phi, ref, T.E, T.SE);
      } else {
        printf("Mean temperature at (%g, %g) with t in [%g %g] and Phi=%g"
          " ~ %g +/- %g\n",
          SPLIT2(solve_args.position), SPLIT2(solve_args.time_range),
          interf->phi, T.E, T.SE);
      }
      break;
    case SDIS_SCENE_3D:
      if(steady) {
        ref = T0 + (1 - solve_args.position[0]) * interf->phi / LAMBDA;
        printf("Steady temperature at (%g, %g, %g) with Phi=%g = %g ~ %g +/- %g\n",
          SPLIT3(solve_args.position), interf->phi, ref, T.E, T.SE);
      } else {
        printf("Mean temperature at (%g, %g, %g) with t in [%g %g] and Phi=%g"
          " ~ %g +/- %g\n",
          SPLIT3(solve_args.position), SPLIT2(solve_args.time_range),
          interf->phi, T.E, T.SE);
      }
      break;
    default: FATAL("Unreachable code.\n"); break;
    }
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    printf("Elapsed time = %s\n", dump);
    printf("Time per realisation (in usec) = %g +/- %g\n\n", time.E, time.SE);

    CHK(nfails + nreals == N);
    CHK(nfails <= N/1000);
    if(steady) CHK(eq_eps(T.E, ref, T.SE * 3));

    time_current(&t0);
    OK(sdis_solve_probe_green_function(scn, &solve_args, &green));
    time_current(&t1);
    OK(sdis_green_function_solve(green, &estimator2));
    time_current(&t2);

    OK(sdis_estimator_get_realisation_count(estimator2, &nreals));
    OK(sdis_estimator_get_failure_count(estimator2, &nfails));
    OK(sdis_estimator_get_temperature(estimator2, &T));

    switch(dim) {
    case SDIS_SCENE_2D:
      if(steady) {
        ref = T0 + (1 - solve_args.position[0]) * interf->phi / LAMBDA;
        printf("Steady Green temperature at (%g, %g) with Phi=%g = %g ~ %g +/- %g\n",
          SPLIT2(solve_args.position), interf->phi, ref, T.E, T.SE);
      } else {
        printf("Mean Green temperature at (%g, %g) with t in [%g %g] and Phi=%g"
          " ~ %g +/- %g\n",
          SPLIT2(solve_args.position), SPLIT2(solve_args.time_range),
          interf->phi, T.E, T.SE);
      }
      break;
    case SDIS_SCENE_3D:
      if(steady) {
        ref = T0 + (1 - solve_args.position[0]) * interf->phi / LAMBDA;
        printf("Steady Green temperature at (%g, %g, %g) with Phi=%g = %g"
          " ~ %g +/- %g\n",
          SPLIT3(solve_args.position), interf->phi, ref, T.E, T.SE);
      } else {
        printf("Mean Green temperature at (%g, %g, %g) with t in [%g %g] and Phi=%g"
          " ~ %g +/- %g\n",
          SPLIT3(solve_args.position), SPLIT2(solve_args.time_range),
          interf->phi, T.E, T.SE);
      }
      break;
    default: FATAL("Unreachable code.\n"); break;
    }
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    time_sub(&t0, &t1, &t0);
    time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
    printf("Green estimation time = %s\n", dump);
    time_sub(&t1, &t2, &t1);
    time_dump(&t1, TIME_ALL, NULL, dump, sizeof(dump));
    printf("Green solve time = %s\n", dump);

    check_green_function(green);
    check_estimator_eq(estimator, estimator2);
    check_green_serialization(green, scn);

    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
    printf("\n");

    /* Check same green used at a different flux value */
    interf->phi = 3 * PHI;

    time_current(&t0);
    OK(sdis_solve_probe(scn, &solve_args, &estimator));
    time_sub(&t0, time_current(&t1), &t0);
    time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));

    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    switch(dim) {
    case SDIS_SCENE_2D:
      if(steady) {
        ref = T0 + (1 - solve_args.position[0]) * interf->phi / LAMBDA;
        printf("Steady temperature at (%g, %g) with Phi=%g = %g ~ %g +/- %g\n",
          SPLIT2(solve_args.position), interf->phi, ref, T.E, T.SE);
      } else {
        printf("Mean temperature at (%g, %g) with t in [%g %g] and Phi=%g"
          " ~ %g +/- %g\n",
          SPLIT2(solve_args.position), SPLIT2(solve_args.time_range),
          interf->phi, T.E, T.SE);
      }
      break;
    case SDIS_SCENE_3D:
      if(steady) {
        ref = T0 + (1 - solve_args.position[0]) * interf->phi / LAMBDA;
        printf("Steady temperature at (%g, %g, %g) with Phi=%g = %g"
          " ~ %g +/- %g\n",
          SPLIT3(solve_args.position), interf->phi, ref, T.E, T.SE);
      } else {
        printf("Mean temperature at (%g, %g, %g) with t in [%g %g] and Phi=%g"
          " ~ %g +/- %g\n",
          SPLIT3(solve_args.position), SPLIT2(solve_args.time_range),
          interf->phi, T.E, T.SE);
      }
      break;
    default: FATAL("Unreachable code.\n"); break;
    }
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    printf("Elapsed time = %s\n", dump);
    printf("Time per realisation (in usec) = %g +/- %g\n\n", time.E, time.SE);

    CHK(nfails + nreals == N);
    CHK(nfails <= N/1000);
    if(steady) CHK(eq_eps(T.E, ref, T.SE * 3));

    time_current(&t0);
    OK(sdis_green_function_solve(green, &estimator2));
    time_sub(&t0, time_current(&t1), &t0);
    time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
    printf("Green solve time = %s\n", dump);

    check_green_function(green);
    check_estimator_eq(estimator, estimator2);

    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
    OK(sdis_green_function_ref_put(green));

    printf("\n\n");
  }

  /* Picard N is not supported with a flux != 0 */
  solve_args.position[0] = 0.1;
  solve_args.position[1] = 0.1;
  solve_args.position[2] = dim == SDIS_SCENE_2D ? 0 : 0.1;
  solve_args.time_range[0] = INF;
  solve_args.time_range[1] = INF;
  solve_args.picard_order = 2;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_interface* interf_adiabatic = NULL;
  struct sdis_interface* interf_T0 = NULL;
  struct sdis_interface* interf_phi = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_scene* square_scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* box_interfaces[12 /*#triangles*/];
  struct sdis_interface* square_interfaces[4/*#segments*/];
  struct interf* interf_props = NULL;
  struct ssp_rng* rng = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  /* Create the dummy fluid medium */
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Create the solid_medium */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));

  /* Setup the interface shader */
  interf_shader.front.temperature = interface_get_temperature;
  interf_shader.front.flux = interface_get_flux;

  /* Create the adiabatic interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = SDIS_TEMPERATURE_NONE;
  interf_props->phi = 0;
  OK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_adiabatic));
  OK(sdis_data_ref_put(data));

  /* Create the T0 interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = T0;
  interf_props->phi = 0; /* Unused */
  OK(sdis_interface_create(dev, solid, fluid, &interf_shader, data, &interf_T0));
  OK(sdis_data_ref_put(data));

  /* Create the PHI interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = SDIS_TEMPERATURE_NONE;
  interf_props->phi = PHI;
  OK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_phi));
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  /* Map the interfaces to their box triangles */
  box_interfaces[0] = box_interfaces[1] = interf_adiabatic; /* Front */
  box_interfaces[2] = box_interfaces[3] = interf_phi;        /* Left */
  box_interfaces[4] = box_interfaces[5] = interf_adiabatic; /* Back */
  box_interfaces[6] = box_interfaces[7] = interf_T0;        /* Right */
  box_interfaces[8] = box_interfaces[9] = interf_adiabatic; /* Top */
  box_interfaces[10]= box_interfaces[11]= interf_adiabatic; /* Bottom */

  /* Map the interfaces to their square segments */
  square_interfaces[0] = interf_adiabatic; /* Bottom */
  square_interfaces[1] = interf_phi;       /* Left */
  square_interfaces[2] = interf_adiabatic; /* Top */
  square_interfaces[3] = interf_T0;        /* Right */

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
  OK(sdis_interface_ref_put(interf_T0));
  OK(sdis_interface_ref_put(interf_phi));

  /* Solve */
  OK(ssp_rng_create(NULL, SSP_RNG_KISS, &rng));
  printf(">> Box scene\n");
  solve(box_scn, rng, interf_props);
  printf(">> Square Scene\n");
  solve(square_scn, rng, interf_props);

  OK(sdis_scene_ref_put(box_scn));
  OK(sdis_scene_ref_put(square_scn));
  OK(sdis_device_ref_put(dev));
  OK(ssp_rng_ref_put(rng));

  CHK(mem_allocated_size() == 0);
  return 0;
}
