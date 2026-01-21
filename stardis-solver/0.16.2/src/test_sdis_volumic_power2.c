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

#define N 10000 /* #realisations */
#define Pw 10000 /* Volumic power */
#define DELTA 0.01
#define DELTA_PSQUARE 0.01

struct reference {
  double pos[3];
  double temperature_2d; /* In celcius */
  double temperature_3d; /* In celcius */
};

/* Temperature in Celcius. The reference is computed by EDF with Syrthes
 * #realisations: 100000
 *
 * >>> Check 1
 * 0.85 0 = 190.29 ~ 190.198 +/- 0.572596; #failures: 46
 * 0.65 0 = 259.95 ~ 259.730 +/- 0.678251; #failures: 73
 * 0.45 0 = 286.33 ~ 285.287 +/- 0.693572; #failures: 74
 * 0.25 0 = 235.44 ~ 235.672 +/- 0.710927; #failures: 61
 * 0.05 0 = 192.33 ~ 192.464 +/- 0.693148; #failures: 70
 *-0.15 0 = 156.82 ~ 157.526 +/- 0.668902; #failures: 43
 *-0.35 0 = 123.26 ~ 124.234 +/- 0.634061; #failures: 31
 *-0.55 0 = 90.250 ~ 91.0285 +/- 0.566423; #failures: 32
 *
 * >>> Check 2
 * 0.85 0 = 678.170 ~ 671.302 +/- 4.03424; #failures: 186
 * 0.65 0 = 1520.84 ~ 1523.42 +/- 5.38182; #failures: 442
 * 0.45 0 = 1794.57 ~ 1790.60 +/- 5.44808; #failures: 528
 * 0.25 0 = 1429.74 ~ 1419.80 +/- 5.33467; #failures: 406 */

static const double vertices[16/*#vertices*/*3/*#coords per vertex*/] = {
 -0.5,-1.0,-0.5,
 -0.5, 1.0,-0.5,
  0.5, 1.0,-0.5,
  0.5,-1.0,-0.5,
 -0.5,-1.0, 0.5,
 -0.5, 1.0, 0.5,
  0.5, 1.0, 0.5,
  0.5,-1.0, 0.5,
 -0.1, 0.4,-0.5,
 -0.1, 0.6,-0.5,
  0.1, 0.6,-0.5,
  0.1, 0.4,-0.5,
 -0.1, 0.4, 0.5,
 -0.1, 0.6, 0.5,
  0.1, 0.6, 0.5,
  0.1, 0.4, 0.5
};
static const size_t nvertices = sizeof(vertices)/(sizeof(double)*3);

static const size_t indices[36/*#triangles*/*3/*#indices per triangle*/]= {
  0, 4, 5, 5, 1, 0, /* Cuboid left */
  1, 5, 6, 6, 2, 1, /* Cuboid top */
  6, 7, 3, 3, 2, 6, /* Cuboid right */
  0, 3, 7, 7, 4, 0, /* Cuboid bottom */
  /* Cuboid back */
  0, 1, 9, 9, 8, 0,
  1, 2, 10, 10, 9, 1,
  2, 3, 11, 11, 10, 2,
  3, 0, 8, 8, 11, 3,
  /* Cuboid front */
  5, 4, 12, 12, 13, 5,
  5, 13, 14, 14, 6, 5,
  6, 14, 15, 15, 7, 6,
  7, 15, 12, 12, 4, 7,
  8, 12, 13, 13, 9, 8, /* Cube left */
  9, 13, 14, 14, 10, 9, /* Cube top */
  14, 15, 11, 11, 10, 14, /* Cube right */
  8, 11, 15, 15, 12, 8,  /* Cube bottom */
  8, 9, 10, 10, 11, 8, /* Cube back */
  12, 15, 14, 14, 13, 12 /* Cube front */
};
static const size_t ntriangles = sizeof(indices)/(sizeof(size_t)*3);

/*******************************************************************************
 * Geometry
 ******************************************************************************/
static void
get_indices(const size_t itri, size_t ids[3], void* context)
{
  (void)context;
  CHK(ids);
  ids[0] = indices[itri*3+0];
  ids[1] = indices[itri*3+1];
  ids[2] = indices[itri*3+2];
}

static void
get_position(const size_t ivert, double pos[3], void* context)
{
  (void)context;
  CHK(pos);
  pos[0] = vertices[ivert*3+0];
  pos[1] = vertices[ivert*3+1];
  pos[2] = vertices[ivert*3+2];
}

static void
get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  *bound = interfaces[itri/2];
}

/*******************************************************************************
 * Solid medium
 ******************************************************************************/
struct solid {
  double cp;
  double lambda;
  double rho;
  double delta;
  double P;
  double T;
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
  return ((const struct solid*)sdis_data_cget(data))->T;
}

static double
solid_get_volumic_power
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->P;
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
  CHK(data != NULL && vtx != NULL);
  return ((const struct fluid*)sdis_data_cget(data))->temperature;
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
 * Helper functions
 ******************************************************************************/
static void
check(struct sdis_scene* scn, const struct reference refs[], const size_t nrefs)
{
  struct sdis_estimator* estimator = NULL;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  size_t nreals;
  size_t nfails;
  size_t i;

  solve_args.time_range[0] = INF;
  solve_args.time_range[1] = INF;
  solve_args.nrealisations = N;

  FOR_EACH(i, 0, nrefs) {
    double Tc;
    solve_args.position[0] = refs[i].pos[0];
    solve_args.position[1] = refs[i].pos[1];
    solve_args.position[2] = refs[i].pos[2];

    OK(sdis_solve_probe(scn, &solve_args, &estimator));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    Tc = T.E - 273.15; /* Convert in Celcius */
    printf("Temperature at (%g %g %g) = %g ~ %g +/- %g [%g, %g]\n",
      SPLIT3(refs[i].pos), refs[i].temperature_2d, Tc, T.SE, Tc-3*T.SE, Tc+3*T.SE);
    printf("#realisations: %lu; #failures: %lu\n",
      (unsigned long)nreals, (unsigned long)nfails);
    /*CHK(eq_eps(Tc, refs[i].temperature, T.SE*3));*/
    OK(sdis_estimator_ref_put(estimator));
  }
}

int
main(int argc, char** argv)
{
  struct solid* solid_param = NULL;
  struct fluid* fluid_param = NULL;
  struct interf* interf_param = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_data* data = NULL;
  struct sdis_medium* fluid1 = NULL;
  struct sdis_medium* fluid2 = NULL;
  struct sdis_medium* solid1 = NULL;
  struct sdis_medium* solid2 = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interf_solid1_adiabatic = NULL;
  struct sdis_interface* interf_solid2_adiabatic = NULL;
  struct sdis_interface* interf_solid1_solid2 = NULL;
  struct sdis_interface* interf_solid1_fluid1 = NULL;
  struct sdis_interface* interf_solid1_fluid2 = NULL;
  struct sdis_interface* interfaces[18 /*#rectangles*/];
  /* In celcius. Computed by EDF with Syrthes */
  const struct reference refs1[] = { /* Lambda1=1, Lambda2=10, Pw = 10000 */
    {{0, 0.85, 0}, 190.29, 189.13},
    {{0, 0.65, 0}, 259.95, 247.09},
    {{0, 0.45, 0}, 286.33, 308.42},
    {{0, 0.25, 0}, 235.44, 233.55},
    {{0, 0.05, 0}, 192.33, 192.30},
    {{0,-0.15, 0}, 156.82, 156.98},
    {{0,-0.35, 0}, 123.26, 123.43},
    {{0,-0.55, 0}, 90.250, 90.040}
  };
  const struct reference refs2[] = { /* Lambda1=0.1, Lambda2=10, Pw=10000 */
    {{0, 0.85}, 678.170, 0},
    {{0, 0.65}, 1520.84, 0},
    {{0, 0.45}, 1794.57, 0},
    {{0, 0.25}, 1429.74, 0}
  };
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  /* Setup the fluid shader */
  fluid_shader.temperature = fluid_get_temperature;
  fluid_shader.calorific_capacity = dummy_medium_getter;
  fluid_shader.volumic_mass = dummy_medium_getter;

  /* Create the fluid1 medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = 373.15;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid1) );
  OK(sdis_data_ref_put(data));

  /* Create the fluid2 medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = 273.15;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid2));
  OK(sdis_data_ref_put(data));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  solid_shader.volumic_power = solid_get_volumic_power;

  /* Create the solid1 medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data) );
  solid_param = sdis_data_get(data);
  solid_param->cp = 500000;
  solid_param->rho = 1000;
  solid_param->lambda = 1;
  solid_param->delta = DELTA;
  solid_param->P = SDIS_VOLUMIC_POWER_NONE;
  solid_param->T = SDIS_TEMPERATURE_NONE;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid1));
  OK(sdis_data_ref_put(data));

  /* Create the solid2 medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 500000;
  solid_param->rho = 1000;
  solid_param->lambda = 10;
  solid_param->delta = DELTA_PSQUARE;
  solid_param->P = Pw;
  solid_param->T = SDIS_TEMPERATURE_NONE;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid2));
  OK(sdis_data_ref_put(data));

  /* Create the solid1/solid2 interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  OK(sdis_interface_create(dev, solid2, solid1, &SDIS_INTERFACE_SHADER_NULL,
    NULL, &interf_solid1_solid2));
  OK(sdis_data_ref_put(data));

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;

  /* Create the adiabatic interfaces */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 0;
  OK(sdis_interface_create(dev, solid1, fluid1, &interf_shader, data,
    &interf_solid1_adiabatic));
  OK(sdis_interface_create(dev, solid2, fluid1, &interf_shader, data,
    &interf_solid2_adiabatic));
  OK(sdis_data_ref_put(data));

  /* Setup the interface shader */
  interf_shader.front.temperature = interface_get_temperature;

  /* Create the solid1/fluid1 interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 5;
  interf_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create(dev, solid1, fluid1, &interf_shader, data,
    &interf_solid1_fluid1));
  OK(sdis_data_ref_put(data));

  /* Create the solid1/fluid2 interace */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 10;
  interf_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create(dev, solid1, fluid2, &interf_shader, data,
    &interf_solid1_fluid2));
  OK(sdis_data_ref_put(data));

  /* Map the interfaces to their faces */
  interfaces[0] = interf_solid1_adiabatic;
  interfaces[1] = interf_solid1_fluid1;
  interfaces[2] = interf_solid1_adiabatic;
  interfaces[3] = interf_solid1_fluid2;
  interfaces[4] = interf_solid1_adiabatic;
  interfaces[5] = interf_solid1_adiabatic;
  interfaces[6] = interf_solid1_adiabatic;
  interfaces[7] = interf_solid1_adiabatic;
  interfaces[8] = interf_solid1_adiabatic;
  interfaces[9] = interf_solid1_adiabatic;
  interfaces[10] = interf_solid1_adiabatic;
  interfaces[11] = interf_solid1_adiabatic;
  interfaces[12] = interf_solid1_solid2;
  interfaces[13] = interf_solid1_solid2;
  interfaces[14] = interf_solid1_solid2;
  interfaces[15] = interf_solid1_solid2;
  interfaces[16] = interf_solid2_adiabatic;
  interfaces[17] = interf_solid2_adiabatic;

  /* Create the scene */
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = ntriangles;
  scn_args.nvertices = nvertices;
  scn_args.context = interfaces;
  OK(sdis_scene_create(dev, &scn_args, &scn));

#if 0
  dump_mesh(stdout, vertices, nvertices, indices, ntriangles);
  exit(0);
#endif

  printf(">>> Check 1\n");
  check(scn, refs1, sizeof(refs1)/sizeof(struct reference));

  /* Update the scene */
  OK(sdis_scene_ref_put(scn));
  data = sdis_medium_get_data(solid1);
  solid_param = sdis_data_get(data);
  solid_param->lambda = 0.1;
  OK(sdis_scene_create(dev, &scn_args, &scn));

  printf("\n>>> Check 2\n");
  check(scn, refs2, sizeof(refs2)/sizeof(struct reference));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_solid1_adiabatic));
  OK(sdis_interface_ref_put(interf_solid2_adiabatic));
  OK(sdis_interface_ref_put(interf_solid1_fluid1));
  OK(sdis_interface_ref_put(interf_solid1_fluid2));
  OK(sdis_interface_ref_put(interf_solid1_solid2));

  /* Release the media */
  OK(sdis_medium_ref_put(fluid1));
  OK(sdis_medium_ref_put(fluid2));
  OK(sdis_medium_ref_put(solid1));
  OK(sdis_medium_ref_put(solid2));

  OK(sdis_scene_ref_put(scn));
  OK(sdis_device_ref_put(dev));

  CHK(mem_allocated_size() == 0);
  return 0;
}

