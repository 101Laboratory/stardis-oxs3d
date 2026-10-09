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

/* H delta T */
#define Tboundary1 SDIS_TEMPERATURE_NONE
#define Tboundary2 SDIS_TEMPERATURE_NONE
#define DELTA 0.01
#define Tref 286.83 /* In Celsius. Computed with Syrthes at the position 0.5 */

/* Dirichlets */
/*#define Tboundary1 373.15*/
/*#define Tboundary2 273.15*/
/*#define DELTA 0.01*/
/*#define Tref 246.93*/ /* In Celsius. Computed with Syrthes at the position 0.5 */

/* Temperature in Celcius. The reference is computed by EDF with Syrthes
 * #realisations: 100000
 *
 * >>> Check1
 * 0.85 = 190.29 ~ 189.322 +/- 0.566717; #failures: 51
 * 0.65 = 259.95 ~ 259.995 +/- 0.674453; #failures: 82
 * 0.45 = 286.33 ~ 285.928 +/- 0.691044; #failures: 76
 * 0.25 = 235.44 ~ 234.672 +/- 0.700354; #failures: 80
 * 0.05 = 192.33 ~ 191.977 +/- 0.690793; #failures: 64
 *-0.15 = 156.82 ~ 155.765 +/- 0.660722; #failures: 40
 *-0.35 = 123.26 ~ 122.973 +/- 0.621093; #failures: 29
 *-0.55 = 90.250 ~ 90.3501 +/- 0.561255; #failures: 27
 *
 * >>> Check 2
 * 0.85 = 678.170 ~ 662.616 +/- 3.97997; #failures: 221
 * 0.65 = 1520.84 ~ 1486.35 +/- 5.25785; #failures: 474
 * 0.45 = 1794.57 ~ 1767.21 +/- 5.36318; #failures: 584
 * 0.25 = 1429.74 ~ 1401.39 +/- 5.25579; #failures: 465
 *
 * >>> Check 3
 * 0.85 = 83.99 ~ 84.0098 +/- 0.115932; #failures: 51
 * 0.65 = 73.90 ~ 73.9596 +/- 0.138835; #failures: 82
 * 0.45 = 68.43 ~ 70.0292 +/- 0.144928; #failures: 76
 * 0.25 = 60.61 ~ 61.4412 +/- 0.153980; #failures: 80
 * 0.05 = 52.09 ~ 51.9452 +/- 0.158045; #failures: 64
 *-0.15 = 42.75 ~ 42.9072 +/- 0.156546; #failures: 40
 *-0.35 = 33.04 ~ 33.9338 +/- 0.149751; #failures: 29
 *-0.55 = 24.58 ~ 24.7237 +/- 0.136441; #failures: 27 */

/*
 *           _\  T1
 *          / /
 *          \__/
 *   ///+-----H1-------+///
 *   ///|              |///
 *   ///|   +------+   |///
 *   ///|   |LAMBDA|   |///
 *   ///|   |  Pw  |   |///
 *   ///|   +------+   |///
 *   ///|              |///
 *   ///|              |///
 *   ///|   LAMBDA1    |///
 *   ///|              |///
 *   ///|              |///
 *   ///|              |///
 *   ///+-----H2-------+///
 *            _\  T2
 *           / /
 *           \__/
 */

struct reference {
  double pos[2];
  double temperature; /* In celcius */
};

static const double vertices[8/*#vertices*/*2/*#coords per vertex*/] = {
 -0.5,-1.0,
 -0.5, 1.0,
  0.5, 1.0,
  0.5,-1.0,
 -0.1, 0.4,
 -0.1, 0.6,
  0.1, 0.6,
  0.1, 0.4
};
static const size_t nvertices = sizeof(vertices)/(sizeof(double)*2);

static const size_t indices[8/*#segments*/*2/*#indices per segment*/]= {
  0, 1, /* Rectangle left */
  1, 2, /* Rectangle top */
  2, 3, /* Rectangle right */
  3, 0, /* Rectangle bottom */
  4, 5, /* Square left */
  5, 6, /* Square top */
  6, 7, /* Square right */
  7, 4  /* Square bottom */
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
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  size_t nreals;
  size_t nfails;
  size_t i;

  solve_args.nrealisations = N;
  solve_args.time_range[0] = INF;
  solve_args.time_range[1] = INF;

  FOR_EACH(i, 0, nrefs) {
    double Tc;
    solve_args.position[0] = refs[i].pos[0];
    solve_args.position[1] = refs[i].pos[1];

    OK(sdis_solve_probe(scn, &solve_args, &estimator));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    Tc = T.E - 273.15; /* Convert in Celcius */
    printf("Temperature at (%g %g) = %g ~ %g +/- %g [%g, %g]\n",
      SPLIT2(refs[i].pos), refs[i].temperature, Tc, T.SE, Tc-3*T.SE, Tc+3*T.SE);
    printf("#realisations: %lu; #failures: %lu\n",
      (unsigned long)nreals, (unsigned long)nfails);
    /*CHK(eq_eps(Tc, refs[i].temperature, T.SE*3));*/
    OK(sdis_estimator_ref_put(estimator));
  }
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
  struct sdis_medium* fluid1 = NULL;
  struct sdis_medium* fluid2 = NULL;
  struct sdis_medium* solid1 = NULL;
  struct sdis_medium* solid2 = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interf_adiabatic = NULL;
  struct sdis_interface* interf_solid1_solid2 = NULL;
  struct sdis_interface* interf_solid1_fluid1 = NULL;
  struct sdis_interface* interf_solid1_fluid2 = NULL;
  struct sdis_interface* interfaces[8 /*#segment*/];

  /* In celcius. Computed by EDF with Syrthes */
  const struct reference refs1[] = { /* Lambda1=1, Lambda2=10, Pw = 10000 */
    {{0, 0.85}, 190.29},
    {{0, 0.65}, 259.95},
    {{0, 0.45}, 286.33},
    {{0, 0.25}, 235.44},
    {{0, 0.05}, 192.33},
    {{0,-0.15}, 156.82},
    {{0,-0.35}, 123.26},
    {{0,-0.55}, 90.250}
  };
  const struct reference refs2[] = { /* Lambda1=0.1, Lambda2=10, Pw=10000 */
    {{0, 0.85}, 678.17},
    {{0, 0.65}, 1520.84},
    {{0, 0.45}, 1794.57},
    {{0, 0.25}, 1429.74}
  };
  const struct reference refs3[] = { /* Lambda1=1, Lambda2=10, Pw=NONE */
    {{0, 0.85}, 83.99},
    {{0, 0.65}, 73.90},
    {{0, 0.45}, 68.43},
    {{0, 0.25}, 60.61},
    {{0, 0.05}, 52.09},
    {{0,-0.15}, 42.75},
    {{0,-0.35}, 33.04},
    {{0,-0.55}, 24.58}
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
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid1));
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
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
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
  solid_param->delta = DELTA;
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

  /* Create the adiabatic interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 0;
  OK(sdis_interface_create(dev, solid1, fluid1, &interf_shader, data,
    &interf_adiabatic));
  OK(sdis_data_ref_put(data));

  /* Setup the interface shader */
  interf_shader.front.temperature = interface_get_temperature;

  /* Create the solid1/fluid1 interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 5;
  interf_param->temperature = Tboundary1;
  OK(sdis_interface_create(dev, solid1, fluid1, &interf_shader, data,
    &interf_solid1_fluid1));
  OK(sdis_data_ref_put(data));

  /* Create the solid1/fluid2 interace */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 10;
  interf_param->temperature = Tboundary2;
  OK(sdis_interface_create(dev, solid1, fluid2, &interf_shader, data,
    &interf_solid1_fluid2));
  OK(sdis_data_ref_put(data));


  /* Map the interfaces to their square segments */
  interfaces[0] = interf_adiabatic;
  interfaces[1] = interf_solid1_fluid1;
  interfaces[2] = interf_adiabatic;
  interfaces[3] = interf_solid1_fluid2;
  interfaces[4] = interf_solid1_solid2;
  interfaces[5] = interf_solid1_solid2;
  interfaces[6] = interf_solid1_solid2;
  interfaces[7] = interf_solid1_solid2;

  /* Create the scene */
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface; 
  scn_args.get_position = get_position;
  scn_args.nprimitives = nsegments;
  scn_args.nvertices = nvertices;
  scn_args.context = interfaces;
  OK(sdis_scene_2d_create(dev, &scn_args, &scn));

  printf(">>> Check 1\n");
  check(scn, refs1, sizeof(refs1)/sizeof(struct reference));

  /* Update the scene */
  OK(sdis_scene_ref_put(scn));
  data = sdis_medium_get_data(solid1);
  solid_param = sdis_data_get(data);
  solid_param->lambda = 0.1;
  OK(sdis_scene_2d_create(dev, &scn_args, &scn));

  printf("\n>>> Check 2\n");
  check(scn, refs2, sizeof(refs2)/sizeof(struct reference));

  /* Update the scene */
  OK(sdis_scene_ref_put(scn));
  data = sdis_medium_get_data(solid1);
  solid_param = sdis_data_get(data);
  solid_param->lambda = 1;
  data = sdis_medium_get_data(solid2);
  solid_param = sdis_data_get(data);
  solid_param->lambda = 10;
  solid_param->P = SDIS_VOLUMIC_POWER_NONE;
  OK(sdis_scene_2d_create(dev, &scn_args, &scn));

  printf("\n>>> Check 3\n");
  check(scn, refs3, sizeof(refs3)/sizeof(struct reference));

#if 0
  dump_segments(stdout, vertices, nvertices, indices, nsegments);
  exit(0);
#endif

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_adiabatic));
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

