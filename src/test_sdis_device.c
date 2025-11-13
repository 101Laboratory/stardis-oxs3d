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

#include <rsys/logger.h>

#ifdef SDIS_ENABLE_MPI
#include <mpi.h>
#endif

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
  struct sdis_device_create_args args = SDIS_DEVICE_CREATE_ARGS_DEFAULT;
  struct logger logger;
  struct mem_allocator allocator;
  struct sdis_device* dev;
  int is_mpi_used;
#ifdef SDIS_ENABLE_MPI
  int provided;
#endif
  (void)argc, (void)argv;

  args.nthreads_hint = 0;
  args.verbosity = 0;

  BA(sdis_device_create(&args, NULL));
  BA(sdis_device_create(&args, &dev));
  args.nthreads_hint = 1;
  OK(sdis_device_create(&args, &dev));
  BA(sdis_device_ref_get(NULL));
  OK(sdis_device_ref_get(dev));
  BA(sdis_device_ref_put(NULL));
  OK(sdis_device_ref_put(dev));
  OK(sdis_device_ref_put(dev));

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));

  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);

  args.allocator = &allocator;
  args.verbosity = 0;
  BA(sdis_device_create(&args, NULL));
  OK(sdis_device_create(&args, &dev));
  OK(sdis_device_ref_put(dev));
  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);

  OK(logger_init(&allocator, &logger));
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  args.logger = &logger;
  args.allocator = NULL;
  BA(sdis_device_create(&args, NULL));
  OK(sdis_device_create(&args, &dev));
  OK(sdis_device_ref_put(dev));

  args.allocator = &allocator;
  BA(sdis_device_create(&args, NULL));
  OK(sdis_device_create(&args, &dev));
  OK(sdis_device_ref_put(dev));

  args.nthreads_hint = SDIS_NTHREADS_DEFAULT;
  OK(sdis_device_create(&args, &dev));
  BA(sdis_device_is_mpi_used(NULL, &is_mpi_used));
  BA(sdis_device_is_mpi_used(dev, NULL));

  OK(sdis_device_ref_put(dev));

  args.use_mpi = 1;
  args.verbosity = 1;

#ifndef SDIS_ENABLE_MPI
  OK(sdis_device_create(&args, &dev));
  OK(sdis_device_is_mpi_used(dev, &is_mpi_used));
  CHK(!is_mpi_used);
  OK(sdis_device_ref_put(dev));
#else
  CHK(MPI_Init_thread(&argc, &argv, MPI_THREAD_SERIALIZED, &provided) == MPI_SUCCESS);
  OK(sdis_device_create(&args, &dev));
  OK(sdis_device_is_mpi_used(dev, &is_mpi_used));
  CHK(is_mpi_used);
  CHK(MPI_Finalize() == MPI_SUCCESS);
  OK(sdis_device_ref_put(dev));
#endif

  logger_release(&logger);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

