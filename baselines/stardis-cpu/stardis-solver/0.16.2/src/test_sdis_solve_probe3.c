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

#include <star/s3dut.h>

/*
 * The scene is composed of a solid cube whose temperature is unknown. The
 * convection coefficient with the surrounding fluid is null. The temperature
 * is fixed at the front and back face. At the center of the cube there is a
 * solid sphere whose physical properties are the same of the solid cube; i.e.
 * the sphere influences the random walks but not the result.
 *
 *                      (1,1,1)
 *       +----------------+
 *      /'     #  #      /|
 *     +----*--------*--+ |
 *     | ' #          # | |350K
 *     | ' #          # | |
 * 300K| '  #        #  | |
 *     | +.....#..#.....|.+
 *     |/               |/
 *     +----------------+
 *   (0,0,0)
 */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct context {
  double* positions;
  size_t* indices;
  struct sdis_interface* solid_fluid_Tnone;
  struct sdis_interface* solid_fluid_T300;
  struct sdis_interface* solid_fluid_T350;
  struct sdis_interface* solid_solid;
};
static const struct context CONTEXT_NULL = {NULL, NULL, NULL, NULL, NULL, NULL};

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
  CHK(bound != NULL && context != NULL);

  if(itri == 0 || itri == 1) { /* Box front face */
    *bound = ctx->solid_fluid_T300;
  } else if(itri == 4 || itri == 5) { /* Box back face */
    *bound = ctx->solid_fluid_T350;
  } else if(itri < box_ntriangles) { /* Box remaining faces */
    *bound = ctx->solid_fluid_Tnone;
  } else { /* Faces of the internal geometry */
    *bound = ctx->solid_solid;
  }
}

/*******************************************************************************
 * Medium data
 ******************************************************************************/
static double
temperature_unknown(const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return SDIS_TEMPERATURE_NONE;
}

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL && data == NULL);
  return 2.0;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return 50.0;
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
  CHK(frag != NULL);
  (void)data;
  return 0;
}

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->temperature;
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
  struct sdis_data* data = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_estimator* estimator2 = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_interface* Tnone = NULL;
  struct sdis_interface* T300 = NULL;
  struct sdis_interface* T350 = NULL;
  struct sdis_interface* solid_solid = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = DUMMY_INTERFACE_SHADER;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct s3dut_mesh* msh = NULL;
  struct s3dut_mesh_data msh_data;
  struct context ctx = CONTEXT_NULL;
  struct interf* interface_param = NULL;
  double ref;
  const size_t N = 10000;
  size_t ntris;
  size_t nverts;
  size_t nreals;
  size_t nfails;
  size_t i;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

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

  /* Create the solid/solid interface */
  interface_shader = SDIS_INTERFACE_SHADER_NULL;
  OK(sdis_interface_create
    (dev, solid, solid, &interface_shader, NULL, &solid_solid));

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  /* Register the box geometry */
  FOR_EACH(i, 0, box_nvertices) {
    sa_push(ctx.positions, box_vertices[i*3+0]);
    sa_push(ctx.positions, box_vertices[i*3+1]);
    sa_push(ctx.positions, box_vertices[i*3+2]);
  }
  FOR_EACH(i, 0, box_ntriangles) {
    sa_push(ctx.indices, box_indices[i*3+0]);
    sa_push(ctx.indices, box_indices[i*3+1]);
    sa_push(ctx.indices, box_indices[i*3+2]);
  }

  /* Setup a sphere at the center of the box */
  OK(s3dut_create_sphere(NULL, 0.25, 64, 32, &msh));
  OK(s3dut_mesh_get_data(msh, &msh_data));
  FOR_EACH(i, 0, msh_data.nvertices) {
    sa_push(ctx.positions, msh_data.positions[i*3+0] + 0.5);
    sa_push(ctx.positions, msh_data.positions[i*3+1] + 0.5);
    sa_push(ctx.positions, msh_data.positions[i*3+2] + 0.5);
  }
  FOR_EACH(i, 0, msh_data.nprimitives) {
    sa_push(ctx.indices, msh_data.indices[i*3+0] + box_nvertices);
    sa_push(ctx.indices, msh_data.indices[i*3+1] + box_nvertices);
    sa_push(ctx.indices, msh_data.indices[i*3+2] + box_nvertices);
  }
  OK(s3dut_mesh_ref_put(msh));

  /* Create the scene */
  ctx.solid_fluid_Tnone = Tnone;
  ctx.solid_fluid_T300 = T300;
  ctx.solid_fluid_T350 = T350;
  ctx.solid_solid = solid_solid;
  nverts = sa_size(ctx.positions) / 3;
  ntris = sa_size(ctx.indices) / 3;
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = ntris;
  scn_args.nvertices = nverts;
  scn_args.context = &ctx;
  OK(sdis_scene_create(dev, &scn_args, &scn));

  /* Release the scene data */
  OK(sdis_interface_ref_put(Tnone));
  OK(sdis_interface_ref_put(T300));
  OK(sdis_interface_ref_put(T350));
  OK(sdis_interface_ref_put(solid_solid));
  sa_release(ctx.positions);
  sa_release(ctx.indices);

  /* Launch the solver */
  solve_args.nrealisations = N;
  solve_args.position[0] = 0.5;
  solve_args.position[1] = 0.5;
  solve_args.position[2] = 0.5;
  solve_args.time_range[0] = INF;
  solve_args.time_range[1] = INF;

  OK(sdis_solve_probe(scn, &solve_args, &estimator));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_time(estimator, &time));

  /* Print the estimation results */
  ref = 350 * solve_args.position[2] + (1-solve_args.position[2]) * 300;
  printf("Temperature at (%g, %g, %g) = %g ~ %g +/- %g\n",
    SPLIT3(solve_args.position), ref, T.E, T.SE);
  printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

  /* Check the results */
  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, ref, 3*T.SE));

  /* Check green function */
  OK(sdis_solve_probe_green_function(scn, &solve_args, &green));
  OK(sdis_green_function_solve(green, &estimator2));
  check_green_function(green);
  check_estimator_eq(estimator, estimator2);
  check_green_serialization(green, scn);

  /* Release data */
  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_estimator_ref_put(estimator2));
  OK(sdis_green_function_ref_put(green));
  OK(sdis_scene_ref_put(scn));
  OK(sdis_device_ref_put(dev));

  CHK(mem_allocated_size() == 0);
  return 0;

}
