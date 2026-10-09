/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_interface* interf2 = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader shader = DUMMY_INTERFACE_SHADER;
  struct sdis_interface_shader shader2 = SDIS_INTERFACE_SHADER_NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));

  shader = SDIS_INTERFACE_SHADER_NULL;

  #define CREATE sdis_interface_create
  BA(CREATE(NULL, NULL, NULL, NULL, NULL, NULL));
  BA(CREATE(dev, NULL, NULL, NULL, NULL, NULL));
  BA(CREATE(NULL, solid, NULL, NULL, NULL, NULL));
  BA(CREATE(dev, solid, NULL, NULL, NULL, NULL));
  BA(CREATE(NULL, NULL, fluid, NULL, NULL, NULL));
  BA(CREATE(dev, NULL, fluid, NULL, NULL, NULL));
  BA(CREATE(NULL, solid, fluid, NULL, NULL, NULL));
  BA(CREATE(dev, solid, fluid, NULL, NULL, NULL));
  BA(CREATE(NULL, NULL, NULL, &shader, NULL, NULL));
  BA(CREATE(dev, NULL, NULL, &shader, NULL, NULL));
  BA(CREATE(NULL, solid, NULL, &shader, NULL, NULL));
  BA(CREATE(dev, solid, NULL, &shader, NULL, NULL));
  BA(CREATE(NULL, NULL, fluid, &shader, NULL, NULL));
  BA(CREATE(dev, NULL, fluid, &shader, NULL, NULL));
  BA(CREATE(NULL, solid, fluid, &shader, NULL, NULL));
  BA(CREATE(dev, solid, fluid, &shader, NULL, NULL));
  BA(CREATE(NULL, NULL, NULL, NULL, NULL, &interf));
  BA(CREATE(dev, NULL, NULL, NULL, NULL, &interf));
  BA(CREATE(NULL, solid, NULL, NULL, NULL, &interf));
  BA(CREATE(dev, solid, NULL, NULL, NULL, &interf));
  BA(CREATE(NULL, NULL, fluid, NULL, NULL, &interf));
  BA(CREATE(dev, NULL, fluid, NULL, NULL, &interf));
  BA(CREATE(NULL, solid, fluid, NULL, NULL, &interf));
  BA(CREATE(dev, solid, fluid, NULL, NULL, &interf));
  BA(CREATE(NULL, NULL, NULL, &shader, NULL, &interf));
  BA(CREATE(dev, NULL, NULL, &shader, NULL, &interf));
  BA(CREATE(NULL, solid, NULL, &shader, NULL, &interf));
  BA(CREATE(dev, solid, NULL, &shader, NULL, &interf));
  BA(CREATE(NULL, NULL, fluid, &shader, NULL, &interf));
  BA(CREATE(dev, NULL, fluid, &shader, NULL, &interf));
  BA(CREATE(NULL, solid, fluid, &shader, NULL, &interf));
  OK(CREATE(dev, solid, fluid, &shader, NULL, &interf));

  BA(sdis_interface_ref_get(NULL));
  OK(sdis_interface_ref_get(interf));
  BA(sdis_interface_ref_put(NULL));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_interface_ref_put(interf));

  OK(CREATE(dev, solid, solid, &shader, NULL, &interf));
  OK(sdis_interface_ref_put(interf));
  shader = SDIS_INTERFACE_SHADER_NULL;
  OK(CREATE(dev, solid, solid, &shader, NULL, &interf));
  OK(sdis_interface_ref_put(interf));

  shader.front.temperature = dummy_interface_getter;
  OK(CREATE(dev, solid, solid, &shader, NULL, &interf));
  OK(sdis_interface_ref_put(interf));

  shader.back.emissivity = dummy_radiative_interface_getter;
  OK(CREATE(dev, solid, fluid, &shader, NULL, &interf));
  OK(sdis_interface_ref_put(interf));
  shader.back.specular_fraction = dummy_radiative_interface_getter;
  OK(CREATE(dev, solid, fluid, &shader, NULL, &interf));
  OK(sdis_interface_ref_put(interf));
  shader.back = SDIS_INTERFACE_SIDE_SHADER_NULL;
  shader.front.emissivity = dummy_radiative_interface_getter;
  OK(CREATE(dev, solid, fluid, &shader, NULL, &interf)); /* Warning */
  OK(sdis_interface_ref_put(interf));
  shader.front.emissivity = NULL;
  shader.front.specular_fraction = dummy_radiative_interface_getter;
  OK(CREATE(dev, solid, fluid, &shader, NULL, &interf)); /* Warning */
  OK(sdis_interface_ref_put(interf));
  shader.front.specular_fraction = NULL;
  shader.convection_coef_upper_bound = -1;
  OK(CREATE(dev, solid, solid, &shader, NULL, &interf)); /* Warning */
  OK(sdis_interface_ref_put(interf));
  BA(CREATE(dev, solid, fluid, &shader, NULL, &interf));
  shader.convection_coef_upper_bound = 0;

  OK(sdis_data_create(dev, 4, 16, NULL, &data));
  OK(CREATE(dev, solid, fluid, &shader, data, &interf));
  OK(CREATE(dev, solid, fluid, &shader, NULL, &interf2));

  CHK(sdis_interface_get_data(interf) == data);
  CHK(sdis_interface_get_data(interf2) == NULL);
  CHK(sdis_interface_get_id(interf) != sdis_interface_get_id(interf2));

  BA(sdis_interface_get_shader(NULL, &shader2));
  BA(sdis_interface_get_shader(interf, NULL));
  OK(sdis_interface_get_shader(interf, &shader2));

  CHK(shader.convection_coef == shader2.convection_coef);
  CHK(shader.convection_coef_upper_bound == shader2.convection_coef_upper_bound);
  CHK(shader.front.temperature == shader2.front.temperature);
  CHK(shader.front.flux == shader2.front.flux);
  CHK(shader.front.emissivity == shader2.front.emissivity);
  CHK(shader.front.specular_fraction == shader2.front.specular_fraction);
  CHK(shader.back.temperature == shader2.back.temperature);
  CHK(shader.back.flux == shader2.back.flux);
  CHK(shader.back.emissivity == shader2.back.emissivity);
  CHK(shader.back.specular_fraction == shader2.back.specular_fraction);

  OK(sdis_interface_ref_put(interf));
  OK(sdis_interface_ref_put(interf2));
  OK(sdis_data_ref_put(data));

  #undef CREATE

  OK(sdis_device_ref_put(dev));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid));

  CHK(mem_allocated_size() == 0);
  return 0;
}

