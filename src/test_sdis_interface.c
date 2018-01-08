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
  struct sdis_interface* interface = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader shader = DUMMY_INTERFACE_SHADER;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 0, &dev) == RES_OK);

  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_OK);
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_OK);

  #define CREATE sdis_interface_create
  CHK(CREATE(NULL, NULL, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(dev, NULL, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, solid, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(dev, solid, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, fluid, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(dev, NULL, fluid, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, solid, fluid, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(dev, solid, fluid, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, &shader, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(dev, NULL, NULL, &shader, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, solid, NULL, &shader, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(dev, solid, NULL, &shader, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, fluid, &shader, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(dev, NULL, fluid, &shader, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, solid, fluid, &shader, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(dev, solid, fluid, &shader, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, NULL, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(dev, NULL, NULL, NULL, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(NULL, solid, NULL, NULL, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(dev, solid, NULL, NULL, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, fluid, NULL, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(dev, NULL, fluid, NULL, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(NULL, solid, fluid, NULL, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(dev, solid, fluid, NULL, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, &shader, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(dev, NULL, NULL, &shader, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(NULL, solid, NULL, &shader, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(dev, solid, NULL, &shader, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, fluid, &shader, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(dev, NULL, fluid, &shader, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(NULL, solid, fluid, &shader, NULL, &interface) == RES_BAD_ARG);
  CHK(CREATE(dev, solid, fluid, &shader, NULL, &interface) == RES_OK);

  CHK(sdis_interface_ref_get(NULL) == RES_BAD_ARG);
  CHK(sdis_interface_ref_get(interface) == RES_OK);
  CHK(sdis_interface_ref_put(NULL) == RES_BAD_ARG);
  CHK(sdis_interface_ref_put(interface) == RES_OK);
  CHK(sdis_interface_ref_put(interface) == RES_OK);

  CHK(CREATE(dev, solid, solid, &shader, NULL, &interface) == RES_BAD_ARG);
  shader.convection_coef = NULL;
  CHK(CREATE(dev, solid, solid, &shader, NULL, &interface) == RES_OK);
  CHK(sdis_interface_ref_put(interface) == RES_OK);

  shader.temperature = NULL;
  CHK(CREATE(dev, solid, solid, &shader, NULL, &interface) == RES_OK);
  CHK(sdis_interface_ref_put(interface) == RES_OK);

  CHK(CREATE(dev, solid, fluid, &shader, NULL, &interface) == RES_BAD_ARG);
  shader.convection_coef = DUMMY_INTERFACE_SHADER.convection_coef;
  CHK(CREATE(dev, solid, fluid, &shader, NULL, &interface) == RES_OK);
  CHK(sdis_interface_ref_put(interface) == RES_OK);
  #undef CREATE

  CHK(sdis_device_ref_put(dev) == RES_OK);
  CHK(sdis_medium_ref_put(fluid) == RES_OK);
  CHK(sdis_medium_ref_put(solid) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

