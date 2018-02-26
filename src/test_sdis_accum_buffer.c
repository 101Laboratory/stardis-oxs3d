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

#include "test_sdis_utils.h"
#include <string.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sdis_accum_buffer* buf = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_accum_buffer_layout layout;
  const struct sdis_accum* accums = NULL;
  struct sdis_accum* accums_tmp = NULL;
  (void)argc, (void)argv;
  
  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 0, &dev) == RES_OK);

  CHK(sdis_accum_buffer_create(NULL, 4 ,4, &buf) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_create(dev, 0 ,4, &buf) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_create(dev, 4 ,0, &buf) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_create(dev, 4 ,0, NULL) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_create(dev, 4 ,4, &buf) == RES_OK);

  CHK(sdis_accum_buffer_ref_get(NULL) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_ref_get(buf) == RES_OK);
  CHK(sdis_accum_buffer_ref_put(NULL) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_ref_put(buf) == RES_OK);
  CHK(sdis_accum_buffer_ref_put(buf) == RES_OK);

  CHK(sdis_accum_buffer_create(dev, 16, 8, &buf) == RES_OK);

  CHK(sdis_accum_buffer_get_layout(NULL, &layout) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_get_layout(buf, NULL) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_get_layout(buf, &layout) == RES_OK);

  CHK(layout.width == 16);
  CHK(layout.height == 8);

  CHK(sdis_accum_buffer_map(NULL, &accums) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_map(buf, NULL) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_map(buf, &accums) == RES_OK);

  /* Check the accessibility to the mapped data */
  accums_tmp = MEM_CALLOC
    (&allocator, layout.width*layout.height, sizeof(struct sdis_accum));
  CHK(accums_tmp != NULL);
  memcpy(accums_tmp, accums_tmp, 
    layout.width*layout.height*sizeof(struct sdis_accum));
  MEM_RM(&allocator, accums_tmp);

  CHK(sdis_accum_buffer_unmap(NULL) == RES_BAD_ARG);
  CHK(sdis_accum_buffer_unmap(buf) == RES_OK);

  CHK(sdis_accum_buffer_ref_put(buf) == RES_OK);
  CHK(sdis_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
