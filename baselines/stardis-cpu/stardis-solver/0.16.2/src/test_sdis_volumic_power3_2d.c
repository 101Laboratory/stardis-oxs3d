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

#define Pw 10000.0 /* Volumic power */
#define LAMBDA 10.0 /* Lambda of the middle slab */
#define LAMBDA1 1.0 /* Lambda of the upper slab */
#define LAMBDA2 LAMBDA1 /* Lambda of the lower slab */
#define T1 373.15 /* Temperature of the upper fluid */
#define T2 273.15 /* Temperature of the lower fluid */
#define H1 5.0 /* Convection coef between the upper slab and the fluid */
#define H2 10.0 /* Convection coef between the lower slab and the fluid */
#define DELTA 0.01 /* Delta of the middle slab */
#define DELTA1 0.02 /* Delta of the upper slab */
#define DELTA2 0.07 /* Delta of the lower slab */
#define L 0.2 /* Size of the middle slab */
#define L1 0.4 /* Size of the upper slab */
#define L2 1.4 /* Size of the lower slab */

#define N 10000 /* #realisations */

/* Analitically computed temperatures wrt the previous parameters.*/
#define Tp1 648.6217
#define Tp2 335.4141
#define Ta 1199.5651
#define Tb 1207.1122

/* Fixed temperatures */
#define Tsolid1_fluid SDIS_TEMPERATURE_NONE /*Tp1*/
#define Tsolid2_fluid SDIS_TEMPERATURE_NONE /*Tp2*/
#define Tsolid_solid1 SDIS_TEMPERATURE_NONE /*Ta*/
#define Tsolid_solid2 SDIS_TEMPERATURE_NONE /*Tb*/

#define PROBE_POS 1.8

/*
 * The 2D scene is composed of 3 stacked solid slabs whose middle slab has a
 * volumic power. The +/-X sides of the slabs are stretched far away to
 * simulate a 1D case. The upper and lower bounds of the "sandwich" has a
 * convective exchange with the surrounding fluid whose temperature is known.
 *
 *           _\  T1
 *          / /
 *          \__/
 * ... -----H1------ ... Tp1
 *       LAMBDA1
 *
 * ... ------------- ... Ta
 *       LAMBDA, Pw
 * ... ------------- ... Tb
 *
 *       LAMBDA2
 *
 *
 * ... -----H2------ ... Tp2
 *            _\  T2
 *           / /
 *           \__/
 */

static const double vertices[8/*#vertices*/*2/*#coords per vertex*/] = {
 -100000.5, 0.0, /* 0 */
 -100000.5, 1.4, /* 1 */
 -100000.5, 1.6, /* 2 */
 -100000.5, 2.0, /* 3 */
  100000.5, 2.0, /* 4 */
  100000.5, 1.6, /* 5 */
  100000.5, 1.4, /* 6 */
  100000.5, 0.0  /* 7 */
};
static const size_t nvertices = sizeof(vertices)/(sizeof(double)*2);

static const size_t indices[10/*#segments*/*2/*#indices per segment*/]= {
  0, 1,
  1, 2,
  2, 3,
  3, 4,
  4, 5,
  5, 6,
  6, 7,
  7, 0,
  6, 1,
  2, 5
};
static const size_t nsegments = sizeof(indices)/(sizeof(size_t)*2);

/*******************************************************************************
 * Geometry
 ******************************************************************************/
static void
get_indices(const size_t iseg, size_t ids[2], void* context)
{
  (void)context;
  CHK(ids);
  ids[0] = indices[iseg*2+0];
  ids[1] = indices[iseg*2+1];
}

static void
get_position(const size_t ivert, double pos[2], void* context)
{
  (void)context;
  CHK(pos);
  pos[0] = vertices[ivert*2+0];
  pos[1] = vertices[ivert*2+1];
}

static void
get_interface(const size_t iseg, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  *bound = interfaces[iseg];
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
  double temperature_lower;
  double temperature_upper;
};

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  const struct fluid* fluid;
  CHK(data != NULL && vtx != NULL);
  fluid = sdis_data_cget(data);
  return vtx->P[1] < 1 ? fluid->temperature_lower : fluid->temperature_upper;
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
  struct solid* solid_param = NULL;
  struct fluid* fluid_param = NULL;
  struct interf* interf_param = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_data* data = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* solid1 = NULL;
  struct sdis_medium* solid2 = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interf_solid_adiabatic = NULL;
  struct sdis_interface* interf_solid1_adiabatic = NULL;
  struct sdis_interface* interf_solid2_adiabatic = NULL;
  struct sdis_interface* interf_solid_solid1 = NULL;
  struct sdis_interface* interf_solid_solid2 = NULL;
  struct sdis_interface* interf_solid1_fluid = NULL;
  struct sdis_interface* interf_solid2_fluid = NULL;
  struct sdis_interface* interfaces[10/*#segment*/];
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  double Tref;
  double time_range[2];
  double pos[2];
  size_t nfails;
  size_t nreals;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  time_range[0] = INF;
  time_range[1] = INF;

  /* Create the fluid medium */
  fluid_shader.temperature = fluid_get_temperature;
  fluid_shader.calorific_capacity = dummy_medium_getter;
  fluid_shader.volumic_mass = dummy_medium_getter;
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature_upper = T1;
  fluid_param->temperature_lower = T2;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid));
  OK(sdis_data_ref_put(data));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  solid_shader.volumic_power = solid_get_volumic_power;

  /* Create the medium of the upper slab */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 500000;
  solid_param->rho = 1000;
  solid_param->lambda = LAMBDA1;
  solid_param->delta = DELTA1;
  solid_param->volumic_power = SDIS_VOLUMIC_POWER_NONE;
  solid_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid1));
  OK(sdis_data_ref_put(data));

  /* Create the medium of the lower slab */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 500000;
  solid_param->rho = 1000;
  solid_param->lambda = LAMBDA2;
  solid_param->delta = DELTA2;
  solid_param->volumic_power = SDIS_VOLUMIC_POWER_NONE;
  solid_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid2));
  OK(sdis_data_ref_put(data));

  /* Create the medium of the middle slab */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 500000;
  solid_param->rho = 1000;
  solid_param->lambda = LAMBDA;
  solid_param->delta = DELTA;
  solid_param->volumic_power = Pw;
  solid_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  interf_shader.front.temperature = interface_get_temperature;

  /* Create the solid/solid1 interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->temperature = Tsolid_solid1;
  OK(sdis_interface_create(dev, solid, solid1, &interf_shader,
    data, &interf_solid_solid1));
  OK(sdis_data_ref_put(data));

  /* Create the solid/solid2 interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->temperature = Tsolid_solid2;
  OK(sdis_interface_create(dev, solid, solid2, &interf_shader,
    data, &interf_solid_solid2));
  OK(sdis_data_ref_put(data));

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;

  /* Create the adiabatic interfaces */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 0;
  interf_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create(dev, solid, fluid, &interf_shader, data,
    &interf_solid_adiabatic));
  OK(sdis_interface_create(dev, solid1, fluid, &interf_shader, data,
    &interf_solid1_adiabatic));
  OK(sdis_interface_create(dev, solid2, fluid, &interf_shader, data,
    &interf_solid2_adiabatic) );
  OK(sdis_data_ref_put(data));

  /* Create the solid1 fluid interace */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = H1;
  interf_param->temperature = Tsolid1_fluid;
  OK(sdis_interface_create(dev, solid1, fluid, &interf_shader, data,
    &interf_solid1_fluid));
  OK(sdis_data_ref_put(data));

  /* Create the solid2 fluid interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = H2;
  interf_param->temperature = Tsolid2_fluid;
  OK(sdis_interface_create(dev, solid2, fluid, &interf_shader, data,
    &interf_solid2_fluid));
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(solid1));
  OK(sdis_medium_ref_put(solid2));

  /* Map the interfaces to their square segments */
  interfaces[0] = interf_solid2_adiabatic;
  interfaces[1] = interf_solid_adiabatic;
  interfaces[2] = interf_solid1_adiabatic;
  interfaces[3] = interf_solid1_fluid;
  interfaces[4] = interf_solid1_adiabatic;
  interfaces[5] = interf_solid_adiabatic;
  interfaces[6] = interf_solid2_adiabatic;
  interfaces[7] = interf_solid2_fluid;
  interfaces[8] = interf_solid_solid2;
  interfaces[9] = interf_solid_solid1;

#if 0
  dump_segments(stdout, vertices, nvertices, indices, nsegments);
  exit(0);
#endif

  /* Create the scene */
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = nsegments;
  scn_args.nvertices = nvertices;
  scn_args.context = interfaces;
  OK(sdis_scene_2d_create(dev, &scn_args, &scn));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_solid_adiabatic));
  OK(sdis_interface_ref_put(interf_solid1_adiabatic));
  OK(sdis_interface_ref_put(interf_solid2_adiabatic));
  OK(sdis_interface_ref_put(interf_solid_solid1));
  OK(sdis_interface_ref_put(interf_solid_solid2));
  OK(sdis_interface_ref_put(interf_solid1_fluid));
  OK(sdis_interface_ref_put(interf_solid2_fluid));

  pos[0] = 0;
  pos[1] = PROBE_POS;

 if(pos[1] > 0 && pos[1] < L2) { /* Lower slab */
    Tref = Tp2 + (Tb - Tp2) * pos[1] / L2;
  } else if(pos[1] > L2 && pos[1] < L2 + L) { /* Middle slab */
    Tref =
      (Ta + Tb) / 2
    + (Ta - Tb)/L * (pos[1] - (L2+L/2))
    + Pw * (L*L/4.0 - pow((pos[1] - (L2+L/2)), 2)) / (2*LAMBDA);
  } else if(pos[1] > L2 + L && pos[1] < L2 + L1 + L) {
    Tref = Ta + (Tp1 - Ta) / L1 * (pos[1] - (L+L2));
  } else {
    FATAL("Unreachable code.\n");
  }

  solve_args.nrealisations = N;
  solve_args.position[0] = pos[0];
  solve_args.position[1] = pos[1];
  solve_args.time_range[0] = time_range[0];
  solve_args.time_range[1] = time_range[1];
  OK(sdis_solve_probe(scn, &solve_args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  printf("Temperature at (%g %g) = %g ~ %g +/- %g [%g, %g]\n",
    SPLIT2(pos), Tref, T.E, T.SE, T.E-3*T.SE, T.E+3*T.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
  OK(sdis_estimator_ref_put(estimator));

  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, Tref, T.SE*3));

  OK(sdis_scene_ref_put(scn));
  OK(sdis_device_ref_put(dev));

  CHK(mem_allocated_size() == 0);
  return 0;
}

