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
#include <rsys_math.h>

#define Tf 100.0
#define Power 10000.0
#define H 50.0
#define LAMBDA 100.0
#define DELTA 0.2/*(1.0/2.0)*/
#define N 100000
#define LENGTH 10000.0

/*
 * The 2D scene is a solid slabs stretched along the X dimension to simulate a
 * 1D case. The slab has a volumic power and has a convective exchange with
 * surrounding fluid whose temperature is fixed to Tf.
 *
 *
 *           _\  Tf
 *          / /
 *          \__/
 *
 * ... -----Hboundary----- ...
 *
 *        Lambda, Power
 *
 * ... -----Hboundary----- ...
 *
 *           _\  Tf
 *          / /
 *          \__/
 *
 */

static const double vertices_2d[4/*#vertices*/*2/*#coords per vertex*/] = {
  LENGTH,-0.5,
 -LENGTH,-0.5,
 -LENGTH, 0.5,
  LENGTH, 0.5
};

static const double vertices_3d[8/*#vertices*/*3/*#coords per vertex*/] = {
 -LENGTH,-0.5,-LENGTH,
  LENGTH,-0.5,-LENGTH,
 -LENGTH, 0.5,-LENGTH,
  LENGTH, 0.5,-LENGTH,
 -LENGTH,-0.5, LENGTH,
  LENGTH,-0.5, LENGTH,
 -LENGTH, 0.5, LENGTH,
  LENGTH, 0.5, LENGTH
};

/*******************************************************************************
 * Geometry
 ******************************************************************************/
static void
get_position_2d(const size_t ivert, double pos[2], void* context)
{
  (void)context;
  CHK(pos);
  pos[0] = vertices_2d[ivert*2+0];
  pos[1] = vertices_2d[ivert*2+1];
}

static void
get_position_3d(const size_t ivert, double pos[3], void* context)
{
  (void)context;
  CHK(pos);
  pos[0] = vertices_3d[ivert*3+0];
  pos[1] = vertices_3d[ivert*3+1];
  pos[2] = vertices_3d[ivert*3+2];
}

static void
get_interface(const size_t iprim, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  *bound = interfaces[iprim];
}

/*******************************************************************************
 * Solid medium
 ******************************************************************************/
struct solid {
  double cp;
  double lambda;
  double rho;
  double delta;
  double volumic_power;
  double temperature;
};

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->cp;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->lambda;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->rho;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->delta;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->temperature;
}

static double
solid_get_volumic_power
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->volumic_power;
}

/*******************************************************************************
 * Fluid medium
 ******************************************************************************/
struct fluid {
  double temperature;
};

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  const struct fluid* fluid;
  CHK(data != NULL && vtx != NULL);
  fluid = sdis_data_cget(data);
  return fluid->temperature;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double h;
  double temperature;
};

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(frag && data);
  return ((const struct interf*)sdis_data_cget(data))->h;
}

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(frag && data);
  return ((const struct interf*)sdis_data_cget(data))->temperature;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  char dump[128];
  struct time t0, t1;
  struct solid* solid_param = NULL;
  struct fluid* fluid_param = NULL;
  struct interf* interf_param = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_data* data = NULL;
  struct sdis_medium* fluid1 = NULL;
  struct sdis_medium* fluid2 = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_scene* scn_2d = NULL;
  struct sdis_scene* scn_3d = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interf_adiabatic = NULL;
  struct sdis_interface* interf_solid_fluid1 = NULL;
  struct sdis_interface* interf_solid_fluid2 = NULL;
  struct sdis_interface* interfaces[12/*#max primitives*/];
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  size_t nreals, nfails;
  double pos[3];
  double Tref;
  double x;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  /* Create the fluid medium */
  fluid_shader.temperature = fluid_get_temperature;
  fluid_shader.calorific_capacity = dummy_medium_getter;
  fluid_shader.volumic_mass = dummy_medium_getter;

  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tf;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid1));
  OK(sdis_data_ref_put(data));

  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tf;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid2));
  OK(sdis_data_ref_put(data));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  solid_shader.volumic_power = solid_get_volumic_power;

  /* Create the solid medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 500000;
  solid_param->rho = 1000;
  solid_param->lambda = LAMBDA;
  solid_param->delta = DELTA;
  solid_param->volumic_power = Power;
  solid_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;

  /* Create the adiabatic interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 0;
  interf_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create(dev, solid, fluid1, &interf_shader, data,
    &interf_adiabatic));
  OK(sdis_data_ref_put(data));

  /* Create the solid fluid1 interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = H;
  interf_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create(dev, solid, fluid1, &interf_shader, data,
    &interf_solid_fluid1));
  OK(sdis_data_ref_put(data));

  /* Create the solid fluid2 interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = H;
  interf_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create(dev, solid, fluid2, &interf_shader, data,
    &interf_solid_fluid2));
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(fluid1));
  OK(sdis_medium_ref_put(fluid2));
  OK(sdis_medium_ref_put(solid));

#if 0
  dump_segments(stdout, vertices, nvertices, indices, nsegments);
  exit(0);
#endif

  /* Map the interfaces to their square segments */
  interfaces[0] = interf_solid_fluid2; /* Bottom */
  interfaces[1] = interf_adiabatic; /* Left */
  interfaces[2] = interf_solid_fluid1; /* Top */
  interfaces[3] = interf_adiabatic; /* Right */

  /* Create the 2D scene */
  scn_args.get_indices = square_get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position_2d;
  scn_args.nprimitives = square_nsegments;
  scn_args.nvertices = square_nvertices;
  scn_args.context = interfaces;
  OK(sdis_scene_2d_create(dev, &scn_args, &scn_2d));

  /* Map the interfaces to their box triangles */
  interfaces[0] = interfaces[1] = interf_adiabatic; /* Front */
  interfaces[2] = interfaces[3] = interf_adiabatic; /* Left */
  interfaces[4] = interfaces[5] = interf_adiabatic; /* Back */
  interfaces[6] = interfaces[7] = interf_adiabatic; /* Right */
  interfaces[8] = interfaces[9] = interf_solid_fluid1; /* Top */
  interfaces[10]= interfaces[11]= interf_solid_fluid2; /* Bottom */

  /* Create the 3D scene */
  scn_args.get_indices = box_get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position_3d;
  scn_args.nprimitives = box_ntriangles;
  scn_args.nvertices = box_nvertices;
  scn_args.context = interfaces;
  OK(sdis_scene_create(dev, &scn_args, &scn_3d));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_adiabatic));
  OK(sdis_interface_ref_put(interf_solid_fluid1));
  OK(sdis_interface_ref_put(interf_solid_fluid2));

  pos[0] = 0;
  pos[1] = 0;
  pos[2] = 0;

  x = pos[1];
  Tref = -Power / (2*LAMBDA) * x*x + Tf + Power/(2*H) + Power/(8*LAMBDA);

  solve_args.nrealisations = N;
  solve_args.position[0] = pos[0];
  solve_args.position[1] = pos[1];
  solve_args.position[2] = pos[2];
  solve_args.time_range[0] = INF;
  solve_args.time_range[1] = INF;

  printf(">>> 2D\n");

  time_current(&t0);
  OK(sdis_solve_probe(scn_2d, &solve_args, &estimator));
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
  printf("Elapsed time = %s\n", dump);

  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  printf("Temperature at (%g %g) = %g ~ %g +/- %g [%g %g]\n",
    SPLIT2(pos), Tref, T.E, T.SE, T.E-3*T.SE, T.E+3*T.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
  OK(sdis_estimator_ref_put(estimator));
  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, Tref, T.SE*3));

  printf("\n>>> 3D\n");

  time_current(&t0);
  OK(sdis_solve_probe(scn_3d, &solve_args, &estimator));
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
  printf("Elapsed time = %s\n", dump);

  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  printf("Temperature at (%g %g) = %g ~ %g +/- %g [%g %g]\n",
    SPLIT2(pos), Tref, T.E, T.SE, T.E-3*T.SE, T.E+3*T.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
  OK(sdis_estimator_ref_put(estimator));
  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, Tref, T.SE*3));

  OK(sdis_scene_ref_put(scn_2d));
  OK(sdis_scene_ref_put(scn_3d));
  OK(sdis_device_ref_put(dev));

  CHK(mem_allocated_size() == 0);
  return 0;
}

