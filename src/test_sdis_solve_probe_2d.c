/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

#include <rsys/math.h>

/*
 * The scene is composed of a solid square with unknown temperature. The
 * surrounding fluid has a fixed constant temperature.
 *
 *           (1,1)
 *    +-------+    _\
 *    |       |   / /
 *    |       |   \__/  300K
 *    |       |
 *    +-------+
 * (0,0)
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
get_indices(const size_t iseg, size_t ids[2], void* context)
{
  struct context* ctx = context;
  ids[0] = ctx->indices[iseg*2+0];
  ids[1] = ctx->indices[iseg*2+1];
}

static void
get_position(const size_t ivert, double pos[2], void* context)
{
  struct context* ctx = context;
  pos[0] = ctx->positions[ivert*2+0];
  pos[1] = ctx->positions[ivert*2+1];
}

static void
get_interface(const size_t iseg, struct sdis_interface** bound, void* context)
{
  struct context* ctx = context;
  (void)iseg;
  *bound = ctx->interf;
}

/*******************************************************************************
 * Media & interface
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

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)vtx, (void)data;
  return 1.0;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)vtx, (void)data;
  return 0.1;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)vtx, (void)data;
  return 1.0;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)vtx, (void)data;
  return 1.0/20.0;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)vtx, (void)data;
  return -1;
}

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  (void)frag, (void)data;
  return 0.5;
}

/*******************************************************************************
 * Main test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_data* data = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_estimator* estimator2 = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = DUMMY_INTERFACE_SHADER;
  struct context ctx;
  struct fluid* fluid_param;
  double pos[2];
  double time_range[2] = { INF, INF };
  double ref;
  const size_t N = 1000;
  size_t nreals;
  size_t nfails;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev));

  /* Create the fluid medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = 300;
  fluid_shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid));
  OK(sdis_data_ref_put(data));

  /* Create the solid medium */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));

  /* Create the solid/fluid interface */
  interface_shader.convection_coef = interface_get_convection_coef;
  interface_shader.front = SDIS_INTERFACE_SIDE_SHADER_NULL;
  interface_shader.back = SDIS_INTERFACE_SIDE_SHADER_NULL;
  OK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, NULL, &interf));

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  /* Create the scene */
  ctx.positions = square_vertices;
  ctx.indices = square_indices;
  ctx.interf = interf;
  OK(sdis_scene_2d_create(dev, square_nsegments, get_indices, get_interface,
    square_nvertices, get_position, &ctx, &scn));

  OK(sdis_interface_ref_put(interf));

  /* Test the solver */
  pos[0] = 0.5;
  pos[1] = 0.5;
  time_range[0] = time_range[1] = INF;
  OK(sdis_solve_probe(scn, N, pos, time_range, 1.0, 0, 0, 0, &estimator));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_time(estimator, &time));

  ref = 300;
  printf("Temperature at (%g, %g) = %g ~ %g +/- %g\n",
    SPLIT2(pos), ref, T.E, T.SE);
  printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, ref, T.SE));

  OK(sdis_solve_probe_green_function(scn, N, pos, 1.0, 0, 0, &green));
  OK(sdis_green_function_solve(green, time_range, &estimator2));
  check_green_function(green);
  check_estimator_eq(estimator, estimator2);

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_estimator_ref_put(estimator2));
  OK(sdis_green_function_ref_put(green));

  /* The external fluid cannot have an unknown temperature */
  fluid_param->temperature = -1;
  
  BA(sdis_solve_probe(scn, N, pos, time_range, 1.0, 0, 0, 0, &estimator));

  OK(sdis_scene_ref_put(scn));
  OK(sdis_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
