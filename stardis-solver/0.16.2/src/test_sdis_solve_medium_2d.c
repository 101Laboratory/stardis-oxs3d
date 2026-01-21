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

#include <rsys/stretchy_array.h>
#include <rsys_math.h>

#include <string.h>

#define Tf0 300.0
#define Tf1 330.0
#define N 1000ul /* #realisations */
#define Np 10000ul /* #realisations precise */

/*
 * The scene is composed of a square and a disk whose temperature is unknown.
 * The square is surrounded by a fluid whose temperature is Tf0 while the disk
 * is in a fluid whose temperature is Tf1. The temperature of the square
 * and the disk are thus uniform and equal to Tf0 and Tf1, respectively.
 *
 *          #  #          Tf1        +---------+
 *       #        #    _\            |         |   _\  Tf0
 *      #          #  / /            |         |  / /
 *      #          #  \__/           |         |  \__/
 *       #        #                  |         |
 *          #  #                     +---------+
 *
 * This program performs 2 tests. In the first one, the square and the disk
 * have different media; the medium solver estimates the temperature of
 * the square or the one of the disk. In the second test, the scene is updated
 * to use the same medium for the 2 shapes. When invoked on
 * the right medium, the estimated temperature T is equal to :
 *
 *    T = Tf0 * A0/(A0 + A1) + Tf1 * A1/(A0 + A1)
 *
 * with A0 and A1 the area of the shape and the area of the disk, respectively.
 */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct context {
  const double* positions;
  const size_t* indices;
  size_t nsegments_interf0; /* #segments of the interface 0 */
  struct sdis_interface* interf0;
  struct sdis_interface* interf1;
};

static void
get_indices(const size_t iseg, size_t ids[2], void* context)
{
  const struct context* ctx = context;
  ids[0] = ctx->indices[iseg*2+0];
  ids[1] = ctx->indices[iseg*2+1];
}

static void
get_position(const size_t ivert, double pos[2], void* context)
{
  const struct context* ctx = context;
  pos[0] = ctx->positions[ivert*2+0];
  pos[1] = ctx->positions[ivert*2+1];
}

static void
get_interface(const size_t iseg, struct sdis_interface** bound, void* context)
{
  const struct context* ctx = context;
  *bound = iseg < ctx->nsegments_interf0 ? ctx->interf0 : ctx->interf1;
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
 * Solid medium
 ******************************************************************************/
struct solid {
  double cp;
  double lambda;
  double rho;
  double delta;
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

struct interf {
  double hc;
  double epsilon;
  double specular_fraction;
};

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->hc;
}

static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  (void)source_id;
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->epsilon;
}

static double
interface_get_specular_fraction
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  (void)source_id;
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->specular_fraction;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid0 = NULL;
  struct sdis_medium* solid1 = NULL;
  struct sdis_medium* fluid0 = NULL;
  struct sdis_medium* fluid1 = NULL;
  struct sdis_interface* solid0_fluid0 = NULL;
  struct sdis_interface* solid0_fluid1 = NULL;
  struct sdis_interface* solid1_fluid1 = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_data* data = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_estimator* estimator2 = NULL;
  struct sdis_green_function* green = NULL;
  struct fluid* fluid_param = NULL;
  struct solid* solid_param = NULL;
  struct interf* interface_param = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_solve_medium_args solve_args = SDIS_SOLVE_MEDIUM_ARGS_DEFAULT;
  struct context ctx;
  double a, a0, a1;
  double ref;
  double* positions = NULL;
  size_t* indices = NULL;
  size_t nverts;
  size_t nreals;
  size_t nfails;
  size_t i;
  int is_master_process;
  (void)argc, (void)argv;

  create_default_device(&argc, &argv, &is_master_process, &dev);

  fluid_shader.temperature = fluid_get_temperature;

  /* Create the fluid0 medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tf0;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid0));
  OK(sdis_data_ref_put(data));

  /* Create the fluid1 medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tf1;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid1));
  OK(sdis_data_ref_put(data));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid0 medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 1.0;
  solid_param->lambda = 0.1;
  solid_param->rho = 1.0;
  solid_param->delta = 1.0/20.0;
  solid_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid0));
  OK(sdis_data_ref_put(data));

  /* Create the solid1 medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 1.0;
  solid_param->lambda = 1.0;
  solid_param->rho = 1.0;
  solid_param->delta = 1.0/20.0;
  solid_param->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid1));
  OK(sdis_data_ref_put(data));

  /* Create the interfaces */
  OK(sdis_data_create(dev, sizeof(struct interf),
    ALIGNOF(struct interf), NULL, &data));
  interface_param = sdis_data_get(data);
  interface_param->hc = 0.5;
  interface_param->epsilon = 0;
  interface_param->specular_fraction = 0;
  interface_shader.convection_coef = interface_get_convection_coef;
  interface_shader.front = SDIS_INTERFACE_SIDE_SHADER_NULL;
  interface_shader.back.temperature = NULL;
  interface_shader.back.emissivity = interface_get_emissivity;
  interface_shader.back.specular_fraction = interface_get_specular_fraction;
  OK(sdis_interface_create
    (dev, solid0, fluid0, &interface_shader, data, &solid0_fluid0));
  OK(sdis_interface_create
    (dev, solid0, fluid1, &interface_shader, data, &solid0_fluid1));
  OK(sdis_interface_create
    (dev, solid1, fluid1, &interface_shader, data, &solid1_fluid1));
  OK(sdis_data_ref_put(data));

  /* Setup the square geometry */
  (void)sa_add(positions, square_nvertices*2);
  (void)sa_add(indices, square_nsegments*2);
  memcpy(positions, square_vertices, square_nvertices*sizeof(double[2]));
  memcpy(indices, square_indices, square_nsegments*sizeof(size_t[2]));

  /* Transate the square in X */
  FOR_EACH(i, 0, square_nvertices) positions[i*2] += 2;

  /* Setup a disk */
  nverts = 64;
  FOR_EACH(i, 0, nverts) {
    const double theta = (double)i * (2*PI)/(double)nverts;
    const double r = 1; /* Radius */
    const double x = cos(theta) * r - 2/* X translation */;
    const double y = sin(theta) * r + 0.5/* Y translation */;
    sa_push(positions, x);
    sa_push(positions, y);
  }
  FOR_EACH(i, 0, nverts) {
    const size_t i0 = i + square_nvertices;
    const size_t i1 = (i+1) % nverts + square_nvertices;
    /* Flip the ids to ensure that the normals point inward the disk */
    sa_push(indices, i1);
    sa_push(indices, i0);
  }

  /* Create the scene */
  ctx.positions = positions;
  ctx.indices = indices;
  ctx.nsegments_interf0 = square_nsegments;
  ctx.interf0 = solid0_fluid0;
  ctx.interf1 = solid1_fluid1;
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = sa_size(indices)/2;
  scn_args.nvertices = sa_size(positions)/2;
  scn_args.context = &ctx;
  OK(sdis_scene_2d_create(dev, &scn_args, &scn));

  OK(sdis_scene_get_medium_spread(scn, solid0, &a0));
  CHK(eq_eps(a0, 1.0, 1.e-6));
  OK(sdis_scene_get_medium_spread(scn, solid1, &a1));
  /* Rough estimation since the disk is coarsely discretized */
  CHK(eq_eps(a1, PI, 1.e-1));

  solve_args.nrealisations = N;
  solve_args.time_range[0] = INF;
  solve_args.time_range[1] = INF;

  /* Estimate the temperature of the square */
  solve_args.medium = solid0;
  OK(sdis_solve_medium(scn, &solve_args, &estimator));
  if(!is_master_process) {
    CHK(estimator == NULL);
  } else {
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    printf("Square temperature = "STR(Tf0)" ~ %g +/- %g\n", T.E, T.SE);
    printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
    printf("#failures = %lu / %lu\n\n", (unsigned long)nfails, N);
    CHK(eq_eps(T.E, Tf0, T.SE));
    CHK(nreals + nfails == N);
    OK(sdis_estimator_ref_put(estimator));
  }

  /* Estimate the temperature of the disk */
  solve_args.medium = solid1;
  OK(sdis_solve_medium(scn, &solve_args, &estimator));
  if(is_master_process) {
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    printf("Disk temperature = "STR(Tf1)" ~ %g +/- %g\n", T.E, T.SE);
    printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
    printf("#failures = %lu / %lu\n\n", (unsigned long)nfails, N);
    CHK(eq_eps(T.E, Tf1, T.SE));
    CHK(nreals + nfails == N);
    OK(sdis_estimator_ref_put(estimator));
  }

  /* Create a new scene with the same medium for the disk and the square */
  OK(sdis_scene_ref_put(scn));
  ctx.interf0 = solid0_fluid0;
  ctx.interf1 = solid0_fluid1;
  OK(sdis_scene_2d_create(dev, &scn_args, &scn));

  OK(sdis_scene_get_medium_spread(scn, solid0, &a));
  CHK(eq_eps(a, a0+a1, 1.e-6));

  /* Estimate the temperature of the square and disk shapes */
  solve_args.medium = solid1;
  solve_args.nrealisations = Np;
  BA(sdis_solve_medium(scn, &solve_args, &estimator));
  solve_args.medium = solid0;
  OK(sdis_solve_medium(scn, &solve_args, &estimator));
  if(is_master_process) {
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    ref = Tf0 * a0/a + Tf1 * a1/a;
    printf("Square + Disk temperature = %g ~ %g +/- %g\n", ref, T.E, T.SE);
    printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
    printf("#failures = %lu / %lu\n", (unsigned long)nfails, Np);
    CHK(eq_eps(T.E, ref, 3*T.SE));
    CHK(nreals + nfails == Np);
  }

  /* Solve green */
  BA(sdis_solve_medium_green_function(NULL, &solve_args, &green));
  BA(sdis_solve_medium_green_function(scn, NULL, &green));
  BA(sdis_solve_medium_green_function(scn, &solve_args, NULL));
  OK(sdis_solve_medium_green_function(scn, &solve_args, &green));

  if(!is_master_process) {
    CHK(green == NULL);
  } else {
    OK(sdis_green_function_solve(green, &estimator2));
    check_green_function(green);
    check_estimator_eq(estimator, estimator2);
    check_green_serialization(green, scn);

    OK(sdis_green_function_ref_put(green));

    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Release */
  OK(sdis_medium_ref_put(solid0));
  OK(sdis_medium_ref_put(solid1));
  OK(sdis_medium_ref_put(fluid0));
  OK(sdis_medium_ref_put(fluid1));
  OK(sdis_interface_ref_put(solid0_fluid0));
  OK(sdis_interface_ref_put(solid0_fluid1));
  OK(sdis_interface_ref_put(solid1_fluid1));
  OK(sdis_scene_ref_put(scn));

  free_default_device(dev);

  sa_release(positions);
  sa_release(indices);

  CHK(mem_allocated_size() == 0);
  return 0;
}

