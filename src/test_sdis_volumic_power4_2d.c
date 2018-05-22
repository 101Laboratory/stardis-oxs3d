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

#define Tfluid 0
#define Power 10000
#define Tboundary -1
#define Hboundary 50
#define Lambda 10.0
#define Delta (1.0/20.0)
#define Nrealisations 10000

/*
 * The 2D scene is a solid slabs stretched along the X dimension to simulate a
 * 1D case. The slab has a volumic power and has a convective exchange with the
 * surrounding fluid whose temperature is fixed to Tfluid.
 *
 *
 *           _\  TFluid
 *          / /
 *          \__/
 *
 * ... -----Hboundary----- ...
 *
 *        Lambda, Power
 *
 * ... -----Hboundary----- ...
 *
 *           _\  TFluid
 *          / /
 *          \__/
 *
 */


static const double vertices[4/*#vertices*/*2/*#coords per vertex*/] = {
 -10000.5,-0.5,
 -10000.5, 0.5,
  10000.5, 0.5,
  10000.5,-0.5
};
static const size_t nvertices = sizeof(vertices)/sizeof(double[2]);

static const size_t indices[4/*#segments*/*2/*#indices per segment*/]= {
  0, 1,
  1, 2,
  2, 3,
  3, 0
};
static const size_t nsegments = sizeof(indices)/sizeof(size_t[2]);

/*******************************************************************************
 * Geometry
 ******************************************************************************/
static void
get_indices(const size_t iseg, size_t ids[2], void* context)
{
  (void)context;
  CHK(ids);
  ids[0] = indices[iseg*2+0];
  ids[1] = indices[iseg*2+1];
}

static void
get_position(const size_t ivert, double pos[2], void* context)
{
  (void)context;
  CHK(pos);
  pos[0] = vertices[ivert*2+0];
  pos[1] = vertices[ivert*2+1];
}

static void
get_interface(const size_t iseg, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  *bound = interfaces[iseg];
}

/*******************************************************************************
 * Solid medium
 ******************************************************************************/
struct solid {
  double cp;
  double lambda;
  double rho;
  double delta;
  double volumic_power;
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
solid_get_delta_boundary
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->delta * 2.1;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->temperature;
}

static double
solid_get_volumic_power
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->volumic_power;
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
  const struct fluid* fluid;
  CHK(data != NULL && vtx != NULL);
  fluid = sdis_data_cget(data);
  return fluid->temperature;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double h;
  double temperature;
};

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(frag && data);
  return ((const struct interf*)sdis_data_cget(data))->h;
}

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(frag && data);
  return ((const struct interf*)sdis_data_cget(data))->temperature;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct solid* solid_param = NULL;
  struct fluid* fluid_param = NULL;
  struct interf* interf_param = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_data* data = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interf_adiabatic = NULL;
  struct sdis_interface* interf_solid_fluid = NULL;
  struct sdis_interface* interfaces[4/*#segment*/];
  struct sdis_mc T = SDIS_MC_NULL;
  double pos[2];
  double Tref;
  double Tinterf;
  double L;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev) == RES_OK);

  /* Create the fluid medium */
  fluid_shader.temperature = fluid_get_temperature;
  fluid_shader.calorific_capacity = dummy_medium_getter;
  fluid_shader.volumic_mass = dummy_medium_getter;
  CHK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data) == RES_OK);
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tfluid;
  CHK(sdis_fluid_create(dev, &fluid_shader, data, &fluid) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.delta_boundary = solid_get_delta_boundary;
  solid_shader.temperature = solid_get_temperature;
  solid_shader.volumic_power = solid_get_volumic_power;

  /* Create the solid medium */
  CHK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data) == RES_OK);
  solid_param = sdis_data_get(data);
  solid_param->cp = 500000;
  solid_param->rho = 1000;
  solid_param->lambda = Lambda;
  solid_param->delta = Delta;
  solid_param->volumic_power = Power;
  solid_param->temperature = -1;
  CHK(sdis_solid_create(dev, &solid_shader, data, &solid) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;

  /* Create the adiabatic interface */
  CHK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data) == RES_OK);
  interf_param = sdis_data_get(data);
  interf_param->h = 0;
  interf_param->temperature = -1;
  CHK(sdis_interface_create(dev, solid, fluid, &interf_shader, data,
    &interf_adiabatic) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the solid fluid interface */
  CHK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data) == RES_OK);
  interf_param = sdis_data_get(data);
  interf_param->h = Hboundary;
  interf_param->temperature = Tboundary;
  CHK(sdis_interface_create(dev, solid, fluid, &interf_shader, data,
    &interf_solid_fluid) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Release the media */
  CHK(sdis_medium_ref_put(fluid) == RES_OK);
  CHK(sdis_medium_ref_put(solid) == RES_OK);

  /* Map the interfaces to their square segments */
  interfaces[0] = interf_adiabatic;
  interfaces[1] = interf_solid_fluid;
  interfaces[2] = interf_adiabatic;
  interfaces[3] = interf_solid_fluid;

#if 0
  dump_segments(stdout, vertices, nvertices, indices, nsegments);
  exit(0);
#endif

  /* Create the scene */
  CHK(sdis_scene_2d_create(dev, nsegments, get_indices, get_interface,
    nvertices, get_position, interfaces, &scn) == RES_OK);

  /* Release the interfaces */
  CHK(sdis_interface_ref_put(interf_adiabatic) == RES_OK);
  CHK(sdis_interface_ref_put(interf_solid_fluid) == RES_OK);

  pos[0] = 0;
  pos[1] = 0.25;

  L = vertices[3] - vertices[1];
  if(Tboundary >= 0) {
    Tinterf = Tboundary;
  } else {
    Tinterf = Power*L / (2*Hboundary) + Tfluid;
  }
  Tref =
    Tinterf
  + Power / (2*Lambda) * ((L*L)/4.0 - pos[1]*pos[1]);

  CHK(sdis_solve_probe(scn, Nrealisations, pos, INF, 1.f, -1, 0, &estimator) == RES_OK);
  CHK(sdis_estimator_get_temperature(estimator, &T) == RES_OK);
  printf("Temperature at (%g %g) = %g ~ %g +/- %g\n",
    SPLIT2(pos), Tref, T.E, T.SE);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);

  CHK(sdis_scene_ref_put(scn) == RES_OK);
  CHK(sdis_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

