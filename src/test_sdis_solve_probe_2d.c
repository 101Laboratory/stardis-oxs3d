/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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
static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)vtx, (void)data;
  return 300.0;
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
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = DUMMY_INTERFACE_SHADER;
  struct context ctx;
  double pos[2];
  double time;
  double ref;
  const size_t N = 1000;
  size_t nreals;
  size_t nfails;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev) == RES_OK);

  /* Create the fluid medium */
  fluid_shader.temperature = fluid_get_temperature;
  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_OK);

  /* Create the solid medium */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_OK);

  /* Create the solid/fluid interface */
  interface_shader.convection_coef = interface_get_convection_coef;
  interface_shader.front = SDIS_INTERFACE_SIDE_SHADER_NULL;
  interface_shader.back = SDIS_INTERFACE_SIDE_SHADER_NULL;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, NULL, &interf) == RES_OK);

  /* Release the media */
  CHK(sdis_medium_ref_put(solid) == RES_OK);
  CHK(sdis_medium_ref_put(fluid) == RES_OK);

  /* Create the scene */
  ctx.positions = square_vertices;
  ctx.indices = square_indices;
  ctx.interf = interf;
  CHK(sdis_scene_2d_create(dev, square_nsegments, get_indices, get_interface,
    square_nvertices, get_position, &ctx, &scn) == RES_OK);

  CHK(sdis_interface_ref_put(interf) == RES_OK);

  /* Test the solver */
  pos[0] = 0.5;
  pos[1] = 0.5;
  time = INF;
  CHK(sdis_solve_probe(scn, N, pos, time, 1.0, 0, 0, &estimator) == RES_OK);
  CHK(sdis_estimator_get_realisation_count(estimator, &nreals) == RES_OK);
  CHK(sdis_estimator_get_failure_count(estimator, &nfails) == RES_OK);

  CHK(sdis_estimator_get_temperature(estimator, &T) == RES_OK);

  ref = 300;
  printf("Temperature at (%g, %g) = %g ~ %g +/- %g\n",
    SPLIT2(pos), ref, T.E, T.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, ref, T.SE));

  CHK(sdis_estimator_ref_put(estimator) == RES_OK);

  CHK(sdis_scene_ref_put(scn) == RES_OK);
  CHK(sdis_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
