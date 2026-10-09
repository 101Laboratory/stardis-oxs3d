/* Copyright (C) 2015, 2016, 2019, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "sstl.h"

#include <rsys/logger.h>

static void
check_memory_allocator(struct mem_allocator* allocator)
{
  if(MEM_ALLOCATED_SIZE(allocator)) {
    char dump[512];
    MEM_DUMP(allocator, dump, sizeof(dump)/sizeof(char));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks\n");
  }
}

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
  struct logger logger;
  struct mem_allocator allocator;
  struct sstl* sstl;
  (void)argc, (void)argv;

  CHK(sstl_create(NULL, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(sstl_create(NULL, NULL, 0, &sstl) == RES_OK);

  CHK(sstl_ref_get(NULL) == RES_BAD_ARG);
  CHK(sstl_ref_get(sstl) == RES_OK);
  CHK(sstl_ref_put(NULL) == RES_BAD_ARG);
  CHK(sstl_ref_put(sstl) == RES_OK);
  CHK(sstl_ref_put(sstl) == RES_OK);

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(sstl_create(NULL, &allocator, 0, NULL) == RES_BAD_ARG);
  CHK(sstl_create(NULL, &allocator, 0, &sstl) == RES_OK);
  CHK(sstl_ref_put(sstl) == RES_OK);

  CHK(logger_init(&allocator, &logger) == RES_OK);
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  CHK(sstl_create(&logger, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(sstl_create(&logger, NULL, 0, &sstl) == RES_OK);
  CHK(sstl_ref_put(sstl) == RES_OK);
  CHK(sstl_create(&logger, &allocator, 0, NULL) == RES_BAD_ARG);
  CHK(sstl_create(&logger, &allocator, 0, &sstl) == RES_OK);
  CHK(sstl_ref_put(sstl) == RES_OK);

  CHK(sstl_create(&logger, &allocator, 1, NULL) == RES_BAD_ARG);
  CHK(sstl_create(&logger, &allocator, 1, &sstl) == RES_OK);
  CHK(sstl_ref_put(sstl) == RES_OK);

  logger_release(&logger);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

