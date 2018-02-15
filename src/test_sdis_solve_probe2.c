/* Copyright (C) |Meso|Star> 2016-2018 (contact@meso-star.com)
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
  CHK(vtx != NULL);
  return -1;
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

static double
solid_get_delta_boundary
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return 2.1/20.0;
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
  struct mem_allocator allocator;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_device* dev = NULL;
  struct sdis_data* data = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_interface* Tnone = NULL;
  struct sdis_interface* T300 = NULL;
  struct sdis_interface* T350 = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = DUMMY_INTERFACE_SHADER;
  struct sdis_interface* interfaces[12];
  struct context ctx;
  struct interf* interface_param = NULL;
  double pos[3];
  double time;
  double ref;
  const size_t N = 10000;
  size_t nreals;
  size_t nfails;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 0, &dev) == RES_OK);

  /* Create the fluid medium */
  fluid_shader.temperature = temperature_unknown;
  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_OK);

  /* Create the solid medium */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.delta_boundary = solid_get_delta_boundary;
  solid_shader.temperature = temperature_unknown;
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_OK);

  /* Create the fluid/solid interface with no limit conidition */
  interface_shader.convection_coef = null_interface_value;
  interface_shader.temperature = NULL;
  interface_shader.emissivity = null_interface_value;
  interface_shader.specular_fraction = null_interface_value;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, NULL, &Tnone) == RES_OK);

  /* Create the fluid/solid interface with a fixed temperature of 300K */
  CHK(sdis_data_create(dev, sizeof(struct interf),
    ALIGNOF(struct interf), NULL, &data) == RES_OK);
  interface_param = sdis_data_get(data);
  interface_param->temperature = 300;
  interface_shader.convection_coef = null_interface_value;
  interface_shader.temperature = interface_get_temperature;
  interface_shader.emissivity = null_interface_value;
  interface_shader.specular_fraction = null_interface_value;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, data, &T300) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the fluid/solid interface with a fixed temperature of 350K */
  CHK(sdis_data_create(dev, sizeof(struct interf),
    ALIGNOF(struct interf), NULL, &data) == RES_OK);
  interface_param = sdis_data_get(data);
  interface_param->temperature = 350;
  interface_shader.convection_coef = null_interface_value;
  interface_shader.temperature = interface_get_temperature;
  interface_shader.emissivity = null_interface_value;
  interface_shader.specular_fraction = null_interface_value;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, data, &T350) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Release the media */
  CHK(sdis_medium_ref_put(solid) == RES_OK);
  CHK(sdis_medium_ref_put(fluid) == RES_OK);

  /* Setup the per primitive scene interfaces */
  CHK(sizeof(interfaces)/sizeof(struct sdis_interface*) == box_ntriangles);
  interfaces[0] = interfaces[1] = T300; /* Front face */
  interfaces[2] = interfaces[3] = Tnone; /* Left face */
  interfaces[4] = interfaces[5] = T350; /* Back face */
  interfaces[6] = interfaces[7] = Tnone; /* Right face */
  interfaces[8] = interfaces[9] = Tnone; /* Top face */
  interfaces[10] = interfaces[11] = Tnone; /* Bottom face */

  /* Create the scene */
  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.interfaces = interfaces;
  CHK(sdis_scene_create(dev, box_ntriangles, get_indices, get_interface,
    box_nvertices, get_position, &ctx, &scn) == RES_OK);

  /* Release the interfaces */
  CHK(sdis_interface_ref_put(Tnone) == RES_OK);
  CHK(sdis_interface_ref_put(T300) == RES_OK);
  CHK(sdis_interface_ref_put(T350) == RES_OK);

  /* Launch the solver */
  pos[0] = 0.5;
  pos[1] = 0.5;
  pos[2] = 0.5;
  time = INF;
  CHK(sdis_solve_probe( scn, N, pos, time, 1.0, -1, 0, &estimator) == RES_OK);
  CHK(sdis_estimator_get_realisation_count(estimator, &nreals) == RES_OK);
  CHK(sdis_estimator_get_failure_count(estimator, &nfails) == RES_OK);
  CHK(sdis_estimator_get_temperature(estimator, &T) == RES_OK);

  /* Print the estimation results */
  ref = 350 * pos[2] + (1-pos[2]) * 300;
  printf("Temperature at (%g, %g, %g) = %g ~ %g +/- %g\n",
    SPLIT3(pos), ref, T.E, T.SE);
  printf("#realisations: %lu; #failures: %lu\n",
    (unsigned long)nreals, (unsigned long)nfails);

  /* Check the results */
  CHK(nfails + nreals == N);
  CHK(eq_eps(T.E, ref, T.SE));

  /* Release data */
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);
  CHK(sdis_scene_ref_put(scn) == RES_OK);
  CHK(sdis_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
