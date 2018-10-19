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
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc F = SDIS_MC_NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_data* data = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = SDIS_INTERFACE_SHADER_NULL;
  struct context ctx;
  struct fluid* fluid_param;
  struct solid* solid_param;
  struct interf* interface_param;
  enum sdis_estimator_type type;
  double pos[3];
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
  CHK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data) == RES_OK);
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = 300;
  fluid_shader.temperature = fluid_get_temperature;
  CHK(sdis_fluid_create(dev, &fluid_shader, data, &fluid) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the solid medium */
  CHK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data) == RES_OK);
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
  CHK(sdis_solid_create(dev, &solid_shader, data, &solid) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the solid/fluid interface */
  CHK(sdis_data_create(dev, sizeof(struct interf),
    ALIGNOF(struct interf), NULL, &data) == RES_OK);
  interface_param = sdis_data_get(data);
  interface_param->hc = 0.5;
  interface_param->epsilon = 0;
  interface_param->specular_fraction = 0;
  interface_shader.convection_coef = interface_get_convection_coef;
  interface_shader.front = SDIS_INTERFACE_SIDE_SHADER_NULL;
  interface_shader.back.temperature = NULL;
  interface_shader.back.emissivity = interface_get_emissivity;
  interface_shader.back.specular_fraction = interface_get_specular_fraction;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, data, &interf) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Release the media */
  CHK(sdis_medium_ref_put(solid) == RES_OK);
  CHK(sdis_medium_ref_put(fluid) == RES_OK);

  /* Create the scene */
  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.interf = interf;
  CHK(sdis_scene_create(dev, box_ntriangles, get_indices, get_interface,
    box_nvertices, get_position, &ctx, &scn) == RES_OK);

  CHK(sdis_interface_ref_put(interf) == RES_OK);

  /* Test the solver */
  pos[0] = 0.5;
  pos[1] = 0.5;
  pos[2] = 0.5;
  time = INF;
  CHK(sdis_solve_probe(NULL, N, pos, time, 1.0, 0, 0, &estimator) == RES_BAD_ARG);
  CHK(sdis_solve_probe(scn, 0, pos, time, 1.0, 0, 0, &estimator) == RES_BAD_ARG);
  CHK(sdis_solve_probe(scn, N, NULL, time, 1.0, 0, 0, &estimator) == RES_BAD_ARG);
  CHK(sdis_solve_probe(scn, N, pos, time, 0, 0, 0, &estimator) == RES_BAD_ARG);
  CHK(sdis_solve_probe(scn, N, pos, time, 0, 0, -1, &estimator) == RES_BAD_ARG);
  CHK(sdis_solve_probe(scn, N, pos, time, 1.0, 0, 0, NULL) == RES_BAD_ARG);
  CHK(sdis_solve_probe(scn, N, pos, time, 1.0, 0, 0, &estimator) == RES_OK);

  CHK(sdis_estimator_get_type(estimator, NULL) == RES_BAD_ARG);
  CHK(sdis_estimator_get_type(NULL, &type) == RES_BAD_ARG);
  CHK(sdis_estimator_get_type(estimator, &type) == RES_OK);
  CHK(type == SDIS_TEMPERATURE_ESTIMATOR);

  /* Fluxes aren't available after sdis_solve_probe */
  CHK(sdis_estimator_get_convective_flux(estimator, NULL) == RES_BAD_ARG);
  CHK(sdis_estimator_get_convective_flux(NULL, &F) == RES_BAD_ARG);
  CHK(sdis_estimator_get_convective_flux(estimator, &F) == RES_BAD_ARG);

  CHK(sdis_estimator_get_radiative_flux(estimator, NULL) == RES_BAD_ARG);
  CHK(sdis_estimator_get_radiative_flux(NULL, &F) == RES_BAD_ARG);
  CHK(sdis_estimator_get_radiative_flux(estimator, &F) == RES_BAD_ARG);

  CHK(sdis_estimator_get_total_flux(estimator, NULL) == RES_BAD_ARG);
  CHK(sdis_estimator_get_total_flux(NULL, &F) == RES_BAD_ARG);
  CHK(sdis_estimator_get_total_flux(estimator, &F) == RES_BAD_ARG);

  CHK(sdis_estimator_get_realisation_count(estimator, NULL) == RES_BAD_ARG);
  CHK(sdis_estimator_get_realisation_count(NULL, &nreals) == RES_BAD_ARG);
  CHK(sdis_estimator_get_realisation_count(estimator, &nreals) == RES_OK);

  CHK(sdis_estimator_get_failure_count(estimator, NULL) == RES_BAD_ARG);
  CHK(sdis_estimator_get_failure_count(NULL, &nfails) == RES_BAD_ARG);
  CHK(sdis_estimator_get_failure_count(estimator, &nfails) == RES_OK);

  CHK(sdis_estimator_get_temperature(estimator, NULL) == RES_BAD_ARG);
  CHK(sdis_estimator_get_temperature(NULL, &T) == RES_BAD_ARG);
  CHK(sdis_estimator_get_temperature(estimator, &T) == RES_OK);

  ref = 300;
  printf("Temperature at (%g, %g, %g) = %g ~ %g +/- %g\n",
    SPLIT3(pos), ref, T.E, T.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, ref, T.SE));

  CHK(sdis_estimator_ref_get(NULL) == RES_BAD_ARG);
  CHK(sdis_estimator_ref_get(estimator) == RES_OK);
  CHK(sdis_estimator_ref_put(NULL) == RES_BAD_ARG);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);

  /* The external fluid cannot have an unknown temperature */
  fluid_param->temperature = -1;
  CHK(sdis_solve_probe(scn, N, pos, time, 1.0, 0, 0, &estimator) == RES_BAD_ARG);

  CHK(sdis_scene_ref_put(scn) == RES_OK);
  CHK(sdis_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

