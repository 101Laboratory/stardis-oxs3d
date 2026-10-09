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

#include <rsys/rsys.h>

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static double
radenv_get_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray, (void)data;
  return 300;
}

static double
radenv_get_reference_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray, (void)data;
  return 300;
}

static void
check_api(struct sdis_device* sdis)
{
  struct sdis_radiative_env_shader shader = SDIS_RADIATIVE_ENV_SHADER_NULL;
  struct sdis_radiative_env_shader shader2 = SDIS_RADIATIVE_ENV_SHADER_NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_data* data = NULL;

  CHK(sdis != NULL);
  BA(sdis_radiative_env_create(NULL, &shader, NULL, &radenv));
  BA(sdis_radiative_env_create(sdis, NULL, NULL, &radenv));
  OK(sdis_radiative_env_create(sdis, &shader, NULL, &radenv));

  BA(sdis_radiative_env_get_shader(NULL, &shader2));
  BA(sdis_radiative_env_get_shader(radenv, NULL));
  OK(sdis_radiative_env_get_shader(radenv, &shader2));

  CHK(shader2.temperature == shader.temperature);
  CHK(shader2.reference_temperature == shader.reference_temperature);

  CHK(sdis_radiative_env_get_data(radenv) == NULL);

  BA(sdis_radiative_env_ref_get(NULL));
  OK(sdis_radiative_env_ref_get(radenv));
  BA(sdis_radiative_env_ref_put(NULL));
  OK(sdis_radiative_env_ref_put(radenv));
  OK(sdis_radiative_env_ref_put(radenv));

  shader.temperature = radenv_get_temperature;
  shader.reference_temperature = radenv_get_reference_temperature;

  OK(sdis_data_create(sdis, sizeof(uint32_t), ALIGNOF(uint32_t), NULL, &data));
  *((uint32_t*)sdis_data_get(data)) = 0xD3CAFBAD;

  OK(sdis_radiative_env_create(sdis, &shader, data, &radenv));
  OK(sdis_data_ref_put(data));

  OK(sdis_radiative_env_get_shader(radenv, &shader2));

  CHK(shader2.temperature == shader.temperature);
  CHK(shader2.reference_temperature == shader.reference_temperature);

  CHK(sdis_radiative_env_get_data(radenv) == data);
  data = sdis_radiative_env_get_data(radenv);
  CHK(*((uint32_t*)sdis_data_get(data)) == 0xD3CAFBAD);

  OK(sdis_radiative_env_ref_put(radenv));
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* sdis = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &sdis));

  check_api(sdis);

  OK(sdis_device_ref_put(sdis));
  CHK(mem_allocated_size() == 0);
  return 0;
}
