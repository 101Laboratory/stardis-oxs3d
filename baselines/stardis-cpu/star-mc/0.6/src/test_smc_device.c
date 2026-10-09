/* Copyright (C) 2015-2018, 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#include "smc.h"
#include "test_smc_utils.h"

#include <rsys/logger.h>

#include <star/ssp.h>

#include <omp.h>

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
  enum ssp_rng_type type;
  struct smc_device_create_args args = SMC_DEVICE_CREATE_ARGS_DEFAULT;
  struct smc_device* dev;
  unsigned nthreads;
  (void)argc, (void)argv;

  CHK(smc_device_create(&args, NULL) == RES_BAD_ARG);
  CHK(smc_device_create(&args, &dev) == RES_OK);

  CHK(smc_device_get_threads_count(NULL, NULL) == RES_BAD_ARG);
  CHK(smc_device_get_threads_count(dev, NULL) == RES_BAD_ARG);
  CHK(smc_device_get_threads_count(NULL, &nthreads) == RES_BAD_ARG);
  CHK(smc_device_get_threads_count(dev, &nthreads) == RES_OK);
  CHK(nthreads == (unsigned)omp_get_num_procs());

  CHK(smc_device_ref_get(NULL) == RES_BAD_ARG);
  CHK(smc_device_ref_get(dev) == RES_OK);
  CHK(smc_device_ref_put(NULL) == RES_BAD_ARG);
  CHK(smc_device_ref_put(dev) == RES_OK);
  CHK(smc_device_ref_put(dev) == RES_OK);

  args.nthreads_hint = 0;
  CHK(smc_device_create(&args, &dev) == RES_BAD_ARG);

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);

  args.nthreads_hint = 1;
  args.allocator = &allocator;
  CHK(smc_device_create(&args, NULL) == RES_BAD_ARG);
  CHK(smc_device_create(&args, &dev) == RES_OK);
  CHK(smc_device_ref_put(dev) == RES_OK);
  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);

  CHK(logger_init(&allocator, &logger) == RES_OK);
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  args.logger = &logger;
  args.allocator = NULL;
  CHK(smc_device_create(&args, NULL) == RES_BAD_ARG);
  CHK(smc_device_create(&args, &dev) == RES_OK);
  CHK(smc_device_ref_put(dev) == RES_OK);

  args.logger = &logger;
  args.allocator = &allocator;
  args.nthreads_hint = SMC_NTHREADS_DEFAULT;

  CHK(smc_device_create(&args, NULL) == RES_BAD_ARG);
  CHK(smc_device_create(&args, &dev) == RES_OK);

  CHK(smc_device_set_rng_type(NULL, SSP_RNG_TYPE_NULL) == RES_BAD_ARG);
  CHK(smc_device_set_rng_type(dev, SSP_RNG_TYPE_NULL) == RES_BAD_ARG);
  CHK(smc_device_set_rng_type(NULL, SSP_RNG_RANLUX48) == RES_BAD_ARG);
  CHK(smc_device_set_rng_type(dev, SSP_RNG_RANLUX48) == RES_OK);

  CHK(smc_device_get_rng_type(NULL, NULL) == RES_BAD_ARG);
  CHK(smc_device_get_rng_type(dev, NULL) == RES_BAD_ARG);
  CHK(smc_device_get_rng_type(NULL, &type) == RES_BAD_ARG);
  CHK(smc_device_get_rng_type(dev, &type) == RES_OK);
  CHK(type == SSP_RNG_RANLUX48);

  CHK(smc_device_set_rng_type(dev, SSP_RNG_KISS) == RES_OK);
  CHK(smc_device_get_rng_type(dev, &type) == RES_OK);
  CHK(type == SSP_RNG_KISS);

  CHK(smc_device_ref_put(dev) == RES_OK);

  args.rng_type = SSP_RNG_RANLUX48;
  CHK(smc_device_create(&args, &dev) == RES_OK);
  CHK(smc_device_ref_put(dev) == RES_OK);

  logger_release(&logger);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
