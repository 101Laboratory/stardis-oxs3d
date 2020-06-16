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

#define UNKNOWN_TEMPERATURE -1

/*******************************************************************************
 * Geometry functions
 ******************************************************************************/
struct context {
  struct sdis_interface* interfs[12];
};

static void
get_scaled_position(const size_t ivert, double pos[3], void* context)
{
  box_get_position(ivert, pos, context);
  pos[0] *= 0.3;
  pos[1] *= 0.1;
  pos[2] *= 0.2;
}

static void
get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct context* ctx = context;
  *bound = ctx->interfs[itri];
}

/*******************************************************************************
 * Solid medium
 ******************************************************************************/
struct solid {
  double cp;
  double lambda;
  double rho;
  double delta;
  double init_temperature;
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
  if(vtx->time <= 0) {
    return ((const struct solid*)sdis_data_cget(data))->init_temperature;
  } else {
    return UNKNOWN_TEMPERATURE;
  }
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
struct interf {
  double temperature;
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && data);
  return interf->temperature;
}

static struct sdis_interface*
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const struct sdis_interface_shader* interf_shader,
   const double temperature)
{
  struct sdis_data* data = NULL;
  struct sdis_interface* interf = NULL;
  struct interf* interf_props = NULL;

  OK(sdis_data_create
    (dev, sizeof(struct interf), ALIGNOF(struct interf), NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = temperature;
  OK(sdis_interface_create
    (dev, front, back, interf_shader, data, &interf));
  OK(sdis_data_ref_put(data));
  return interf;
}

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sdis_device* dev = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_data* data = NULL;
  struct sdis_fluid_shader fluid_shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interfs[6] = {NULL};
  struct sdis_estimator* estimator = NULL;
  struct sdis_mc temperature = SDIS_MC_NULL;
  struct solid* solid_param = NULL;
  struct context ctx;
  const size_t nrealisations = 10000;
  size_t nfails = 0;
  double probe[3];
  double time[2];
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev));

  /* Create the fluid medium */
  fluid_shader = DUMMY_FLUID_SHADER;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->rho = 1700;
  solid_param->cp = 800;
  solid_param->lambda = 1.15;
  solid_param->delta = 1.0/20.0;
  solid_param->init_temperature = 300;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Setup the interface shader */
  interf_shader.front.temperature = interface_get_temperature;

  /* Create the interfaces */
  interfs[0] = create_interface(dev, solid, fluid, &interf_shader, 280);/*Xmin*/
  interfs[1] = create_interface(dev, solid, fluid, &interf_shader, 290);/*Xmax*/
  interfs[2] = create_interface(dev, solid, fluid, &interf_shader, 310);/*Ymin*/
  interfs[3] = create_interface(dev, solid, fluid, &interf_shader, 270);/*Ymax*/
  interfs[4] = create_interface(dev, solid, fluid, &interf_shader, 300);/*Zmin*/
  interfs[5] = create_interface(dev, solid, fluid, &interf_shader, 320);/*Zmax*/

  /* Setup the scene context */
  ctx.interfs[0]  = ctx.interfs[1]  = interfs[4]; /* Zmin */
  ctx.interfs[2]  = ctx.interfs[3]  = interfs[0]; /* Xmin */
  ctx.interfs[4]  = ctx.interfs[5]  = interfs[5]; /* Zmax */
  ctx.interfs[6]  = ctx.interfs[7]  = interfs[1]; /* Xmax */
  ctx.interfs[8]  = ctx.interfs[9]  = interfs[3]; /* Ymax */
  ctx.interfs[10] = ctx.interfs[11] = interfs[2]; /* Ymin */

  /* Create the box scene */
  OK(sdis_scene_create(dev, box_ntriangles, box_get_indices, get_interface,
    box_nvertices, get_scaled_position, &ctx, &box_scn));

  /* Setup and run the simulation */
  probe[0] = 0.1;
  probe[1] = 0.06;
  probe[2] = 0.130;
  time[0] = time[1] = 1000; /* Observation time range */
  OK(sdis_solve_probe(box_scn, nrealisations, probe, time, 1, 0, 290,
    SDIS_HEAT_PATH_NONE, &estimator));

  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &temperature));
  printf("Temperature at (%g, %g, %g) m at %g s ~ %g +/- %g\n",
    SPLIT3(probe), time[0], temperature.E, temperature.SE);
  printf("#failures = %lu/%lu\n",
    (unsigned long)nfails, (unsigned long)nrealisations);

  OK(sdis_estimator_ref_put(estimator));
  OK(sdis_interface_ref_put(interfs[0]));
  OK(sdis_interface_ref_put(interfs[1]));
  OK(sdis_interface_ref_put(interfs[2]));
  OK(sdis_interface_ref_put(interfs[3]));
  OK(sdis_interface_ref_put(interfs[4]));
  OK(sdis_interface_ref_put(interfs[5]));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_device_ref_put(dev));
  OK(sdis_scene_ref_put(box_scn));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
