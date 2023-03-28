/* Copyright (C) 2016-2022 |Meso|Star> (contact@meso-star.com)
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

/* TODO draw the configuration */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
static void
get_position(const size_t ivert, double pos[2], void* ctx)
{
  square_get_position(ivert, pos, ctx);
  pos[0] *= 0.2;
  pos[1] *= 0.5;
}

/*******************************************************************************
 * Media
 ******************************************************************************/
static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return 500;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return 25;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return 7500;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return 0.01;
}

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return 373.15;
}

static struct sdis_medium*
create_solid(struct sdis_device* dev)
{
  struct sdis_solid_shader shader = DUMMY_SOLID_SHADER;
  struct sdis_medium* solid = NULL;

  /* Create the solid_medium */
  shader.calorific_capacity = solid_get_calorific_capacity;
  shader.thermal_conductivity = solid_get_thermal_conductivity;
  shader.volumic_mass = solid_get_volumic_mass;
  shader.delta = solid_get_delta;
  OK(sdis_solid_create(dev, &shader, NULL, &solid));
  return solid;
}

static struct sdis_medium*
create_fluid(struct sdis_device* dev)
{
  struct sdis_fluid_shader shader = DUMMY_FLUID_SHADER;
  struct sdis_medium* fluid = NULL;

  /* Create the solid_medium */
  shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &shader, NULL, &fluid));
  return fluid;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double h;
  double phi;
};

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  (void)frag;
  return interf->h;
}

static double
interface_get_flux
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  (void)frag;
  return interf->phi;
}

static struct sdis_interface*
interface_create
  (struct sdis_device* sdis,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const double h,
   const double phi)
{
  struct sdis_interface* interf = NULL;
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_data* data = NULL;
  struct interf* props = NULL;

  shader.front.flux = interface_get_flux;
  shader.convection_coef = interface_get_convection_coef;
  shader.convection_coef_upper_bound = h;

  OK(sdis_data_create(sdis, sizeof(struct interf), 16, NULL, &data));
  props = sdis_data_get(data);
  props->h = h;
  props->phi = phi;
  OK(sdis_interface_create(sdis, front, back, &shader, data, &interf));
  OK(sdis_data_ref_put(data));
  return interf;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* dev = NULL;
  struct sdis_interface* interf_left = NULL;
  struct sdis_interface* interf_right = NULL;
  struct sdis_interface* interf_adiab = NULL;
  struct sdis_interface* interfaces[4];
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  solid = create_solid(dev);
  fluid = create_fluid(dev);

  interf_left = interface_create(dev, solid, fluid, 10, 1000);
  interf_right = interface_create(dev, solid, fluid, 0, 5000);
  interf_adiab = interface_create(dev, solid, fluid, 0, SDIS_FLUX_NONE);
  interfaces[0] = interf_adiab; /* Bottom */
  interfaces[1] = interf_left;
  interfaces[2] = interf_adiab; /* Top */
  interfaces[3] = interf_right;

  scn_args.get_indices = square_get_indices;
  scn_args.get_interface = square_get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = square_nsegments;
  scn_args.nvertices = square_nvertices;
  scn_args.context = interfaces;
  OK(sdis_scene_2d_create(dev, &scn_args, &scn));

  OK(sdis_device_ref_put(dev));
  OK(sdis_interface_ref_put(interf_left));
  OK(sdis_interface_ref_put(interf_right));
  OK(sdis_interface_ref_put(interf_adiab));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_scene_ref_put(scn));
  CHK(mem_allocated_size() == 0);
  return 0;
}
