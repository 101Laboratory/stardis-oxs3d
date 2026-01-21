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
#include <rsys/stretchy_array.h>
#include <star/ssp.h>
#include <star/s3dut.h>

#include <string.h>

#define Tf0 300.0
#define Tf1 330.0
#define N 1000ul /* #realisations */
#define Np 10000ul /* #realisations precise */

/*
 * The scene is composed of 2 super shapes whose temperature is unknown. The
 * first super shape is surrounded by a fluid whose temperature is Tf0 while
 * the second one is in fluid whose temperature is Tf1. The temperatures of the
 * super shape 0 and 1 are thus uniform and equal to Tf0 and Tf1, respectively.
 *
 * This program performs 2 tests. In the first one, the super shapes 0 and 1
 * have different media; the medium solver thus estimates the
 * temperature of one super shape. In the second test, the scene is updated to
 * use the same medium for the 2 super shapes. In this case, when invoked on
 * the right medium, the estimated temperature T is equal to :
 *
 *    T = Tf0 * V0/(V0 + V1) + Tf1 * V1/(V0 + V1)
 *
 * with V0 and V1 the volume of the super shapes 0 and 1, respectively.
 */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct context {
  struct s3dut_mesh_data msh0;
  struct s3dut_mesh_data msh1;
  struct sdis_interface* interf0;
  struct sdis_interface* interf1;
};

static void
get_indices(const size_t itri, size_t ids[3], void* context)
{
  const struct context* ctx = context;
  /* Note that we swap the indices to ensure that the triangle normals point
   * inward the super shape */
  if(itri < ctx->msh0.nprimitives) {
    ids[0] = ctx->msh0.indices[itri*3+0];
    ids[2] = ctx->msh0.indices[itri*3+1];
    ids[1] = ctx->msh0.indices[itri*3+2];
  } else {
    const size_t itri2 = itri - ctx->msh0.nprimitives;
    ids[0] = ctx->msh1.indices[itri2*3+0] + ctx->msh0.nvertices;
    ids[2] = ctx->msh1.indices[itri2*3+1] + ctx->msh0.nvertices;
    ids[1] = ctx->msh1.indices[itri2*3+2] + ctx->msh0.nvertices;
  }
}

static void
get_position(const size_t ivert, double pos[3], void* context)
{
  const struct context* ctx = context;
  if(ivert < ctx->msh0.nvertices) {
    pos[0] = ctx->msh0.positions[ivert*3+0] - 2.0;
    pos[1] = ctx->msh0.positions[ivert*3+1];
    pos[2] = ctx->msh0.positions[ivert*3+2];
  } else {
    const size_t ivert2 = ivert - ctx->msh0.nvertices;
    pos[0] = ctx->msh1.positions[ivert2*3+0] + 2.0;
    pos[1] = ctx->msh1.positions[ivert2*3+1];
    pos[2] = ctx->msh1.positions[ivert2*3+2];
  }
}

static void
get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  const struct context* ctx = context;
  *bound = itri < ctx->msh0.nprimitives ? ctx->interf0 : ctx->interf1;
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
  struct s3dut_super_formula f0 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_super_formula f1 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_mesh* msh0 = NULL;
  struct s3dut_mesh* msh1 = NULL;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc T2 = SDIS_MC_NULL;
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
  struct ssp_rng* rng = NULL;
  struct context ctx;
  double ref = 0;
  double v, v0, v1;
  size_t nreals;
  size_t nfails;
  size_t ntris;
  size_t nverts;
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
  solid_param->temperature = SDIS_TEMPERATURE_NONE; /* Unknown temperature */
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
  solid_param->temperature = SDIS_TEMPERATURE_NONE; /* Unknown temperature */
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

  /* Create the mesh0 */
  f0.A = 1; f0.B = 1; f0.M = 3; f0.N0 = 1; f0.N1 = 1; f0.N2 = 2;
  f1.A = 1; f1.B = 1; f1.M = 10; f1.N0 = 1; f1.N1 = 1; f1.N2 = 3;
  OK(s3dut_create_super_shape(NULL, &f0, &f1, 1, 64, 32, &msh0));
  OK(s3dut_mesh_get_data(msh0, &ctx.msh0));

  /* Create the mesh1 */
  f0.A = 1; f0.B = 1; f0.M = 10; f0.N0 = 1; f0.N1 = 1; f0.N2 = 5;
  f1.A = 1; f1.B = 1; f1.M = 1; f1.N0 = 1; f1.N1 = 1; f1.N2 = 1;
  OK(s3dut_create_super_shape(NULL, &f0, &f1, 1, 64, 32, &msh1));
  OK(s3dut_mesh_get_data(msh1, &ctx.msh1));

  /* Create the scene */
  ctx.interf0 = solid0_fluid0;
  ctx.interf1 = solid1_fluid1;
  ntris = ctx.msh0.nprimitives + ctx.msh1.nprimitives;
  nverts = ctx.msh0.nvertices + ctx.msh1.nvertices;
#if 0
  {
    double* vertices = NULL;
    size_t* indices = NULL;
    size_t i;
    CHK(vertices = MEM_CALLOC(&allocator, nverts*3, sizeof(*vertices)));
    CHK(indices = MEM_CALLOC(&allocator, ntris*3, sizeof(*indices)));
    FOR_EACH(i, 0, ntris) get_indices(i, indices + i*3, &ctx);
    FOR_EACH(i, 0, nverts) get_position(i, vertices + i*3, &ctx);
    dump_mesh(stdout, vertices, nverts, indices, ntris);
    MEM_RM(&allocator, vertices);
    MEM_RM(&allocator, indices);
  }
#endif

  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = ntris;
  scn_args.nvertices = nverts;
  scn_args.context = &ctx;
  OK(sdis_scene_create(dev, &scn_args, &scn));

  BA(sdis_scene_get_medium_spread(NULL, solid0, &v0));
  BA(sdis_scene_get_medium_spread(scn, NULL, &v0));
  BA(sdis_scene_get_medium_spread(scn, solid0, NULL));
  OK(sdis_scene_get_medium_spread(scn, solid0, &v0));
  CHK(v0 > 0);
  OK(sdis_scene_get_medium_spread(scn, solid1, &v1));
  CHK(v1 > 0);
  OK(sdis_scene_get_medium_spread(scn, fluid0, &v));
  CHK(v == 0);
  OK(sdis_scene_get_medium_spread(scn, fluid1, &v));
  CHK(v == 0);

  solve_args.nrealisations = N;
  solve_args.medium = solid0;
  solve_args.time_range[0] = INF;
  solve_args.time_range[1] = INF;

  BA(sdis_solve_medium(NULL, &solve_args, &estimator));
  BA(sdis_solve_medium(scn, NULL, &estimator));
  BA(sdis_solve_medium(scn, &solve_args, NULL));
  solve_args.nrealisations = 0;
  BA(sdis_solve_medium(scn, &solve_args, &estimator));
  solve_args.nrealisations = N;
  solve_args.medium = NULL;
  BA(sdis_solve_medium(scn, &solve_args, &estimator));
  solve_args.medium = solid0;
  solve_args.time_range[0] = solve_args.time_range[1] = -1;
  BA(sdis_solve_medium(scn, &solve_args, &estimator));
  solve_args.time_range[0] = 1;
  BA(sdis_solve_medium(scn, &solve_args, &estimator));
  solve_args.time_range[1] = 0;
  BA(sdis_solve_medium(scn, &solve_args, &estimator));
  solve_args.time_range[0] = solve_args.time_range[1] = INF;
  OK(sdis_solve_medium(scn, &solve_args, &estimator));

  if(!is_master_process) {
    CHK(estimator == NULL);
  } else {
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));
    printf("Shape0 temperature = "STR(Tf0)" ~ %g +/- %g\n", T.E, T.SE);
    printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
    printf("#failures = %lu/%lu\n\n", (unsigned long)nfails, N);
    CHK(eq_eps(T.E, Tf0, T.SE));
    CHK(nreals + nfails == N);
    OK(sdis_estimator_ref_put(estimator));
  }

  solve_args.medium = solid1;

  /* Check simulation error handling when paths are registered */
  solve_args.nrealisations = 10;
  solve_args.register_paths = SDIS_HEAT_PATH_ALL;
  fluid_param->temperature = SDIS_TEMPERATURE_NONE;
  BA(sdis_solve_medium(scn, &solve_args, &estimator));
  fluid_param->temperature = Tf1;
  OK(sdis_solve_medium(scn, &solve_args, &estimator));
  if(is_master_process) {
    OK(sdis_estimator_ref_put(estimator));
  }
  solve_args.nrealisations = N;
  solve_args.register_paths = SDIS_HEAT_PATH_NONE;

  OK(sdis_solve_medium(scn, &solve_args, &estimator));
  if(is_master_process) {
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));
    printf("Shape1 temperature = "STR(Tf1)" ~ %g +/- %g\n", T.E, T.SE);
    printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
    printf("#failures = %lu/%lu\n\n", (unsigned long)nfails, N);
    CHK(eq_eps(T.E, Tf1, T.SE));
    CHK(nreals + nfails == N);
    OK(sdis_estimator_ref_put(estimator));
  }

  /* Create a new scene with the same medium in the 2 super shapes */
  OK(sdis_scene_ref_put(scn));
  ctx.interf0 = solid0_fluid0;
  ctx.interf1 = solid0_fluid1;
  OK(sdis_scene_create(dev, &scn_args, &scn));

  OK(sdis_scene_get_medium_spread(scn, solid0, &v));
  CHK(eq_eps(v, v0+v1, 1.e-6));

  solve_args.medium = solid1;
  BA(sdis_solve_medium(scn, &solve_args, &estimator));
  solve_args.medium = solid0;
  solve_args.nrealisations = Np;
  OK(sdis_solve_medium(scn, &solve_args, &estimator));
  if(is_master_process) {
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    ref = Tf0 * v0/v + Tf1 * v1/v;
    printf("Shape0 + Shape1 temperature = %g ~ %g +/- %g\n", ref, T.E, T.SE);
    printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, Np);
    CHK(eq_eps(T.E, ref, T.SE*3));
  }

  /* Check RNG type */
  solve_args.rng_state = NULL;
  solve_args.rng_type = SSP_RNG_TYPE_NULL;
  BA(sdis_solve_medium(scn, &solve_args, &estimator2));
  solve_args.rng_type =
    SDIS_SOLVE_MEDIUM_ARGS_DEFAULT.rng_type == SSP_RNG_THREEFRY
    ? SSP_RNG_MT19937_64 : SSP_RNG_THREEFRY;
  OK(sdis_solve_medium(scn, &solve_args, &estimator2));
  if(is_master_process) {
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(eq_eps(T2.E, ref, 3*T2.SE));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Check RNG state */
  OK(ssp_rng_create(NULL, SSP_RNG_THREEFRY, &rng));
  OK(ssp_rng_discard(rng, 31415926535)); /* Move the RNG state  */
  solve_args.rng_state = rng;
  solve_args.rng_type = SSP_RNG_TYPE_NULL;
  OK(sdis_solve_medium(scn, &solve_args, &estimator2));
  OK(ssp_rng_ref_put(rng));
  if(is_master_process) {
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(eq_eps(T2.E, ref, 3*T2.SE));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Restore args */
  solve_args.rng_state = SDIS_SOLVE_PROBE_ARGS_DEFAULT.rng_state;
  solve_args.rng_type = SDIS_SOLVE_PROBE_ARGS_DEFAULT.rng_type;



  /* Solve green */
  BA(sdis_solve_medium_green_function(NULL, &solve_args, &green));
  BA(sdis_solve_medium_green_function(scn, NULL, &green));
  BA(sdis_solve_medium_green_function(scn, &solve_args, NULL));
  solve_args.nrealisations = 0;
  BA(sdis_solve_medium_green_function(scn, &solve_args, &green));
  solve_args.nrealisations = Np;
  solve_args.medium = NULL;
  BA(sdis_solve_medium_green_function(scn, &solve_args, &green));
  solve_args.medium = solid1;
  BA(sdis_solve_medium_green_function(scn, &solve_args, &green));
  solve_args.medium = solid0;
  solve_args.picard_order = 0;
  BA(sdis_solve_medium_green_function(scn, &solve_args, &green));
  solve_args.picard_order = 1;
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
  OK(s3dut_mesh_ref_put(msh0));
  OK(s3dut_mesh_ref_put(msh1));
  OK(sdis_medium_ref_put(fluid0));
  OK(sdis_medium_ref_put(fluid1));
  OK(sdis_medium_ref_put(solid0));
  OK(sdis_medium_ref_put(solid1));
  OK(sdis_interface_ref_put(solid0_fluid0));
  OK(sdis_interface_ref_put(solid0_fluid1));
  OK(sdis_interface_ref_put(solid1_fluid1));
  OK(sdis_scene_ref_put(scn));
  free_default_device(dev);

  CHK(mem_allocated_size() == 0);
  return 0;
}
