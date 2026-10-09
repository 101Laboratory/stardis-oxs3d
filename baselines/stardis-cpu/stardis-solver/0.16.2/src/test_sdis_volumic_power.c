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
#include <rsys_math.h>
#include <star/ssp.h>

/*
 * The scene is composed of a solid cube/square whose temperature is unknown.
 * The convection coefficient with the surrounding fluid is null everywhere The
 * Temperature of the +/- X faces are fixed to T0, and the solid has a volumic
 * power of P0. This test computes the temperature of a probe position pos into
 * the solid and check that at t=inf it is is equal to:
 *
 *    T(pos) = P0 / (2*LAMBDA) * (A^2/4 - (pos-0.5)^2) + T0
 *
 * with LAMBDA the conductivity of the solid and A the size of the cube/square,
 * i.e. 1.
 *
 *          3D                 2D
 *
 *       ///// (1,1,1)      ///// (1,1)
 *       +-------+          +-------+
 *      /'      /|          |       |
 *     +-------+ T0        T0       T0
 *    T0 +.....|.+          |       |
 *     |,      |/           +-------+
 *     +-------+          (0,0) /////
 * (0,0,0) /////
 */

#define N 10000 /* #realisations */

#define T0 320
#define LAMBDA 0.1
#define P0 10
#define DELTA 1.0/60.0

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static const char*
algo_cstr(const enum sdis_diffusion_algorithm diff_algo)
{
  const char* cstr = "none";

  switch(diff_algo) {
    case SDIS_DIFFUSION_DELTA_SPHERE: cstr = "delta sphere"; break;
    case SDIS_DIFFUSION_WOS: cstr = "WoS"; break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return cstr;
}

/*******************************************************************************
 * Media
 ******************************************************************************/
struct solid {
  double lambda;
  double rho;
  double cp;
  double delta;
  double vpower;
  double initial_temperature;
  double t0;
};

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return SDIS_TEMPERATURE_NONE;
}

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL);
  return ((struct solid*)sdis_data_cget(data))->cp;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL);
  return ((struct solid*)sdis_data_cget(data))->lambda;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL);
  return ((struct solid*)sdis_data_cget(data))->rho;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL);
  return ((struct solid*)sdis_data_cget(data))->delta;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  double t0;
  CHK(vtx != NULL);
  CHK(data != NULL);
  t0 = ((const struct solid*)sdis_data_cget(data))->t0;
  if(vtx->time > t0) {
    return SDIS_TEMPERATURE_NONE;
  } else {
    return ((const struct solid*)sdis_data_cget(data))->initial_temperature;
  }
}

static double
solid_get_volumic_power
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return ((struct solid*)sdis_data_cget(data))->vpower;
}

/*******************************************************************************
 * Interfaces
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
  return 0;
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
solve
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct solid* solid)
{
  char dump[128];
  struct time t0, t1, t2;
  struct sdis_estimator* estimator;
  struct sdis_estimator* estimator2;
  struct sdis_green_function* green;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  size_t nreals;
  size_t nfails;
  enum sdis_scene_dimension dim;
  const int nsimuls = 4;
  int isimul;
  ASSERT(scn && rng && solid);

  OK(sdis_scene_get_dimension(scn, &dim));
  FOR_EACH(isimul, 0, nsimuls) {
    const enum sdis_diffusion_algorithm algo = (isimul / 2) % 2
      ? SDIS_DIFFUSION_WOS
      : SDIS_DIFFUSION_DELTA_SPHERE;
    const int steady = (isimul % 2) == 0;
    double power = P0 == SDIS_VOLUMIC_POWER_NONE ? 0 : P0;

    /* Restore power value */
    solid->vpower = P0;

    solve_args.diff_algo = algo;
    solve_args.position[0] = ssp_rng_uniform_double(rng, 0.1, 0.9);
    solve_args.position[1] = ssp_rng_uniform_double(rng, 0.1, 0.9);
    solve_args.position[2] =
      dim == SDIS_SCENE_2D ? 0 : ssp_rng_uniform_double(rng, 0.1, 0.9);

    solve_args.nrealisations = N;
    if(steady) {
      solve_args.time_range[0] = solve_args.time_range[1] = INF;
    } else {
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

    if(steady) {
      const double x = solve_args.position[0] - 0.5;
      const double ref = power / (2 * LAMBDA) * (1.0 / 4.0 - x * x) + T0;
      printf
        ("Steady temperature - pos: %g, %g, %g; Power: %g algo: %s "
         "= %g ~ %g +/- %g\n",
        SPLIT3(solve_args.position), power, algo_cstr(algo), ref, T.E, T.SE);
      CHK(eq_eps(T.E, ref, T.SE * 3));
    } else {
      printf(
        "Mean temperature - pos: %g, %g, %g;  t in [%g %g]; power: %g; algo: %s "
        "~ %g +/- %g\n",
        SPLIT3(solve_args.position), SPLIT2(solve_args.time_range),
        power, algo_cstr(algo), T.E, T.SE);
    }

    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    printf("Elapsed time = %s\n", dump);
    printf("Time per realisation (in usec) = %g +/- %g\n\n", time.E, time.SE);

    CHK(nfails + nreals == N);
    CHK(nfails <= N/1000);

    /* Check green function */
    time_current(&t0);
    OK(sdis_solve_probe_green_function(scn, &solve_args, &green));
    time_current(&t1);
    OK(sdis_green_function_solve(green, &estimator2));
    time_current(&t2);

    OK(sdis_estimator_get_realisation_count(estimator2, &nreals));
    OK(sdis_estimator_get_failure_count(estimator2, &nfails));
    OK(sdis_estimator_get_temperature(estimator2, &T));

    if(steady) {
      const double x = solve_args.position[0] - 0.5;
      const double ref = power / (2 * LAMBDA) * (1.0 / 4.0 - x * x) + T0;
      printf
        ("Green steady temperature - pos: %g, %g, %g; Power: %g algo: %s"
         "= %g ~ %g +/- %g\n",
        SPLIT3(solve_args.position), power, algo_cstr(algo), ref, T.E, T.SE);
    } else {
      printf(
        "Green mean temperature - pos: %g, %g, %g; t: [%g %g]; power: %g; algo: %s "
        "~ %g +/- %g\n",
        SPLIT3(solve_args.position), SPLIT2(solve_args.time_range),
        power, algo_cstr(algo), T.E, T.SE);
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

    /* Check same green used at a different power level */
    solid->vpower = power = 3 * P0;

    time_current(&t0);
    OK(sdis_solve_probe(scn, &solve_args, &estimator));
    time_sub(&t0, time_current(&t1), &t0);
    time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));

    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    if(steady) {
      const double x = solve_args.position[0] - 0.5;
      const double ref = power / (2 * LAMBDA) * (1.0 / 4.0 - x * x) + T0;
      printf
        ("Steady temperature - pos: %g, %g, %g; Power: %g algo: %s "
         "= %g ~ %g +/- %g\n",
        SPLIT3(solve_args.position), power, algo_cstr(algo), ref, T.E, T.SE);
      CHK(eq_eps(T.E, ref, T.SE * 3));
    } else {
      printf(
        "Mean temperature - pos: %g, %g, %g;  t in [%g %g]; power: %g; algo: %s "
        "~ %g +/- %g\n",
        SPLIT3(solve_args.position), SPLIT2(solve_args.time_range),
        power, algo_cstr(algo), T.E, T.SE);
    }

    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    printf("Elapsed time = %s\n", dump);
    printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);

    CHK(nfails + nreals == N);
    CHK(nfails <= N/1000);

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
}

static void
check_null_power_term_with_green(struct sdis_scene* scn, struct solid* solid)
{
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_green_function* green = NULL;
  double x = 0;
  double ref = 0;
  ASSERT(scn && solid);

  solve_args.position[0] = 0.5;
  solve_args.position[1] = 0.5;
  solve_args.position[2] = 0;
  solve_args.nrealisations = N;
  solve_args.time_range[0] =
  solve_args.time_range[1] = INF;

  solid->vpower = 0;
  OK(sdis_solve_probe_green_function(scn, &solve_args, &green));

  solid->vpower = P0;
  OK(sdis_green_function_solve(green, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &T));

  x = solve_args.position[0] - 0.5;
  ref = solid->vpower / (2*LAMBDA) * (1.0/4.0 - x *x) + T0;
  printf("Green steady temperature at (%g, %g, %g) with Power=%g = %g ~ %g +/- %g\n",
    SPLIT3(solve_args.position), solid->vpower, ref, T.E, T.SE);
  CHK(eq_eps(ref, T.E, 3*T.SE));

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_green_function_ref_put(green));
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
  struct sdis_scene* box_scn = NULL;
  struct sdis_scene* square_scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* box_interfaces[12 /*#triangles*/];
  struct sdis_interface* square_interfaces[4/*#segments*/];
  struct interf* interf_props = NULL;
  struct solid* solid_props = NULL;
  struct ssp_rng* rng = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  fluid_shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  solid_shader.volumic_power = solid_get_volumic_power;

  /* Create the solid medium */
  OK(sdis_data_create(dev, sizeof(struct solid), 16, NULL, &data));
  solid_props = sdis_data_get(data);
  solid_props->lambda = LAMBDA;
  solid_props->cp = 2;
  solid_props->rho = 25;
  solid_props->delta = DELTA;
  solid_props->vpower = P0;
  solid_props->t0 = 0;
  solid_props->initial_temperature = T0;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;

  /* Create the adiabatic interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_adiabatic));
  OK(sdis_data_ref_put(data));

  /* Create the T0 interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = T0;
  OK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_T0));
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  /* Map the interfaces to their box triangles */
  box_interfaces[0] = box_interfaces[1] = interf_adiabatic; /* Front */
  box_interfaces[2] = box_interfaces[3] = interf_T0;        /* Left */
  box_interfaces[4] = box_interfaces[5] = interf_adiabatic; /* Back */
  box_interfaces[6] = box_interfaces[7] = interf_T0;        /* Right */
  box_interfaces[8] = box_interfaces[9] = interf_adiabatic; /* Top */
  box_interfaces[10]= box_interfaces[11]= interf_adiabatic; /* Bottom */

  /* Map the interfaces to their square segments */
  square_interfaces[0] = interf_adiabatic; /* Bottom */
  square_interfaces[1] = interf_T0;        /* Left */
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

  /* Solve */
  OK(ssp_rng_create(NULL, SSP_RNG_MT19937_64, &rng));
  printf(">> Box scene\n");
  solve(box_scn, rng, solid_props);
  printf(">> Square scene\n");
  solve(square_scn, rng, solid_props);

  /* Check green registration with a null power term */
  check_null_power_term_with_green(box_scn, solid_props);

  OK(sdis_scene_ref_put(box_scn));
  OK(sdis_scene_ref_put(square_scn));
  OK(sdis_device_ref_put(dev));
  OK(ssp_rng_ref_put(rng));

  CHK(mem_allocated_size() == 0);
  return 0;
}

