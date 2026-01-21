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
 * The scene is composed of a solid cube whose temperature is unknown. The
 * convection coefficient with the surrounding fluid is null. The temperature
 * is fixed at the front and back face.
 *
 *             (1,1,1)
 *       +-------+
 *      /'      /|350K
 *     +-------+ |
 *     | +.....|.+
 * 300K|,      |/
 *     +-------+
 * (0,0,0)
 */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct context {
  const double* positions;
  const size_t* indices;
  struct sdis_interface** interfaces; /* Per primitive interfaces */
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
  *bound = ctx->interfaces[itri];
}

/*******************************************************************************
 * Medium data
 ******************************************************************************/
static double
temperature_unknown(const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL && IS_INF(vtx->time));
  return SDIS_TEMPERATURE_NONE;
}

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL && IS_INF(vtx->time) && data == NULL);
  CHK(IS_INF(vtx->time));
  return 2.0;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL && IS_INF(vtx->time));
  return 50.0;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL && IS_INF(vtx->time));
  return 25.0;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL && IS_INF(vtx->time));
  return 1.0/20.0;
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
struct interf {
  double temperature;
};

static double
null_interface_value
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(frag != NULL && IS_INF(frag->time));
  (void)data;
  return 0;
}

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL && IS_INF(frag->time));
  return ((const struct interf*)sdis_data_cget(data))->temperature;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc T2 = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  struct sdis_device* dev = NULL;
  struct sdis_data* data = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_estimator* estimator2 = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_interface* Tnone = NULL;
  struct sdis_interface* T300 = NULL;
  struct sdis_interface* T350 = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = DUMMY_INTERFACE_SHADER;
  struct sdis_interface* interfaces[12];
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct ssp_rng* rng = NULL;
  struct context ctx;
  struct interf* interface_param = NULL;
  double ref;
  const size_t N = 10000;
  size_t nreals;
  size_t nfails;
  int is_master_process;
  (void)argc, (void)argv;

  create_default_device(&argc, &argv, &is_master_process, &dev);

  /* Create the fluid medium */
  fluid_shader.temperature = temperature_unknown;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Create the solid medium */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = temperature_unknown;
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));

  /* Create the fluid/solid interface with no limit conidition */
  interface_shader.convection_coef = null_interface_value;
  interface_shader.front = SDIS_INTERFACE_SIDE_SHADER_NULL;
  interface_shader.back = SDIS_INTERFACE_SIDE_SHADER_NULL;
  OK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, NULL, &Tnone));

  /* Create the fluid/solid interface with a fixed temperature of 300K */
  OK(sdis_data_create(dev, sizeof(struct interf),
    ALIGNOF(struct interf), NULL, &data));
  interface_param = sdis_data_get(data);
  interface_param->temperature = 300;
  interface_shader.front.temperature = interface_get_temperature;
  OK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, data, &T300));
  OK(sdis_data_ref_put(data));

  /* Create the fluid/solid interface with a fixed temperature of 350K */
  OK(sdis_data_create(dev, sizeof(struct interf),
    ALIGNOF(struct interf), NULL, &data));
  interface_param = sdis_data_get(data);
  interface_param->temperature = 350;
  OK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, data, &T350));
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  /* Setup the per primitive scene interfaces */
  CHK(sizeof(interfaces)/sizeof(struct sdis_interface*) == box_ntriangles);
  interfaces[0] = interfaces[1] = T300; /* Back face */
  interfaces[2] = interfaces[3] = Tnone; /* Left face */
  interfaces[4] = interfaces[5] = T350; /* Front face */
  interfaces[6] = interfaces[7] = Tnone; /* Right face */
  interfaces[8] = interfaces[9] = Tnone; /* Top face */
  interfaces[10] = interfaces[11] = Tnone; /* Bottom face */

  /* Create the scene */
  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.interfaces = interfaces;
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = box_ntriangles;
  scn_args.nvertices = box_nvertices;
  scn_args.context = &ctx;
  OK(sdis_scene_create(dev, &scn_args, &scn));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(Tnone));
  OK(sdis_interface_ref_put(T300));
  OK(sdis_interface_ref_put(T350));

  /* Launch the solver */
  solve_args.nrealisations = N;
  solve_args.position[0] = 0.5;
  solve_args.position[1] = 0.5;
  solve_args.position[2] = 0.5;
  solve_args.time_range[0] = INF;
  solve_args.time_range[1] = INF;
  solve_args.diff_algo = SDIS_DIFFUSION_WOS;
  OK(sdis_solve_probe(scn, &solve_args, &estimator));

  ref = 350 * solve_args.position[2] + (1-solve_args.position[2]) * 300;
  if(!is_master_process) {
    CHK(estimator == NULL);
  } else {
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    /* Print the estimation results */
    printf("Temperature at (%g, %g, %g) = %g ~ %g +/- %g\n",
      SPLIT3(solve_args.position), ref, T.E, T.SE);
    printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

    /* Check the results */
    CHK(nfails + nreals == N);
    CHK(nfails < N/1000);
    CHK(eq_eps(T.E, ref, 3*T.SE));
  }

  /* Check RNG type */
  solve_args.rng_state = NULL;
  solve_args.rng_type = SSP_RNG_TYPE_NULL;
  BA(sdis_solve_probe(scn, &solve_args, &estimator2));
  solve_args.rng_type =
    SDIS_SOLVE_PROBE_ARGS_DEFAULT.rng_type == SSP_RNG_THREEFRY
    ? SSP_RNG_MT19937_64 : SSP_RNG_THREEFRY;
  OK(sdis_solve_probe(scn, &solve_args, &estimator2));
  if(is_master_process) {
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(eq_eps(T2.E, ref, 3*T2.SE));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Check the RNG state */
  OK(ssp_rng_create(NULL, SSP_RNG_THREEFRY, &rng));
  OK(ssp_rng_discard(rng, 314159265358979)); /* Move the RNG state  */
  solve_args.rng_state = rng;
  solve_args.rng_type = SSP_RNG_TYPE_NULL;
  OK(sdis_solve_probe(scn, &solve_args, &estimator2));
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

  /* Check green */
  OK(sdis_solve_probe_green_function(scn, &solve_args, &green));
  if(!is_master_process) {
    CHK(green == NULL);
  } else {
    OK(sdis_green_function_solve(green, &estimator2));
    check_green_function(green);
    check_estimator_eq(estimator, estimator2);
    check_green_serialization(green, scn);
  }

  /* Release data */
  if(estimator) OK(sdis_estimator_ref_put(estimator));
  if(estimator2) OK(sdis_estimator_ref_put(estimator2));
  if(green) OK(sdis_green_function_ref_put(green));
  OK(sdis_scene_ref_put(scn));
  free_default_device(dev);

  CHK(mem_allocated_size() == 0);

  return 0;
}
