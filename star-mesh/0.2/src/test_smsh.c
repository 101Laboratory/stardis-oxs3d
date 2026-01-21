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

#include "smsh.h"

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
  struct smsh_create_args args = SMSH_CREATE_ARGS_DEFAULT;
  struct smsh* smsh;
  (void)argc, (void)argv;

  CHK(smsh_create(NULL, &smsh) == RES_BAD_ARG);
  CHK(smsh_create(&args, NULL) == RES_BAD_ARG);
  CHK(smsh_create(&args, &smsh) == RES_OK);

  CHK(smsh_ref_get(NULL) == RES_BAD_ARG);
  CHK(smsh_ref_get(smsh) == RES_OK);
  CHK(smsh_ref_put(NULL) == RES_BAD_ARG);
  CHK(smsh_ref_put(smsh) == RES_OK);
  CHK(smsh_ref_put(smsh) == RES_OK);

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  args.allocator = &allocator;
  args.verbose = 1;
  CHK(smsh_create(&args, &smsh) == RES_OK);
  CHK(smsh_ref_put(smsh) == RES_OK);

  CHK(logger_init(&allocator, &logger) == RES_OK);
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  args.logger = &logger;
  args.verbose = 0;
  CHK(smsh_create(&args, &smsh) == RES_OK);
  CHK(smsh_ref_put(smsh) == RES_OK);
  args.allocator = NULL;
  CHK(smsh_create(&args, &smsh) == RES_OK);
  CHK(smsh_ref_put(smsh) == RES_OK);

  logger_release(&logger);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
