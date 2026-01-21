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

#include <star/ssp.h>

static res_T
compute_1
  (void* value,
   struct ssp_rng* rng,
   const unsigned ithread,
   const uint64_t irealisation,
   void* context)
{
  /* pretend that 10% of the realizations fail */
  int ok = ssp_rng_canonical(rng) > 0.1;
  (void)context, (void)ithread, (void)irealisation;
  if(ok) {
    SMC_DOUBLE(value) = 1; /* weight is 1 */
  } else {
    SMC_DOUBLE(value) = 0; /* clear weight */
  }
  /* a realization has been carried out only if ok */
  return ok ? RES_OK : RES_BAD_ARG;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct smc_device* dev;
  struct smc_device_create_args args = SMC_DEVICE_CREATE_ARGS_DEFAULT;
  struct smc_integrator integrator = SMC_INTEGRATOR_NULL;
  struct smc_estimator* estimator;
  struct smc_estimator_status status;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  args.allocator = &allocator;
  SMC(device_create(&args, &dev));

  integrator.integrand = &compute_1;
  integrator.type = &smc_double;
  integrator.max_realisations = 10000;

  SMC(solve(dev, &integrator, NULL, &estimator));
  SMC(estimator_get_status(estimator, &status));

  /* result must be 1 with std err = 0 */
  CHK(SMC_DOUBLE(status.E) == 1);
  CHK(SMC_DOUBLE(status.SE) == 0);

  printf("OK realizations = %lu; KO realizations = %lu\n",
	 (unsigned long)status.N, (unsigned long)status.NF);

  SMC(estimator_ref_put(estimator));

  /* set auto cancel ON */
  integrator.max_failures = integrator.max_realisations / 1000;

  SMC(solve(dev, &integrator, NULL, &estimator));
  SMC(estimator_get_status(estimator, &status));

  /* it is expected that solve was auto-canceled */
  CHK(integrator.max_realisations != status.N);
  CHK(status.NF >= integrator.max_failures + 1);

  /* result must be 1 with std err = 0 */
  CHK(SMC_DOUBLE(status.E) == 1);
  CHK(SMC_DOUBLE(status.SE) == 0);

  printf("OK realizations = %lu; KO realizations = %lu\n",
	 (unsigned long)status.N, (unsigned long)status.NF);

  SMC(device_ref_put(dev));
  SMC(estimator_ref_put(estimator));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

