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

#include <rsys/math.h>

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

#define UNKNOWN_TEMPERATURE -1
#define N 10000 /* #realisations */

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
  return UNKNOWN_TEMPERATURE;
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
  size_t nreals;
  size_t nfails;
  CHK(estimator && nrealisations);

  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  printf("%g ~ %g +/- %g\n", ref, T.E, T.SE);
  printf("#failures = %lu/%lu\n",
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
  struct mem_allocator allocator;
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
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* box_interfaces[12 /*#triangles*/];
  struct sdis_interface* square_interfaces[4/*#segments*/];
  struct interf* interf_props = NULL;
  struct fluid* fluid_param;
  double uv[2];
  double pos[3];
  double time_range[2] = { INF, INF };
  double tr[2];
  double ref;
  size_t prims[4];
  enum sdis_side sides[4];
  size_t iprim;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev));

  /* Create the fluid medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tf;
  fluid_shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid));
  OK(sdis_data_ref_put(data));

  /* Create the solid_medium */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
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
  interf_props->temperature = UNKNOWN_TEMPERATURE;
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
  interf_props->temperature = UNKNOWN_TEMPERATURE;
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
  OK(sdis_scene_create(dev, box_ntriangles, box_get_indices,
    box_get_interface, box_nvertices, box_get_position, box_interfaces,
    &box_scn));

  /* Create the square scene */
  OK(sdis_scene_2d_create(dev, square_nsegments, square_get_indices,
    square_get_interface, square_nvertices, square_get_position,
    square_interfaces, &square_scn));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_adiabatic));
  OK(sdis_interface_ref_put(interf_Tb));
  OK(sdis_interface_ref_put(interf_H));

  ref = (H*Tf + LAMBDA * Tb) / (H + LAMBDA);

  #define SOLVE sdis_solve_probe_boundary
  #define F SDIS_FRONT
  uv[0] = 0.3;
  uv[1] = 0.3;
  iprim = 6;

  BA(SOLVE(NULL, N, iprim, uv, time_range, F, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, 0, iprim, uv, time_range, F, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, N, 12, uv, time_range, F, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, N, iprim, NULL, time_range, F, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, N, iprim, uv, NULL, F, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, N, iprim, uv, time_range, -1, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, N, iprim, uv, time_range, F, 1.0, 0, 0, NULL));
  tr[0] = tr[1] = -1;
  BA(SOLVE(box_scn, N, iprim, uv, tr, F, 1.0, 0, 0, NULL));
  tr[0] = 1;
  BA(SOLVE(box_scn, N, iprim, uv, tr, F, 1.0, 0, 0, NULL));
  tr[1] = 0;
  BA(SOLVE(box_scn, N, iprim, uv, tr, F, 1.0, 0, 0, NULL));

  OK(SOLVE(box_scn, N, iprim, uv, time_range, F, 1.0, 0, 0, &estimator));
  OK(sdis_scene_get_boundary_position(box_scn, iprim, uv, pos));
  printf("Boundary temperature of the box at (%g %g %g) = ", SPLIT3(pos));
  check_estimator(estimator, N, ref);
  OK(sdis_estimator_ref_put(estimator));

  /* The external fluid cannot have an unknown temperature */
  fluid_param->temperature = UNKNOWN_TEMPERATURE;
  BA(SOLVE(box_scn, N, iprim, uv, time_range, F, 1.0, 0, 0, &estimator));
  fluid_param->temperature = Tf;

  uv[0] = 0.5;
  iprim = 3;
  BA(SOLVE(square_scn, N, 4, uv, time_range, F, 1.0, 0, 0, &estimator));
  OK(SOLVE(square_scn, N, iprim, uv, time_range, F, 1.0, 0, 0, &estimator));
  OK(sdis_scene_get_boundary_position(square_scn, iprim, uv, pos));
  printf("Boundary temperature of the square at (%g %g) = ", SPLIT2(pos));
  check_estimator(estimator, N, ref);
  OK(sdis_estimator_ref_put(estimator));

  /* The external fluid cannot have an unknown temperature */
  fluid_param->temperature = UNKNOWN_TEMPERATURE;
  BA(SOLVE(square_scn, N, iprim, uv, time_range, F, 1.0, 0, 0, &estimator));
  fluid_param->temperature = Tf;
  #undef F
  #undef SOLVE

  sides[0] = SDIS_FRONT;
  sides[1] = SDIS_FRONT;
  sides[2] = SDIS_FRONT;
  sides[3] = SDIS_FRONT;

  #define SOLVE sdis_solve_boundary
  prims[0] = 6;
  prims[1] = 7;
  BA(SOLVE(NULL, N, prims, sides, 2, time_range, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, 0, prims, sides, 2, time_range, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, N, NULL, sides, 2, time_range, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, N, prims, NULL, 2, time_range, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, N, prims, sides, 0, time_range, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, N, prims, sides, 2, NULL, 1.0, 0, 0, &estimator));
  BA(SOLVE(box_scn, N, prims, sides, 2, time_range, 1.0, 0, 0, NULL));
  tr[0] = tr[1] = -1;
  BA(SOLVE(box_scn, N, prims, sides, 2, tr, 1.0, 0, 0, NULL));
  tr[0] = 1;
  BA(SOLVE(box_scn, N, prims, sides, 2, tr, 1.0, 0, 0, NULL));
  tr[1] = 0;
  BA(SOLVE(box_scn, N, prims, sides, 2, tr, 1.0, 0, 0, NULL));

  /* Average temperature on the right side of the box */
  OK(SOLVE(box_scn, N, prims, sides, 2, time_range, 1.0, 0, 0, &estimator));
  printf("Average temperature of the right side of the box = ");
  check_estimator(estimator, N, ref);
  OK(sdis_estimator_ref_put(estimator));

  /* Average temperature on the right side of the square */
  prims[0] = 3;
  sides[0] = SDIS_FRONT;
  OK(SOLVE(square_scn, N, prims, sides, 1, time_range, 1.0, 0, 0, &estimator));
  printf("Average temperature of the right side of the square = ");
  check_estimator(estimator, N, ref);
  OK(sdis_estimator_ref_put(estimator));

  /* Check out of bound prims */
  prims[0] = 12;
  BA(SOLVE(box_scn, N, prims, sides, 2, time_range, 1.0, 0, 0, &estimator));
  prims[0] = 4;
  BA(SOLVE(square_scn, N, prims, sides, 1, time_range, 1.0, 0, 0, &estimator));

  /* Average temperature on the left+right sides of the box */
  prims[0] = 2;
  prims[1] = 3;
  prims[2] = 6;
  prims[3] = 7;

  ref = (ref + Tb) / 2;

  OK(SOLVE(box_scn, N, prims, sides, 4, time_range, 1.0, 0, 0, &estimator));
  printf("Average temperature of the left+right sides of the box = ");
  check_estimator(estimator, N, ref);
  OK(sdis_estimator_ref_put(estimator));

  /* Average temperature on the left+right sides of the square */
  prims[0] = 1;
  prims[1] = 3;
  OK(SOLVE(square_scn, N, prims, sides, 2, time_range, 1.0, 0, 0, &estimator));
  printf("Average temperature of the left+right sides of the square = ");
  check_estimator(estimator, N, ref);
  OK(sdis_estimator_ref_put(estimator));
  #undef SOLVE

  OK(sdis_scene_ref_put(box_scn));
  OK(sdis_scene_ref_put(square_scn));
  OK(sdis_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

