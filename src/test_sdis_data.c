/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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
  if(param->name) CHK(sdis_data_ref_put(param->name) == RES_OK);
}

int
main(int argc, char** argv)
{
  const char* str = "Hello world!";
  struct mem_allocator allocator;
  struct sdis_device* dev = NULL;
  struct sdis_data* data = NULL;
  struct param* param = NULL;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 0, &dev) == RES_OK);

  CHK(sdis_data_create(NULL, 0, 0, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_data_create(dev, 0, 0, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_data_create(NULL, 8, 0, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_data_create(dev, 8, 0, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_data_create(NULL, 0, 8, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_data_create(dev, 0, 8, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_data_create(NULL, 8, 8, NULL, NULL) == RES_BAD_ARG);
  CHK(sdis_data_create(dev, 8, 8, NULL, NULL) == RES_BAD_ARG);

  CHK(sdis_data_create(NULL, 0, 0, NULL, &data) == RES_BAD_ARG);
  CHK(sdis_data_create(dev, 0, 0, NULL, &data) == RES_BAD_ARG);
  CHK(sdis_data_create(NULL, 8, 0, NULL, &data) == RES_BAD_ARG);
  CHK(sdis_data_create(dev, 8, 0, NULL, &data) == RES_BAD_ARG);
  CHK(sdis_data_create(NULL, 0, 8, NULL, &data) == RES_BAD_ARG);
  CHK(sdis_data_create(dev, 0, 8, NULL, &data) == RES_BAD_ARG);
  CHK(sdis_data_create(NULL, 8, 8, NULL, &data) == RES_BAD_ARG);
  CHK(sdis_data_create(dev, 8, 8, NULL, &data) == RES_OK);

  CHK(sdis_data_get(data) != NULL);
  CHK(sdis_data_cget(data) == sdis_data_get(data));
  CHK(IS_ALIGNED(sdis_data_get(data), 8));

  CHK(sdis_data_ref_get(NULL) == RES_BAD_ARG);
  CHK(sdis_data_ref_get(data) == RES_OK);
  CHK(sdis_data_ref_put(NULL) == RES_BAD_ARG);
  CHK(sdis_data_ref_put(data) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  CHK(sdis_data_create
    (dev, sizeof(struct param), 64, param_release, &data) == RES_OK);
  param = sdis_data_get(data);
  CHK(sdis_data_create
    (dev, strlen(str)+1, ALIGNOF(char), NULL, &param->name) == RES_OK);
  strcpy(sdis_data_get(param->name), str);
  param->d = 3.14159;
  param->i = 314159;

  param = sdis_data_get(data);
  CHK(strcmp(sdis_data_cget(param->name), str) == 0);
  CHK(param->d == 3.14159);
  CHK(param->i == 314159);

  CHK(sdis_data_ref_put(data) == RES_OK);

  CHK(sdis_device_ref_put(dev) == RES_OK);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

