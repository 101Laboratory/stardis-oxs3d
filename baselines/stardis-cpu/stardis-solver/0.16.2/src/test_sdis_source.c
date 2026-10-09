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

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
spherical_source_get_position
  (const double time,
   double pos[3],
   struct sdis_data* data)
{
  (void)time, (void)data;
  pos[0] = pos[1] = pos[2] = 1.234; /* [m] */
}

static double
spherical_source_get_power
  (const double time,
   struct sdis_data* data)
{
  (void)time, (void)data;
  return 10; /* [W] */
}

static double
spherical_source_get_diffuse_radiance
  (const double time,
   const double dir[3],
   struct sdis_data* data)
{
  (void)time, (void)dir, (void)data;
  return 50; /* [W/m^2/sr] */
}

static void
check_spherical_source(struct sdis_device* dev)
{
  struct sdis_spherical_source_shader shader = SDIS_SPHERICAL_SOURCE_SHADER_NULL;
  struct sdis_spherical_source_shader shader2= SDIS_SPHERICAL_SOURCE_SHADER_NULL;
  struct sdis_source* src = NULL;
  struct sdis_source* src2 = NULL;
  struct sdis_data* data = NULL;
  const double dir[3] = {1,0,0};

  /* Create a data to check its memory management */
  OK(sdis_data_create(dev, sizeof(double[3]), ALIGNOF(double[3]), NULL, &data));

  shader.position = spherical_source_get_position;
  shader.power = spherical_source_get_power;
  shader.radius = 1;
  BA(sdis_spherical_source_create(NULL, &shader, data, &src));
  BA(sdis_spherical_source_create(dev, NULL, data, &src));
  BA(sdis_spherical_source_create(dev, &shader, data, NULL));
  OK(sdis_spherical_source_create(dev, &shader, data, &src));

  BA(sdis_spherical_source_get_shader(NULL, &shader2));
  BA(sdis_spherical_source_get_shader(src, NULL));
  OK(sdis_spherical_source_get_shader(src, &shader2));

  CHK(sdis_source_get_data(src) == data);

  CHK(shader2.position == shader.position);
  CHK(shader2.power == shader.power);
  CHK(shader2.diffuse_radiance == shader.diffuse_radiance);
  CHK(shader2.power(INF, data) == 10);

  BA(sdis_source_ref_get(NULL));
  OK(sdis_source_ref_get(src));
  BA(sdis_source_ref_put(NULL));
  OK(sdis_source_ref_put(src));
  OK(sdis_source_ref_put(src));

  OK(sdis_data_ref_put(data));

  OK(sdis_spherical_source_create(dev, &shader, NULL, &src));
  OK(sdis_spherical_source_create(dev, &shader, NULL, &src2));
  CHK(sdis_source_get_id(src) != sdis_source_get_id(src2));
  CHK(sdis_source_get_data(src) == NULL);
  OK(sdis_source_ref_put(src));
  OK(sdis_source_ref_put(src2));

  shader.position = NULL;
  BA(sdis_spherical_source_create(dev, &shader, NULL, &src));
  shader.position = spherical_source_get_position;
  shader.power = NULL;
  BA(sdis_spherical_source_create(dev, &shader, NULL, &src));
  shader.power = spherical_source_get_power;
  shader.diffuse_radiance = spherical_source_get_diffuse_radiance;
  OK(sdis_spherical_source_create(dev, &shader, NULL, &src));

  OK(sdis_spherical_source_get_shader(src, &shader2));
  CHK(shader2.diffuse_radiance = spherical_source_get_diffuse_radiance);
  CHK(shader2.diffuse_radiance(INF, dir, NULL) == 50);

  OK(sdis_source_ref_put(src));
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* dev = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  check_spherical_source(dev);

  OK(sdis_device_ref_put(dev));
  CHK(mem_allocated_size() == 0);
  return 0;
}
