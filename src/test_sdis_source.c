/* Copyright (C) 2016-2023 |Méso|Star> (contact@meso-star.com)
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
  pos[0] = pos[1] = pos[2] = 1.234;
}

static void
check_spherical_source(struct sdis_device* dev)
{
  struct sdis_spherical_source_create_args args =
    SDIS_SPHERICAL_SOURCE_CREATE_ARGS_NULL;
  struct sdis_source* src = NULL;
  struct sdis_data* data = NULL;

  /* Create a data to check its memory management */
  OK(sdis_data_create(dev, sizeof(double[3]), ALIGNOF(double[3]), NULL, &data));

  args.position = spherical_source_get_position;
  args.data = data;
  args.radius = 1;
  args.power = 10;

  BA(sdis_spherical_source_create(NULL, &args, &src));
  BA(sdis_spherical_source_create(dev, NULL, &src));
  BA(sdis_spherical_source_create(dev, &args, NULL));
  OK(sdis_spherical_source_create(dev, &args, &src));

  BA(sdis_source_ref_get(NULL));
  OK(sdis_source_ref_get(src));
  BA(sdis_source_ref_put(NULL));
  OK(sdis_source_ref_put(src));
  OK(sdis_source_ref_put(src));

  OK(sdis_data_ref_put(data));

  args.data = NULL;
  OK(sdis_spherical_source_create(dev, &args, &src));
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
