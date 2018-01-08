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

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 0, &dev) == RES_OK);

  CHK(sdis_fluid_create(NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_fluid_create(dev, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_fluid_create(NULL, &fluid_shader, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_fluid_create(NULL, NULL, NULL, &fluid) == RES_BAD_ARG);
  CHK(sdis_fluid_create(dev, NULL, NULL, &fluid) == RES_BAD_ARG);
  CHK(sdis_fluid_create(NULL, &fluid_shader, NULL, &fluid) == RES_BAD_ARG);
  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_OK);

  CHK(sdis_medium_ref_get(NULL) == RES_BAD_ARG);
  CHK(sdis_medium_ref_get(fluid) == RES_OK);
  CHK(sdis_medium_ref_put(NULL) == RES_BAD_ARG);
  CHK(sdis_medium_ref_put(fluid) == RES_OK);
  CHK(sdis_medium_ref_put(fluid) == RES_OK);

  fluid_shader.calorific_capacity = NULL;
  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_BAD_ARG);
  fluid_shader.calorific_capacity = DUMMY_FLUID_SHADER.calorific_capacity;

  fluid_shader.volumic_mass = NULL;
  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_BAD_ARG);
  fluid_shader.volumic_mass = DUMMY_FLUID_SHADER.volumic_mass;

  fluid_shader.temperature = NULL;
  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_BAD_ARG);
  fluid_shader.temperature = DUMMY_FLUID_SHADER.temperature;

  CHK(sdis_fluid_create
    (dev, &SDIS_FLUID_SHADER_NULL, NULL, &fluid) == RES_BAD_ARG);

  CHK(sdis_solid_create(NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_solid_create(dev, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_solid_create(NULL, &solid_shader, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_solid_create(dev, &solid_shader, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_solid_create(NULL, NULL, NULL, &solid) == RES_BAD_ARG);
  CHK(sdis_solid_create(dev, NULL, NULL, &solid) == RES_BAD_ARG);
  CHK(sdis_solid_create(NULL, &solid_shader, NULL, &solid) == RES_BAD_ARG);
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_OK);
  CHK(sdis_medium_ref_put(solid) == RES_OK);

  solid_shader.calorific_capacity = NULL;
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_BAD_ARG);
  solid_shader.calorific_capacity = DUMMY_SOLID_SHADER.calorific_capacity;

  solid_shader.thermal_conductivity = NULL;
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_BAD_ARG);
  solid_shader.thermal_conductivity = DUMMY_SOLID_SHADER.thermal_conductivity;

  solid_shader.volumic_mass = NULL;
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_BAD_ARG);
  solid_shader.volumic_mass = DUMMY_SOLID_SHADER.volumic_mass;

  solid_shader.delta_solid = NULL;
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_BAD_ARG);
  solid_shader.delta_solid = DUMMY_SOLID_SHADER.delta_solid;

  solid_shader.delta_boundary = NULL;
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_BAD_ARG);
  solid_shader.delta_boundary = DUMMY_SOLID_SHADER.delta_boundary;

  solid_shader.temperature = NULL;
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_BAD_ARG);
  solid_shader.temperature = DUMMY_SOLID_SHADER.temperature;

  CHK(sdis_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return 0;
}
