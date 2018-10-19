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

#define Tf1 0
#define Tf2 100
#define Power 0 /*10000*/
#define H1 50
#define H2 50
#define LAMBDA 100.0
#define DELTA (1.0/20.0)
#define N 10000

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
  struct sdis_medium* fluid1 = NULL;
  struct sdis_medium* fluid2 = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interf_adiabatic = NULL;
  struct sdis_interface* interf_solid_fluid1 = NULL;
  struct sdis_interface* interf_solid_fluid2 = NULL;
  struct sdis_interface* interfaces[4/*#segment*/];
  struct sdis_mc T = SDIS_MC_NULL;
  size_t nreals, nfails;
  double pos[2];
  double time_range[2] = { INF, INF };
  double Tref;
  double a, b, x;
  double L;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev));

  /* Create the fluid medium */
  fluid_shader.temperature = fluid_get_temperature;
  fluid_shader.calorific_capacity = dummy_medium_getter;
  fluid_shader.volumic_mass = dummy_medium_getter;

  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tf1;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid1));
  OK(sdis_data_ref_put(data));

  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tf2;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid2));
  OK(sdis_data_ref_put(data));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  solid_shader.volumic_power = solid_get_volumic_power;

  /* Create the solid medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 500000;
  solid_param->rho = 1000;
  solid_param->lambda = LAMBDA;
  solid_param->delta = DELTA;
  solid_param->volumic_power = Power;
  solid_param->temperature = -1;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;

  /* Create the adiabatic interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 0;
  interf_param->temperature = -1;
  OK(sdis_interface_create(dev, solid, fluid1, &interf_shader, data,
    &interf_adiabatic));
  OK(sdis_data_ref_put(data));

  /* Create the solid fluid1 interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = H1;
  interf_param->temperature = -1;
  OK(sdis_interface_create(dev, solid, fluid1, &interf_shader, data,
    &interf_solid_fluid1));
  OK(sdis_data_ref_put(data));

  /* Create the solid fluid2 interface */
  OK(sdis_data_create (dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = H2;
  interf_param->temperature = -1;
  OK(sdis_interface_create(dev, solid, fluid2, &interf_shader, data,
    &interf_solid_fluid2));
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(fluid1));
  OK(sdis_medium_ref_put(fluid2));
  OK(sdis_medium_ref_put(solid));

  /* Map the interfaces to their square segments */
  interfaces[0] = interf_adiabatic;
  interfaces[1] = interf_solid_fluid1;
  interfaces[2] = interf_adiabatic;
  interfaces[3] = interf_solid_fluid2;

#if 0
  dump_segments(stdout, vertices, nvertices, indices, nsegments);
  exit(0);
#endif

  /* Create the scene */
  OK(sdis_scene_2d_create(dev, nsegments, get_indices, get_interface,
    nvertices, get_position, interfaces, &scn));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_adiabatic));
  OK(sdis_interface_ref_put(interf_solid_fluid1));
  OK(sdis_interface_ref_put(interf_solid_fluid2));

  pos[0] = 0;
  pos[1] = 0.25;

  L = vertices[3] - vertices[1];
#if 1
  x = pos[1] + vertices[3];
  a = (H2*Power*L + H1*H2*(Tf1 - Tf2) + H1*H2*Power*L*L/(2*LAMBDA))
    / (LAMBDA * (H1 + H2) + H1*H2*L);
  b = Tf2 + a * LAMBDA / H2;
  Tref = -Power / (2*LAMBDA) * x*x + a * x + b;
#else
  tmp = LAMBDA / L;
  T1 = H1 * (H2+tmp) / (tmp*(H1+H2) + H1*H2) * Tf1
     + H2 *     tmp  / (tmp*(H1+H2) + H1*H2) * Tf2;
  T2 = H1 *     tmp  / (tmp*(H1+H2) + H1*H2) * Tf1
     + H2 * (H1+tmp) / (tmp*(H1+H2) + H1*H2) * Tf2;
  Tref = T2 + (T1-T2)/L * (pos[1]  + vertices[3]);
#endif

  OK(sdis_solve_probe(scn, N, pos, time_range, 1.f, -1, 0, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  printf("Temperature at (%g %g) = %g ~ %g +/- %g [%g %g]\n",
    SPLIT2(pos), Tref, T.E, T.SE, T.E-3*T.SE, T.E+3*T.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
  OK(sdis_estimator_ref_put(estimator));
  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, Tref, T.SE*3));

  OK(sdis_scene_ref_put(scn));
  OK(sdis_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

