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
#include <rsys/math.h>

#include <rsys_math.h>

static res_T
rcp_x
  (void* value,
   struct ssp_rng* rng,
   const unsigned ithread,
   const uint64_t irealisation,
   void* ctx)
{
  double* result = value;
  double samp;
  (void)ithread, (void)irealisation;
  CHK(value != NULL);
  CHK(rng != NULL);
  CHK(ctx == NULL);
  samp = ssp_rng_uniform_double(rng, 1.0, 4.0);
  *result = 1.0 / samp * 3;
  return RES_OK;
}

static res_T
cos_x
  (void* value,
   struct ssp_rng* rng,
   const unsigned ithread,
   const uint64_t irealisation,
   void* ctx)
{
  float* result = value;
  double samp;
  (void)ithread, (void)irealisation;
  CHK(value != NULL);
  CHK(rng != NULL);
  CHK(ctx == (void*)0xC0DE);
  samp = ssp_rng_uniform_double(rng, -PI/4.0, PI/4.0);
  *result = (float)(cos(samp) * PI / 2.0);
  return RES_OK;
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
  CHK(smc_device_create(&args, &dev) == RES_OK);

  integrator.integrand = rcp_x;
  integrator.type = &smc_double;
  integrator.max_realisations = 10000;

  CHK(smc_solve(NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(smc_solve(dev, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(smc_solve(NULL, &integrator, NULL, NULL) == RES_BAD_ARG);
  CHK(smc_solve(dev, &integrator, NULL, NULL) == RES_BAD_ARG);
  CHK(smc_solve(NULL, NULL, NULL, &estimator) == RES_BAD_ARG);
  CHK(smc_solve(dev, NULL, NULL, &estimator) == RES_BAD_ARG);
  CHK(smc_solve(NULL, &integrator, NULL, &estimator) == RES_BAD_ARG);
  CHK(smc_solve(dev, &integrator, NULL, &estimator) == RES_OK);

  CHK(smc_estimator_get_status(NULL, NULL) == RES_BAD_ARG);
  CHK(smc_estimator_get_status(estimator, NULL) == RES_BAD_ARG);
  CHK(smc_estimator_get_status(NULL, &status) == RES_BAD_ARG);
  CHK(smc_estimator_get_status(estimator, &status) == RES_OK);
  printf("Integral[1, 4] 1/x dx = %g; E = %g; SE = %g\n",
    log(4.0) - log(1.0), SMC_DOUBLE(status.E), SMC_DOUBLE(status.SE));
  CHK(eq_eps
    (log(4.0) - log(1.0), SMC_DOUBLE(status.E), 2.0 * SMC_DOUBLE(status.SE)));
  CHK(smc_estimator_ref_put(estimator) == RES_OK);

  integrator.type = NULL;
  CHK(smc_solve(dev, &integrator, NULL, &estimator) == RES_BAD_ARG);
  integrator.type = &smc_double;
  integrator.integrand = NULL;
  CHK(smc_solve(dev, &integrator, NULL, &estimator) == RES_BAD_ARG);

  integrator.integrand = cos_x;
  integrator.type = &smc_float;
  CHK(smc_solve(dev, &integrator, (void*)0xC0DE, &estimator) == RES_OK);
  CHK(smc_estimator_get_status(estimator, &status) == RES_OK);
  printf("Integral[-PI/4, PI/4] cos(x) dx = %f; E = %f; SE = %f\n",
     2*sin(PI/4.0), SMC_FLOAT(status.E), SMC_FLOAT(status.SE));
  CHK(eq_eps
    ((float)2*sin(PI/4.0), SMC_FLOAT(status.E), 2 * SMC_FLOAT(status.SE)));
  CHK(smc_device_ref_put(dev) == RES_OK);

  CHK(smc_estimator_ref_put(estimator) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

