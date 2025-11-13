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

#include <string.h>

struct param {
  struct sdis_data* name;
  double d;
  int i;
};

static void
param_release(void* mem)
{
  struct param* param = mem;
  CHK(param != NULL);
  if(param->name) OK(sdis_data_ref_put(param->name));
}

int
main(int argc, char** argv)
{
  const char* str = "Hello world!";
  struct sdis_device* dev = NULL;
  struct sdis_data* data = NULL;
  struct param* param = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));
  BA(sdis_data_create(NULL, 0, 0, NULL, NULL));
  BA(sdis_data_create(dev, 0, 0, NULL, NULL));
  BA(sdis_data_create(NULL, 8, 0, NULL, NULL));
  BA(sdis_data_create(dev, 8, 0, NULL, NULL));
  BA(sdis_data_create(NULL, 0, 8, NULL, NULL));
  BA(sdis_data_create(dev, 0, 8, NULL, NULL));
  BA(sdis_data_create(NULL, 8, 8, NULL, NULL));
  BA(sdis_data_create(dev, 8, 8, NULL, NULL));

  BA(sdis_data_create(NULL, 0, 0, NULL, &data));
  BA(sdis_data_create(dev, 0, 0, NULL, &data));
  BA(sdis_data_create(NULL, 8, 0, NULL, &data));
  BA(sdis_data_create(dev, 8, 0, NULL, &data));
  BA(sdis_data_create(NULL, 0, 8, NULL, &data));
  BA(sdis_data_create(dev, 0, 8, NULL, &data));
  BA(sdis_data_create(NULL, 8, 8, NULL, &data));
  OK(sdis_data_create(dev, 8, 8, NULL, &data));

  CHK(sdis_data_get(data) != NULL);
  CHK(sdis_data_cget(data) == sdis_data_get(data));
  CHK(IS_ALIGNED(sdis_data_get(data), 8));

  BA(sdis_data_ref_get(NULL));
  OK(sdis_data_ref_get(data));
  BA(sdis_data_ref_put(NULL));
  OK(sdis_data_ref_put(data));
  OK(sdis_data_ref_put(data));

  OK(sdis_data_create(dev, sizeof(struct param), 64, param_release, &data));
  param = sdis_data_get(data);
  OK(sdis_data_create(dev, strlen(str)+1, ALIGNOF(char), NULL, &param->name));
  strcpy(sdis_data_get(param->name), str);
  param->d = 3.14159;
  param->i = 314159;

  param = sdis_data_get(data);
  CHK(strcmp(sdis_data_cget(param->name), str) == 0);
  CHK(param->d == 3.14159);
  CHK(param->i == 314159);

  OK(sdis_data_ref_put(data));

  OK(sdis_device_ref_put(dev));
  CHK(mem_allocated_size() == 0);
  return 0;
}

