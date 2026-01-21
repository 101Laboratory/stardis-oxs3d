/* Copyright (C) 2019, 2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#include "sg3d.h"
#include "test_sg3d_utils.h"

#include <rsys/logger.h>

static INLINE void
log_stream(const char* msg, void* ctx)
{
  ASSERT(msg);
  (void) msg, (void) ctx;
  printf("%s\n", msg);
}

int
main(int argc, char** argv)
{
  struct logger logger;
  struct mem_allocator allocator;
  struct sg3d_device* dev;
  (void)argc, (void)argv;

  BA(sg3d_device_create(NULL, NULL, 0, NULL));
  OK(sg3d_device_create(NULL, NULL, 0, &dev));
  BA(sg3d_device_ref_get(NULL));
  OK(sg3d_device_ref_get(dev));
  BA(sg3d_device_ref_put(NULL));
  OK(sg3d_device_ref_put(dev));
  OK(sg3d_device_ref_put(dev));

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));

  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);
  BA(sg3d_device_create(NULL, &allocator, 0, NULL));
  OK(sg3d_device_create(NULL, &allocator, 0, &dev));
  OK(sg3d_device_ref_put(dev));
  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);

  OK(logger_init(&allocator, &logger));
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  BA(sg3d_device_create(&logger, NULL, 0, NULL));
  OK(sg3d_device_create(&logger, NULL, 1, &dev));
  OK(sg3d_device_ref_put(dev));

  BA(sg3d_device_create(&logger, &allocator, 0, NULL));
  OK(sg3d_device_create(&logger, &allocator, 0, &dev));
  OK(sg3d_device_ref_put(dev));

  OK(sg3d_device_create(&logger, &allocator, 0, &dev));
  OK(sg3d_device_ref_put(dev));

  logger_release(&logger);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

