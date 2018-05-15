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

static const double vertices[8/*#vertices*/*2/*#coords per vertex*/] = {
 -0.5,-1.0,
 -0.5, 1.0,
  0.5, 1.0,
  0.5,-1.0,
 -0.1, 0.4,
 -0.1, 0.6,
  0.1, 0.6,
  0.1, 0.4
};
static const size_t nvertices = sizeof(vertices)/sizeof(double[2]);

static const size_t indices[8/*#segments*/*2/*#indices per segment*/]= {
  0, 1, /* Rectangle left */
  1, 2, /* Rectangle top */
  2, 3, /* Rectangle right */
  3, 0, /* Rectangle bottom */
  4, 5, /* Square left */
  5, 6, /* Square top */
  6, 7, /* Square right */
  7, 4  /* Square bottom */
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
  double P;
  double T;
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
  return ((const struct solid*)sdis_data_cget(data))->T;
}

static double
solid_get_volumic_power
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->P;
}

/*******************************************************************************
 * Fluid medium
 ******************************************************************************/
struct fluid {
  double T0, T1;
};

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  const struct fluid* fluid;
  CHK(data != NULL && vtx != NULL);
  fluid = sdis_data_cget(data);
  return vtx->P[1] < 0 ? fluid->T0 : fluid->T1;
}


/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double h;
};

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(frag && data);
  return ((const struct interf*)sdis_data_cget(data))->h;
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
  struct sdis_medium* solid0 = NULL;
  struct sdis_medium* solid1 = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interf_adiabatic = NULL;
  struct sdis_interface* interf_solid0_solid1 = NULL;
  struct sdis_interface* interf_solid0_T0 = NULL;
  struct sdis_interface* interf_solid0_T1 = NULL;
  struct sdis_interface* interfaces[8 /*#segment*/];
  struct sdis_mc T = SDIS_MC_NULL;
  double pos[2];
  const size_t N = 10000;
  size_t i;
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
  fluid_param->T0 = 273.15;
  fluid_param->T1 = 373.15;
  CHK(sdis_fluid_create(dev, &fluid_shader, data, &fluid) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.delta_boundary = solid_get_delta_boundary;
  solid_shader.temperature = solid_get_temperature;
  solid_shader.volumic_power = solid_get_volumic_power;

  /* Create the solid0 medium */
  CHK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data) == RES_OK);
  solid_param = sdis_data_get(data);
  solid_param->cp = 500;
  solid_param->rho = 1000;
  solid_param->lambda = 1;
  solid_param->delta = 0.05;
  solid_param->P = SDIS_VOLUMIC_POWER_NONE;
  solid_param->T = -1;
  CHK(sdis_solid_create(dev, &solid_shader, data, &solid0) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the solid1 medium */
  CHK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data) == RES_OK);
  solid_param = sdis_data_get(data);
  solid_param->cp = 500;
  solid_param->rho = 1000;
  solid_param->lambda = 1;
  solid_param->delta = 0.01;
  solid_param->P = 10000;
  solid_param->T = -1;
  CHK(sdis_solid_create(dev, &solid_shader, data, &solid1) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;

  /* Create the solid0/solid1 interface */
  CHK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data) == RES_OK);
  CHK(sdis_interface_create(dev, solid1, solid0, &SDIS_INTERFACE_SHADER_NULL,
    NULL, &interf_solid0_solid1) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the adiabatic interface */
  CHK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data) == RES_OK);
  interf_param = sdis_data_get(data);
  interf_param->h = 0;
  CHK(sdis_interface_create(dev, solid0, fluid, &interf_shader, data,
    &interf_adiabatic) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the solid0 T0 interace */
  CHK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data) == RES_OK);
  interf_param = sdis_data_get(data);
  interf_param->h = 10;
  CHK(sdis_interface_create(dev, solid0, fluid, &interf_shader, data,
    &interf_solid0_T0) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the solid0 T1 interace */
  CHK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data) == RES_OK);
  interf_param = sdis_data_get(data);
  interf_param->h = 5;
  CHK(sdis_interface_create(dev, solid0, fluid, &interf_shader, data,
    &interf_solid0_T1) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Release the media */
  CHK(sdis_medium_ref_put(fluid) == RES_OK);
  CHK(sdis_medium_ref_put(solid0) == RES_OK);
  CHK(sdis_medium_ref_put(solid1) == RES_OK);

  /* Map the interfaces to their square segments */
  interfaces[0] = interf_adiabatic;
  interfaces[1] = interf_solid0_T1;
  interfaces[2] = interf_adiabatic;
  interfaces[3] = interf_solid0_T0;
  interfaces[4] = interf_solid0_solid1;
  interfaces[5] = interf_solid0_solid1;
  interfaces[6] = interf_solid0_solid1;
  interfaces[7] = interf_solid0_solid1;

  /* Create the scene */
  CHK(sdis_scene_2d_create(dev, nsegments, get_indices, get_interface,
    nvertices, get_position, interfaces, &scn) == RES_OK);

  /* Release the interfaces */
  CHK(sdis_interface_ref_put(interf_adiabatic) == RES_OK);
  CHK(sdis_interface_ref_put(interf_solid0_T0) == RES_OK);
  CHK(sdis_interface_ref_put(interf_solid0_T1) == RES_OK);
  CHK(sdis_interface_ref_put(interf_solid0_solid1) == RES_OK);

  FOR_EACH(i, 0, 8) {
    pos[0] = 0;
    pos[1] = 0.85 - (double)i*0.2;;
    CHK(sdis_solve_probe(scn, N, pos, INF, 1.f, -1, 0, &estimator) == RES_OK);
    CHK(sdis_estimator_get_temperature(estimator, &T) == RES_OK);
    printf("Temperature at (%g %g) = %g +/- %g\n", SPLIT2(pos), T.E-273.15, T.SE);
    CHK(sdis_estimator_ref_put(estimator) == RES_OK);
  }

  CHK(sdis_scene_ref_put(scn) == RES_OK);
  CHK(sdis_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

