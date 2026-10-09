/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#include "suvm.h"
#include "test_suvm_utils.h"

#include <rsys/logger.h>

static void
log_stream(const char* msg, void* ctx)
{
  ASSERT(msg);
  (void)msg, (void)ctx;
  printf("%s\n", msg);
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct logger logger;
  struct suvm_device* suvm;
  (void)argc, (void)argv;

  CHK(suvm_device_create(NULL, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(suvm_device_create(NULL, NULL, 0, &suvm) == RES_OK);

  CHK(suvm_device_ref_get(NULL) == RES_BAD_ARG);
  CHK(suvm_device_ref_get(suvm) == RES_OK);
  CHK(suvm_device_ref_put(NULL) == RES_BAD_ARG);
  CHK(suvm_device_ref_put(suvm) == RES_OK);
  CHK(suvm_device_ref_put(suvm) == RES_OK);

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(suvm_device_create(NULL, &allocator, 1, &suvm) == RES_OK);
  CHK(suvm_device_ref_put(suvm) == RES_OK);

  CHK(logger_init(&allocator, &logger) == RES_OK);
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  CHK(suvm_device_create(&logger, &allocator, 0, &suvm) == RES_OK);
  CHK(suvm_device_ref_put(suvm) == RES_OK);
  CHK(suvm_device_create(&logger, NULL, 0, &suvm) == RES_OK);
  CHK(suvm_device_ref_put(suvm) == RES_OK);

  logger_release(&logger);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
