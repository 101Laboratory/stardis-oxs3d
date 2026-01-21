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

#include <rsys_math.h>

int
main(int argc, char** argv)
{
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_fluid_shader fluid_shader2 = SDIS_FLUID_SHADER_NULL;
  struct sdis_solid_shader solid_shader2 = SDIS_SOLID_SHADER_NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  BA(sdis_fluid_create(NULL, NULL, NULL, NULL));
  BA(sdis_fluid_create(dev, NULL, NULL, NULL));
  BA(sdis_fluid_create(NULL, &fluid_shader, NULL, NULL));
  BA(sdis_fluid_create(dev, &fluid_shader, NULL, NULL));
  BA(sdis_fluid_create(NULL, NULL, NULL, &fluid));
  BA(sdis_fluid_create(dev, NULL, NULL, &fluid));
  BA(sdis_fluid_create(NULL, &fluid_shader, NULL, &fluid));
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  CHK(sdis_medium_get_type(fluid) == SDIS_FLUID);
  CHK(sdis_medium_get_data(fluid) == NULL);

  BA(sdis_medium_ref_get(NULL));
  OK(sdis_medium_ref_get(fluid));
  BA(sdis_medium_ref_put(NULL));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(fluid));

  fluid_shader.calorific_capacity = NULL;
  BA(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));
  fluid_shader.calorific_capacity = DUMMY_FLUID_SHADER.calorific_capacity;

  fluid_shader.volumic_mass = NULL;
  BA(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));
  fluid_shader.volumic_mass = DUMMY_FLUID_SHADER.volumic_mass;

  fluid_shader.temperature = NULL;
  BA(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));
  fluid_shader.temperature = DUMMY_FLUID_SHADER.temperature;

  fluid_shader.t0 = -INF;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));
  OK(sdis_medium_ref_put(fluid));
  fluid_shader.t0 = INF;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));
  OK(sdis_medium_ref_put(fluid));
  fluid_shader.t0 = DUMMY_FLUID_SHADER.t0;

  BA(sdis_fluid_create(dev, &SDIS_FLUID_SHADER_NULL, NULL, &fluid));

  OK(sdis_data_create(dev, 4, 16, NULL, &data));
  BA(sdis_solid_create(NULL, NULL, data, NULL));
  BA(sdis_solid_create(dev, NULL, data, NULL));
  BA(sdis_solid_create(NULL, &solid_shader, data, NULL));
  BA(sdis_solid_create(dev, &solid_shader, data, NULL));
  BA(sdis_solid_create(NULL, NULL, data, &solid));
  BA(sdis_solid_create(dev, NULL, data, &solid));
  BA(sdis_solid_create(NULL, &solid_shader, data, &solid));
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  CHK(sdis_medium_get_type(solid) == SDIS_SOLID);
  CHK(sdis_medium_get_data(solid) == data);

  OK(sdis_medium_ref_put(solid));
  OK(sdis_data_ref_put(data));

  solid_shader.calorific_capacity = NULL;
  BA(sdis_solid_create(dev, &solid_shader, NULL, &solid));
  solid_shader.calorific_capacity = DUMMY_SOLID_SHADER.calorific_capacity;

  solid_shader.thermal_conductivity = NULL;
  BA(sdis_solid_create(dev, &solid_shader, NULL, &solid));
  solid_shader.thermal_conductivity = DUMMY_SOLID_SHADER.thermal_conductivity;

  solid_shader.volumic_mass = NULL;
  BA(sdis_solid_create(dev, &solid_shader, NULL, &solid));
  solid_shader.volumic_mass = DUMMY_SOLID_SHADER.volumic_mass;

  solid_shader.delta = NULL;
  BA(sdis_solid_create(dev, &solid_shader, NULL, &solid));
  solid_shader.delta = DUMMY_SOLID_SHADER.delta;

  solid_shader.temperature = NULL;
  BA(sdis_solid_create(dev, &solid_shader, NULL, &solid));
  solid_shader.temperature = DUMMY_SOLID_SHADER.temperature;

  solid_shader.t0 = -INF;
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));
  OK(sdis_medium_ref_put(solid));
  solid_shader.t0 = INF;
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));
  OK(sdis_medium_ref_put(solid));
  solid_shader.t0 = DUMMY_SOLID_SHADER.t0;

  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));

  CHK(sdis_medium_get_id(fluid) != sdis_medium_get_id(solid));

  BA(sdis_fluid_get_shader(NULL, &fluid_shader2));
  BA(sdis_fluid_get_shader(fluid, NULL));
  BA(sdis_fluid_get_shader(solid, &fluid_shader2));
  OK(sdis_fluid_get_shader(fluid, &fluid_shader2));

  CHK(fluid_shader.calorific_capacity == fluid_shader2.calorific_capacity);
  CHK(fluid_shader.volumic_mass == fluid_shader2.volumic_mass);
  CHK(fluid_shader.temperature == fluid_shader2.temperature);
  CHK(fluid_shader.t0 == fluid_shader2.t0);

  BA(sdis_solid_get_shader(NULL, &solid_shader2));
  BA(sdis_solid_get_shader(solid, NULL));
  BA(sdis_solid_get_shader(fluid, &solid_shader2));
  OK(sdis_solid_get_shader(solid, &solid_shader2));

  CHK(solid_shader.calorific_capacity == solid_shader2.calorific_capacity);
  CHK(solid_shader.thermal_conductivity == solid_shader2.thermal_conductivity);
  CHK(solid_shader.volumic_mass == solid_shader2.volumic_mass);
  CHK(solid_shader.delta == solid_shader2.delta);
  CHK(solid_shader.volumic_power == solid_shader2.volumic_power);
  CHK(solid_shader.temperature == solid_shader2.temperature);
  CHK(solid_shader.t0 == solid_shader2.t0);

  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_device_ref_put(dev));

  CHK(mem_allocated_size() == 0);

  return 0;
}
