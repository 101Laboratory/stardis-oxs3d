/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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

#include <star/ssp.h>
#include <rsys/math.h>

/*
 * The scene is composed of a solid cube with unknown temperature. The
 * surrounding fluid has a fixed constant temperature.
 *
 *             (1,1,1)
 *       +-------+
 *      /'      /|    _\
 *     +-------+ |   / /
 *     | +.....|.+   \__/
 *     |,      |/
 *     +-------+
 * (0,0,0)
 */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct context {
  const double* positions;
  const size_t* indices;
  struct sdis_interface* interf;
};

static void
get_indices(const size_t itri, size_t ids[3], void* context)
{
  struct context* ctx = context;
  ids[0] = ctx->indices[itri*3+0];
  ids[1] = ctx->indices[itri*3+1];
  ids[2] = ctx->indices[itri*3+2];
}

static void
get_position(const size_t ivert, double pos[3], void* context)
{
  struct context* ctx = context;
  pos[0] = ctx->positions[ivert*3+0];
  pos[1] = ctx->positions[ivert*3+1];
  pos[2] = ctx->positions[ivert*3+2];
}

static void
get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct context* ctx = context;
  (void)itri;
  *bound = ctx->interf;
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

/*******************************************************************************
 * Interface
 ******************************************************************************/
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
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->epsilon;
}

static double
interface_get_specular_fraction
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->specular_fraction;
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
struct dump_path_context {
  FILE* stream;
  size_t offset;
  size_t nfailures;
  size_t nsuccesses;
};
static const struct dump_path_context DUMP_PATH_CONTEXT_NULL = {NULL, 0, 0, 0};

static res_T
dump_vertex_pos(const struct sdis_heat_vertex* vert, void* context)
{
  struct dump_path_context* ctx = context;
  CHK(vert && context);
  fprintf(ctx->stream, "v %g %g %g\n", SPLIT3(vert->P));
  return RES_OK;
}

static res_T
process_heat_path(const struct sdis_heat_path* path, void* context)
{
  struct dump_path_context* ctx = context;
  struct sdis_heat_vertex vert = SDIS_HEAT_VERTEX_NULL;
  enum sdis_heat_path_flag status = SDIS_HEAT_PATH_NONE;
  size_t i;
  size_t n;
  (void)context;

  CHK(path && context);

  BA(sdis_heat_path_get_vertices_count(NULL, &n));
  BA(sdis_heat_path_get_vertices_count(path, NULL));
  OK(sdis_heat_path_get_vertices_count(path, &n));
  CHK(n != 0);

  BA(sdis_heat_path_get_status(NULL, &status));
  BA(sdis_heat_path_get_status(path, NULL));
  OK(sdis_heat_path_get_status(path, &status));
  CHK(status == SDIS_HEAT_PATH_SUCCESS || status == SDIS_HEAT_PATH_FAILURE);

  switch(status) {
    case SDIS_HEAT_PATH_FAILURE: ++ctx->nfailures; break;
    case SDIS_HEAT_PATH_SUCCESS: ++ctx->nsuccesses; break;
    default: FATAL("Unreachable code.\n"); break;
  }

  BA(sdis_heat_path_get_vertex(NULL, 0, &vert));
  BA(sdis_heat_path_get_vertex(path, n, &vert));
  BA(sdis_heat_path_get_vertex(path, 0, NULL));

  FOR_EACH(i, 0, n) {
    OK(sdis_heat_path_get_vertex(path, i, &vert));
    CHK(vert.type == SDIS_HEAT_VERTEX_CONVECTION
     || vert.type == SDIS_HEAT_VERTEX_CONDUCTION
     || vert.type == SDIS_HEAT_VERTEX_RADIATIVE);
  }

  BA(sdis_heat_path_for_each_vertex(NULL, dump_vertex_pos, context));
  BA(sdis_heat_path_for_each_vertex(path, NULL, context));
  OK(sdis_heat_path_for_each_vertex(path, dump_vertex_pos, context));

  FOR_EACH(i, 0, n-1) {
    fprintf(ctx->stream, "l %lu %lu\n",
      (unsigned long)(i+1 + ctx->offset),
      (unsigned long)(i+2 + ctx->offset));
  }

  ctx->offset += n;

  return RES_OK;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc F = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_data* data = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_estimator* estimator2 = NULL;
  struct sdis_estimator* estimator3 = NULL;
  struct sdis_green_function* green = NULL;
  const struct sdis_heat_path* path = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct dump_path_context dump_ctx = DUMP_PATH_CONTEXT_NULL;
  struct context ctx;
  struct fluid* fluid_param;
  struct solid* solid_param;
  struct interf* interface_param;
  struct ssp_rng* rng_state = NULL;
  enum sdis_estimator_type type;
  FILE* stream = NULL;
  double ref;
  const size_t N = 1000;
  const size_t N_dump = 10;
  size_t nreals;
  size_t nfails;
  size_t n;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 0, &dev));

  /* Create the fluid medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = 300;
  fluid_shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid));
  OK(sdis_data_ref_put(data));

  /* Create the solid medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 1.0;
  solid_param->lambda = 0.1;
  solid_param->rho = 1.0;
  solid_param->delta = 1.0/20.0;
  solid_param->temperature = -1; /* Unknown temperature */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Create the solid/fluid interface */
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
    (dev, solid, fluid, &interface_shader, data, &interf));
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  /* Create the scene */
  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.interf = interf;
  OK(sdis_scene_create(dev, box_ntriangles, get_indices, get_interface,
    box_nvertices, get_position, &ctx, &scn));

  OK(sdis_interface_ref_put(interf));

  /* Test the solver */
  solve_args.nrealisations = N;
  solve_args.position[0] = 0.5;
  solve_args.position[1] = 0.5;
  solve_args.position[2] = 0.5;
  solve_args.time_range[0] = INF;
  solve_args.time_range[1] = INF;

  BA(sdis_solve_probe(NULL, &solve_args, &estimator));
  BA(sdis_solve_probe(scn, NULL, &estimator));
  BA(sdis_solve_probe(scn, &solve_args, NULL));
  solve_args.nrealisations = 0;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
  solve_args.nrealisations = N;
  solve_args.fp_to_meter = 0;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
  solve_args.fp_to_meter = 1;
  solve_args.time_range[0] = solve_args.time_range[1] = -1;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
  solve_args.time_range[0] = 1;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
  solve_args.time_range[1] = 0;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
  solve_args.time_range[0] = solve_args.time_range[1] = INF;
  OK(sdis_solve_probe(scn, &solve_args, &estimator));

  BA(sdis_estimator_get_type(estimator, NULL));
  BA(sdis_estimator_get_type(NULL, &type));
  OK(sdis_estimator_get_type(estimator, &type));
  CHK(type == SDIS_ESTIMATOR_TEMPERATURE);

  /* Fluxes aren't available after sdis_solve_probe */
  BA(sdis_estimator_get_convective_flux(estimator, NULL));
  BA(sdis_estimator_get_convective_flux(NULL, &F));
  BA(sdis_estimator_get_convective_flux(estimator, &F));

  BA(sdis_estimator_get_radiative_flux(estimator, NULL));
  BA(sdis_estimator_get_radiative_flux(NULL, &F));
  BA(sdis_estimator_get_radiative_flux(estimator, &F));

  BA(sdis_estimator_get_total_flux(estimator, NULL));
  BA(sdis_estimator_get_total_flux(NULL, &F));
  BA(sdis_estimator_get_total_flux(estimator, &F));

  BA(sdis_estimator_get_realisation_count(estimator, NULL));
  BA(sdis_estimator_get_realisation_count(NULL, &nreals));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));

  BA(sdis_estimator_get_failure_count(estimator, NULL));
  BA(sdis_estimator_get_failure_count(NULL, &nfails));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));

  BA(sdis_estimator_get_temperature(estimator, NULL));
  BA(sdis_estimator_get_temperature(NULL, &T));
  OK(sdis_estimator_get_temperature(estimator, &T));

  BA(sdis_estimator_get_realisation_time(estimator, NULL));
  BA(sdis_estimator_get_realisation_time(NULL, &time));
  OK(sdis_estimator_get_realisation_time(estimator, &time));

  BA(sdis_estimator_get_rng_state(NULL, &rng_state));
  BA(sdis_estimator_get_rng_state(estimator, NULL));
  OK(sdis_estimator_get_rng_state(estimator, &rng_state));

  ref = 300;
  printf("Temperature at (%g, %g, %g) with Tfluid=%g = %g ~ %g +/- %g\n",
    SPLIT3(solve_args.position), fluid_param->temperature, ref, T.E, T.SE);
  printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, ref, T.SE));

  BA(sdis_estimator_ref_get(NULL));
  OK(sdis_estimator_ref_get(estimator));
  BA(sdis_estimator_ref_put(NULL));
  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_estimator_ref_put(estimator));

  /* The external fluid cannot have an unknown temperature */
  fluid_param->temperature = -1;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));

  fluid_param->temperature = 300;
  OK(sdis_solve_probe(scn, &solve_args, &estimator));

  BA(sdis_solve_probe_green_function(NULL, &solve_args, &green));
  BA(sdis_solve_probe_green_function(scn, NULL, &green));
  BA(sdis_solve_probe_green_function(scn, &solve_args, NULL));
  solve_args.nrealisations = 0;
  BA(sdis_solve_probe_green_function(scn, &solve_args, &green));
  solve_args.nrealisations = N;
  solve_args.fp_to_meter = 0;
  BA(sdis_solve_probe_green_function(scn, &solve_args, &green));
  solve_args.fp_to_meter = 1;
  OK(sdis_solve_probe_green_function(scn, &solve_args, &green));

  BA(sdis_green_function_solve(NULL, solve_args.time_range, &estimator2));
  BA(sdis_green_function_solve(green, NULL, &estimator2));
  BA(sdis_green_function_solve(green, solve_args.time_range, NULL));
  OK(sdis_green_function_solve(green, solve_args.time_range, &estimator2));

  check_green_function(green);
  check_estimator_eq(estimator, estimator2);

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_estimator_ref_put(estimator2));
  printf("\n");

  /* Check green used at a different temperature */
  fluid_param->temperature = 500;
  OK(sdis_solve_probe(scn, &solve_args, &estimator));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_time(estimator, &time));

  ref = 500;
  printf("Temperature at (%g, %g, %g) with Tfluid=%g = %g ~ %g +/- %g\n",
    SPLIT3(solve_args.position), fluid_param->temperature, ref, T.E, T.SE);
  printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

  CHK(nfails + nreals == N);
  CHK(nfails < N / 1000);
  CHK(eq_eps(T.E, ref, T.SE));

  OK(sdis_green_function_solve(green, solve_args.time_range, &estimator2));
  check_green_function(green);
  check_estimator_eq(estimator, estimator2);

  stream = tmpfile();
  CHK(stream);
  BA(sdis_green_function_write(NULL, stream));
  BA(sdis_green_function_write(green, NULL));
  OK(sdis_green_function_write(green, stream));

  BA(sdis_green_function_ref_get(NULL));
  OK(sdis_green_function_ref_get(green));
  BA(sdis_green_function_ref_put(NULL));
  OK(sdis_green_function_ref_put(green));
  OK(sdis_green_function_ref_put(green));

  rewind(stream);
  BA(sdis_green_function_create_from_stream(NULL, stream, &green));
  BA(sdis_green_function_create_from_stream(scn, NULL, &green));
  BA(sdis_green_function_create_from_stream(scn, stream, NULL));
  OK(sdis_green_function_create_from_stream(scn, stream, &green));
  CHK(!fclose(stream));

  OK(sdis_green_function_solve(green, solve_args.time_range, &estimator3));

  check_green_function(green);
  check_estimator_eq_strict(estimator2, estimator3);

  OK(sdis_green_function_ref_put(green));

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_estimator_ref_put(estimator2));
  OK(sdis_estimator_ref_put(estimator3));

  OK(sdis_solve_probe(scn, &solve_args, &estimator));
  BA(sdis_estimator_get_paths_count(NULL, &n));
  BA(sdis_estimator_get_paths_count(estimator, NULL));
  OK(sdis_estimator_get_paths_count(estimator, &n));
  CHK(n == 0);
  OK(sdis_estimator_ref_put(estimator));

  solve_args.nrealisations = N_dump;
  solve_args.register_paths = SDIS_HEAT_PATH_ALL;

  /* Check simulation error handling when paths are registered */
  fluid_param->temperature = -1;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));

  fluid_param->temperature = 300;
  OK(sdis_solve_probe(scn, &solve_args, &estimator));
  OK(sdis_estimator_get_paths_count(estimator, &n));
  CHK(n == N_dump);

  BA(sdis_estimator_get_path(NULL, 0, &path));
  BA(sdis_estimator_get_path(estimator, n, &path));
  BA(sdis_estimator_get_path(estimator, 0, NULL));
  OK(sdis_estimator_get_path(estimator, 0, &path));

  dump_ctx.stream = stderr;
  BA(sdis_estimator_for_each_path(NULL, process_heat_path, &dump_ctx));
  BA(sdis_estimator_for_each_path(estimator, NULL, &dump_ctx));
  OK(sdis_estimator_for_each_path(estimator, process_heat_path, &dump_ctx));

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_scene_ref_put(scn));
  OK(sdis_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

