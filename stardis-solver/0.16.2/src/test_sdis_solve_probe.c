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

#include <star/ssp.h>
#include <rsys_math.h>

#include <string.h>

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
  double reference_temperature;
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

static double
interface_get_reference_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->reference_temperature;
}

/*******************************************************************************
 * Radiative environment
 ******************************************************************************/
struct radenv {
  double temperature; /* [K] */
  double reference; /* [K] */
};

static double
radenv_get_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray;
  return ((const struct radenv*)sdis_data_cget(data))->temperature;
}

static double
radenv_get_reference_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray;
  return ((const struct radenv*)sdis_data_cget(data))->reference;
}

static struct sdis_radiative_env*
create_radenv
  (struct sdis_device* dev,
   const double temperature, /* [K] */
   const double reference) /* [K] */
{
  struct sdis_radiative_env_shader shader = SDIS_RADIATIVE_ENV_SHADER_NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_data* data = NULL;
  struct radenv* radenv_args = NULL;

  OK(sdis_data_create(dev, sizeof(struct radenv), ALIGNOF(radenv), NULL, &data));
  radenv_args = sdis_data_get(data);
  radenv_args->temperature = temperature;
  radenv_args->reference = reference;

  shader.temperature = radenv_get_temperature;
  shader.reference_temperature = radenv_get_reference_temperature;
  OK(sdis_radiative_env_create(dev, &shader, data, &radenv));
  OK(sdis_data_ref_put(data));
  return radenv;
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

  BA(sdis_heat_path_get_line_strips_count(NULL, &n));
  BA(sdis_heat_path_get_line_strips_count(path, NULL));
  OK(sdis_heat_path_get_line_strips_count(path, &n));
  CHK(n == 1);

  BA(sdis_heat_path_get_status(NULL, &status));
  BA(sdis_heat_path_get_status(path, NULL));
  OK(sdis_heat_path_get_status(path, &status));
  CHK(status == SDIS_HEAT_PATH_SUCCESS || status == SDIS_HEAT_PATH_FAILURE);

  switch(status) {
    case SDIS_HEAT_PATH_FAILURE: ++ctx->nfailures; break;
    case SDIS_HEAT_PATH_SUCCESS: ++ctx->nsuccesses; break;
    default: FATAL("Unreachable code.\n"); break;
  }

  BA(sdis_heat_path_line_strip_get_vertices_count(NULL, 0, &n));
  BA(sdis_heat_path_line_strip_get_vertices_count(path, 1, &n));
  BA(sdis_heat_path_line_strip_get_vertices_count(path, 0, NULL));
  OK(sdis_heat_path_line_strip_get_vertices_count(path, 0, &n));
  CHK(n != 0);

  BA(sdis_heat_path_line_strip_get_vertex(NULL, 0, 0, &vert));
  BA(sdis_heat_path_line_strip_get_vertex(path, 1, 1, &vert));
  BA(sdis_heat_path_line_strip_get_vertex(path, 0, n, &vert));
  BA(sdis_heat_path_line_strip_get_vertex(path, 0, 0, NULL));

  FOR_EACH(i, 0, n) {
    OK(sdis_heat_path_line_strip_get_vertex(path, 0, i, &vert));
    CHK(vert.type == SDIS_HEAT_VERTEX_CONVECTION
     || vert.type == SDIS_HEAT_VERTEX_CONDUCTION
     || vert.type == SDIS_HEAT_VERTEX_RADIATIVE);
  }

  BA(sdis_heat_path_line_strip_for_each_vertex(NULL, 0, dump_vertex_pos, context));
  BA(sdis_heat_path_line_strip_for_each_vertex(path, 1, dump_vertex_pos, context));
  BA(sdis_heat_path_line_strip_for_each_vertex(path, 0, NULL, context));
  OK(sdis_heat_path_line_strip_for_each_vertex(path, 0, dump_vertex_pos, context));

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
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_data* data = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_estimator* estimator2 = NULL;
  struct sdis_estimator* estimator3 = NULL;
  struct sdis_green_function* green = NULL;
  const struct sdis_heat_path* path = NULL;
  struct sdis_device_create_args dev_args = SDIS_DEVICE_CREATE_ARGS_DEFAULT;
  struct sdis_green_function_create_from_stream_args green_args =
    SDIS_GREEN_FUNCTION_CREATE_FROM_STREAM_ARGS_DEFAULT;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct dump_path_context dump_ctx = DUMP_PATH_CONTEXT_NULL;
  struct context ctx;
  struct radenv* radenv_args;
  struct fluid* fluid_args;
  struct solid* solid_args;
  struct interf* interface_args;
  struct ssp_rng* rng_state = NULL;
  enum sdis_estimator_type type;
  FILE* stream = NULL;
  double t_range[2];
  double ref;
  const size_t N = 1000;
  const size_t N_dump = 10;
  size_t nreals;
  size_t nfails;
  size_t n;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  dev_args.allocator = &allocator;
  OK(sdis_device_create(&dev_args, &dev));

  /* Create the fluid medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_args = sdis_data_get(data);
  fluid_args->temperature = 300;
  fluid_shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid));
  OK(sdis_data_ref_put(data));

  /* Create the solid medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_args = sdis_data_get(data);
  solid_args->cp = 1.0;
  solid_args->lambda = 0.1;
  solid_args->rho = 1.0;
  solid_args->delta = 1.0/20.0;
  solid_args->temperature = SDIS_TEMPERATURE_NONE; /* Unknown temperature */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Create the solid/fluid interface */
  OK(sdis_data_create(dev, sizeof(struct interf),
    ALIGNOF(struct interf), NULL, &data));
  interface_args = sdis_data_get(data);
  interface_args->hc = 0.5;
  interface_args->epsilon = 0;
  interface_args->specular_fraction = 0;
  interface_shader.convection_coef = interface_get_convection_coef;
  interface_shader.front = SDIS_INTERFACE_SIDE_SHADER_NULL;
  interface_shader.back.temperature = NULL;
  interface_shader.back.emissivity = interface_get_emissivity;
  interface_shader.back.specular_fraction = interface_get_specular_fraction;
  interface_shader.back.reference_temperature = interface_get_reference_temperature;
  OK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, data, &interf));
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  /* Create the radiative environment */
  radenv = create_radenv(dev, SDIS_TEMPERATURE_NONE, SDIS_TEMPERATURE_NONE);
  radenv_args = sdis_data_get(sdis_radiative_env_get_data(radenv));

  /* Create the scene */
  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.interf = interf;
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = box_ntriangles;
  scn_args.nvertices = box_nvertices;
  scn_args.radenv = radenv;
  scn_args.context = &ctx;
  OK(sdis_scene_create(dev, &scn_args, &scn));

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
  solve_args.time_range[0] = solve_args.time_range[1] = -1;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
  solve_args.time_range[0] = 1;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
  solve_args.time_range[1] = 0;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
  solve_args.time_range[0] = solve_args.time_range[1] = INF;
  solve_args.picard_order = 0;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
  solve_args.picard_order = 1;
  solve_args.diff_algo = SDIS_DIFFUSION_NONE;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));
  solve_args.diff_algo = SDIS_DIFFUSION_DELTA_SPHERE;
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
    SPLIT3(solve_args.position), fluid_args->temperature, ref, T.E, T.SE);
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
  fluid_args->temperature = SDIS_TEMPERATURE_NONE;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));

  fluid_args->temperature = 300;
  OK(sdis_solve_probe(scn, &solve_args, &estimator));

  BA(sdis_solve_probe_green_function(NULL, &solve_args, &green));
  BA(sdis_solve_probe_green_function(scn, NULL, &green));
  BA(sdis_solve_probe_green_function(scn, &solve_args, NULL));
  solve_args.nrealisations = 0;
  BA(sdis_solve_probe_green_function(scn, &solve_args, &green));
  solve_args.nrealisations = N;
  OK(sdis_solve_probe_green_function(scn, &solve_args, &green));

  BA(sdis_green_function_solve(NULL, &estimator2));
  BA(sdis_green_function_solve(green, NULL));
  BA(sdis_green_function_solve(NULL, NULL));
  OK(sdis_green_function_solve(green, &estimator2));

  check_green_function(green);
  check_estimator_eq(estimator, estimator2);

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_estimator_ref_put(estimator2));
  printf("\n");

  /* Check same green used at a different temperature */
  fluid_args->temperature = 500;

  OK(sdis_solve_probe(scn, &solve_args, &estimator));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_time(estimator, &time));

  ref = 500;
  printf("Temperature at (%g, %g, %g) with Tfluid=%g = %g ~ %g +/- %g\n",
    SPLIT3(solve_args.position), fluid_args->temperature, ref, T.E, T.SE);
  printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

  CHK(nfails + nreals == N);
  CHK(nfails < N / 1000);
  CHK(eq_eps(T.E, ref, T.SE));

  OK(sdis_green_function_solve(green, &estimator2));
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
  green_args.scene = NULL;
  green_args.stream = stream;
  BA(sdis_green_function_create_from_stream(&green_args, &green));
  green_args.scene = scn;
  green_args.stream = NULL;
  BA(sdis_green_function_create_from_stream(&green_args, &green));
  green_args.scene = scn;
  green_args.stream = stream;
  BA(sdis_green_function_create_from_stream(&green_args, NULL));
  OK(sdis_green_function_create_from_stream(&green_args, &green));
  CHK(!fclose(stream));

  OK(sdis_green_function_solve(green, &estimator3));
  check_green_function(green);
  check_estimator_eq_strict(estimator2, estimator3);
  OK(sdis_green_function_ref_put(green));
  OK(sdis_estimator_ref_put(estimator3));

  CHK(stream = tmpfile());
  hash_sha256("Hello, world!", strlen("Hello, world!"), solve_args.signature);
  OK(sdis_solve_probe_green_function(scn, &solve_args, &green));
  OK(sdis_green_function_write(green, stream));
  OK(sdis_green_function_ref_put(green));

  green_args.scene = scn;
  green_args.stream = stream;
  rewind(stream);
  BA(sdis_green_function_create_from_stream(&green_args, &green));
  memcpy(green_args.signature, solve_args.signature, sizeof(hash256_T));
  rewind(stream);
  OK(sdis_green_function_create_from_stream(&green_args, &green));
  CHK(!fclose(stream));

  OK(sdis_green_function_solve(green, &estimator3));
  check_estimator_eq_strict(estimator2, estimator3);
  OK(sdis_green_function_ref_put(green));
  OK(sdis_estimator_ref_put(estimator3));

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_estimator_ref_put(estimator2));

  OK(sdis_solve_probe(scn, &solve_args, &estimator));
  BA(sdis_estimator_get_paths_count(NULL, &n));
  BA(sdis_estimator_get_paths_count(estimator, NULL));
  OK(sdis_estimator_get_paths_count(estimator, &n));
  CHK(n == 0);
  OK(sdis_estimator_ref_put(estimator));

  solve_args.nrealisations = N_dump;
  solve_args.register_paths = SDIS_HEAT_PATH_ALL;

  /* Check simulation error handling when paths are registered */
  fluid_args->temperature = SDIS_TEMPERATURE_NONE;
  BA(sdis_solve_probe(scn, &solve_args, &estimator));

  fluid_args->temperature = 300;
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

  printf("\n");

  /* Green and ambient radiative temperature */
  solve_args.nrealisations = N;
  radenv_args->temperature = 300;
  radenv_args->reference = 300;
  t_range[0] = 300;
  t_range[1] = 300;
  OK(sdis_scene_set_temperature_range(scn, t_range));

  interface_args->epsilon = 1;
  interface_args->reference_temperature = 300;

  OK(sdis_solve_probe(scn, &solve_args, &estimator));
  OK(sdis_solve_probe_green_function(scn, &solve_args, &green));
  OK(sdis_green_function_solve(green, &estimator2));

  check_green_function(green);
  check_estimator_eq(estimator, estimator2);

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_estimator_ref_put(estimator2));

  /* Check same green used at different ambient radiative temperature */
  radenv_args->temperature = 300;
  t_range[0] = 300;
  t_range[1] = 600;
  OK(sdis_scene_set_temperature_range(scn, t_range));

  OK(sdis_solve_probe(scn, &solve_args, &estimator));
  OK(sdis_green_function_solve(green, &estimator2));

  check_green_function(green);
  check_estimator_eq(estimator, estimator2);

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_estimator_ref_put(estimator2));
  OK(sdis_green_function_ref_put(green));
  OK(sdis_radiative_env_ref_put(radenv));

  OK(sdis_scene_ref_put(scn));
  OK(sdis_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

