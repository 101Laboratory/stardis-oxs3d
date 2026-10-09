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

#include <rsys_math.h>

enum {
  WEIGHT,
  WEIGHT_SENSIBILITY_ALPHA,
  WEIGHTS_COUNT
};

struct alpha { double value; };

static res_T
cos_x
  (void* value,
   struct ssp_rng* rng,
   const unsigned ithread,
   const uint64_t irealisation,
   void* context)
{
  double* result = SMC_DOUBLEN(value);
  double samp;
  struct smc_doubleN_context* ctx = context;
  const struct alpha* alpha = ctx->integrand_data;
  (void)ithread, (void)irealisation;
  CHK(value != NULL);
  CHK(rng != NULL);
  samp = ssp_rng_uniform_double(rng, -PI/4.0, PI/4.0);

  result[WEIGHT] = cos(alpha->value*samp) * PI / 2.0;
  result[WEIGHT_SENSIBILITY_ALPHA] = -samp * sin(alpha->value*samp) * PI/2.0;

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
  struct smc_doubleN_context ctx;
  struct alpha alpha;
  const double a = 0.4;
  double result;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  args.allocator = &allocator;
  CHK(smc_device_create(&args, &dev) == RES_OK);

  integrator.integrand = cos_x;
  integrator.type = &smc_doubleN;
  integrator.max_realisations = 100000;
  alpha.value = a;
  ctx.count = WEIGHTS_COUNT;
  ctx.integrand_data = &alpha;

  CHK(smc_solve(dev, &integrator, NULL, &estimator) == RES_MEM_ERR);
  ctx.count = 0;
  CHK(smc_solve(dev, &integrator, &ctx, &estimator) == RES_MEM_ERR);
  ctx.count = WEIGHTS_COUNT;
  CHK(smc_solve(dev, &integrator, &ctx, &estimator) == RES_OK);
  CHK(smc_estimator_get_status(estimator, &status) == RES_OK);

  /* Check the estimated result */
  result = 2*sin(a*PI/4.0) / a;
  printf("Integral[-PI/4, PI/4] cos(%g*x) dx = %g ~ %g +/- %g\n",
     a, result,
     SMC_DOUBLEN(status.E)[WEIGHT],
     SMC_DOUBLEN(status.SE)[WEIGHT]);
  CHK(eq_eps
    (result,
     SMC_DOUBLEN(status.E)[WEIGHT],
     SMC_DOUBLEN(status.SE)[WEIGHT]));

  /* Check the estimated sensibility in alpha */
  result = 2*(a*PI/4.0*cos(a*PI/4.0) - sin(a*PI/4.0)) / (a*a);
  printf("Integral'[-PI/4, PI/4] cos(%g*x) dx = %g ~ %g +/- %g\n",
     a, result,
     SMC_DOUBLEN(status.E)[WEIGHT_SENSIBILITY_ALPHA],
     SMC_DOUBLEN(status.SE)[WEIGHT_SENSIBILITY_ALPHA]);
  CHK(eq_eps
    (result,
     SMC_DOUBLEN(status.E)[WEIGHT_SENSIBILITY_ALPHA],
     SMC_DOUBLEN(status.SE)[WEIGHT_SENSIBILITY_ALPHA]));

  CHK(smc_estimator_ref_put(estimator) == RES_OK);
  CHK(smc_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
