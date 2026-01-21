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
  
  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 0, &dev));

  BA(sdis_accum_buffer_create(NULL, 4 ,4, &buf));
  BA(sdis_accum_buffer_create(dev, 0 ,4, &buf));
  BA(sdis_accum_buffer_create(dev, 4 ,0, &buf));
  BA(sdis_accum_buffer_create(dev, 4 ,0, NULL));
  OK(sdis_accum_buffer_create(dev, 4 ,4, &buf));

  BA(sdis_accum_buffer_ref_get(NULL));
  OK(sdis_accum_buffer_ref_get(buf));
  BA(sdis_accum_buffer_ref_put(NULL));
  OK(sdis_accum_buffer_ref_put(buf));
  OK(sdis_accum_buffer_ref_put(buf));

  OK(sdis_accum_buffer_create(dev, 16, 8, &buf));

  BA(sdis_accum_buffer_get_layout(NULL, &layout));
  BA(sdis_accum_buffer_get_layout(buf, NULL));
  OK(sdis_accum_buffer_get_layout(buf, &layout));

  CHK(layout.width == 16);
  CHK(layout.height == 8);

  BA(sdis_accum_buffer_map(NULL, &accums));
  BA(sdis_accum_buffer_map(buf, NULL));
  OK(sdis_accum_buffer_map(buf, &accums));

  /* Check the accessibility to the mapped data */
  accums_tmp = MEM_CALLOC
    (&allocator, layout.width*layout.height, sizeof(struct sdis_accum));
  CHK(accums_tmp != NULL);
  memmove(accums_tmp, accums_tmp, 
    layout.width*layout.height*sizeof(struct sdis_accum));
  MEM_RM(&allocator, accums_tmp);

  BA(sdis_accum_buffer_unmap(NULL));
  OK(sdis_accum_buffer_unmap(buf));

  OK(sdis_accum_buffer_ref_put(buf));
  OK(sdis_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
