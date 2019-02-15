/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

/*
 * The scene is composed of a solid cube/square whose temperature is unknown.
 * The temperature is fixed at T0 on the +X face. The Flux of the -X face is
 * fixed to PHI. The flux on the other faces is null (i.e. adiabatic). This
 * test computes the temperature of a probe position pos into the solid and
 * check that it is equal to:
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

#define UNKNOWN_TEMPERATURE -1
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
  return UNKNOWN_TEMPERATURE;
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
solve(struct sdis_scene* scn, const double pos[])
{
  char dump[128];
  struct time t0, t1, t2;
  struct sdis_estimator* estimator;
  struct sdis_estimator* estimator2;
  struct sdis_green_function* green;
  struct sdis_mc T;
  size_t nreals;
  size_t nfails;
  double ref;
  const double time_range[2] = {INF, INF};
  enum sdis_scene_dimension dim;
  ASSERT(scn && pos);

  ref = T0 + (1 - pos[0]) * PHI/LAMBDA;

  time_current(&t0);
  OK(sdis_solve_probe(scn, N, pos, time_range, 1.0, 0, 0, &estimator));
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));

  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &T));

  OK(sdis_scene_get_dimension(scn, &dim));

  switch(dim) {
    case SDIS_SCENE_2D:
      printf("Temperature at (%g %g) = %g ~ %g +/- %g\n",
        SPLIT2(pos), ref, T.E, T.SE);
      break;
    case SDIS_SCENE_3D:
      printf("Temperature at (%g %g %g) = %g ~ %g +/- %g\n",
        SPLIT3(pos), ref, T.E, T.SE);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
  printf("Elapsed time = %s\n\n", dump);

  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, ref, T.SE*3));

  time_current(&t0);
  OK(sdis_solve_probe_green_function(scn, N, pos, 1.0, 0, 0, &green));
  time_current(&t1);
  OK(sdis_green_function_solve(green, time_range, &estimator2));
  time_current(&t2);

  OK(sdis_estimator_get_realisation_count(estimator2, &nreals));
  OK(sdis_estimator_get_failure_count(estimator2, &nfails));
  OK(sdis_estimator_get_temperature(estimator2, &T));

  switch(dim) {
    case SDIS_SCENE_2D:
      printf("Green temperature at (%g %g) = %g ~ %g +/- %g\n",
        SPLIT2(pos), ref, T.E, T.SE);
      break;
    case SDIS_SCENE_3D:
      printf("Green temperature at (%g %g %g) = %g ~ %g +/- %g\n",
        SPLIT3(pos), ref, T.E, T.SE);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
  time_sub(&t0, &t1, &t0);
  time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
  printf("Green estimation time = %s\n", dump);
  time_sub(&t1, &t2, &t1);
  time_dump(&t1, TIME_ALL, NULL, dump, sizeof(dump));
  printf("Green solve time = %s\n\n", dump);

  check_estimator_eq(estimator, estimator2);

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_estimator_ref_put(estimator2));
  OK(sdis_green_function_ref_put(green));
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_interface* interf_adiabatic = NULL;
  struct sdis_interface* interf_T0 = NULL;
  struct sdis_interface* interf_phi = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_scene* square_scn = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* box_interfaces[12 /*#triangles*/];
  struct sdis_interface* square_interfaces[4/*#segments*/];
  struct interf* interf_props = NULL;
  double pos[3];
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 0, &dev));

  /* Create the dummy fluid medium */
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Create the solid_medium */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));

  /* Setup the interface shader */
  interf_shader.front.temperature = interface_get_temperature;
  interf_shader.front.flux = interface_get_flux;

  /* Create the adiabatic interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = UNKNOWN_TEMPERATURE;
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
  interf_props->temperature = UNKNOWN_TEMPERATURE;
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
  OK(sdis_scene_create(dev, box_ntriangles, box_get_indices,
    box_get_interface, box_nvertices, box_get_position, box_interfaces,
    &box_scn));

  /* Create the square scene */
  OK(sdis_scene_2d_create(dev, square_nsegments, square_get_indices,
    square_get_interface, square_nvertices, square_get_position,
    square_interfaces, &square_scn));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_adiabatic));
  OK(sdis_interface_ref_put(interf_T0));
  OK(sdis_interface_ref_put(interf_phi));

  /* Solve */
  d3_splat(pos, 0.25);
  printf(">> Box scene\n");
  solve(box_scn, pos);
  printf(">> Square Scene\n");
  solve(square_scn, pos);

  OK(sdis_scene_ref_put(box_scn));
  OK(sdis_scene_ref_put(square_scn));
  OK(sdis_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
