/* Copyright (C) 2015-2025 |Méso|Star> (contact@meso-star.com)
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

#include "ssp.h"
#include "test_ssp_utils.h"

#include <string.h>

#define NSAMPS 1024

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct ssp_ranst_discrete* ran;
  struct ssp_rng* rng;
  const double weights[] = { 0.5, 0.1, 0.2, 0.05, 0.15 };
  const size_t nweights = sizeof(weights)/sizeof(double);
  size_t tmp[sizeof(weights)/sizeof(double)];
  double accum;
  size_t i;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng) == RES_OK);

  CHK(ssp_ranst_discrete_create(NULL, NULL) == RES_BAD_ARG);
  CHK(ssp_ranst_discrete_create(NULL, &ran) == RES_OK);

  CHK(ssp_ranst_discrete_ref_get(NULL) == RES_BAD_ARG);
  CHK(ssp_ranst_discrete_ref_get(ran) == RES_OK);
  CHK(ssp_ranst_discrete_ref_put(NULL) == RES_BAD_ARG);
  CHK(ssp_ranst_discrete_ref_put(ran) == RES_OK);
  CHK(ssp_ranst_discrete_ref_put(ran) == RES_OK);

  CHK(ssp_ranst_discrete_create(&allocator, &ran) == RES_OK);

  CHK(ssp_ranst_discrete_setup(NULL, NULL, 0) == RES_BAD_ARG); 
  CHK(ssp_ranst_discrete_setup(ran, NULL, 0) == RES_BAD_ARG); 
  CHK(ssp_ranst_discrete_setup(NULL, weights, 0) == RES_BAD_ARG); 
  CHK(ssp_ranst_discrete_setup(ran, weights, 0) == RES_BAD_ARG); 
  CHK(ssp_ranst_discrete_setup(NULL, NULL, nweights) == RES_BAD_ARG); 
  CHK(ssp_ranst_discrete_setup(ran, NULL, nweights) == RES_BAD_ARG); 
  CHK(ssp_ranst_discrete_setup(NULL, weights, nweights) == RES_BAD_ARG); 
  CHK(ssp_ranst_discrete_setup(ran, weights, nweights) == RES_OK); 

  memset(tmp, 0, sizeof(tmp));
  FOR_EACH(i, 0, NSAMPS) {
    const size_t k = ssp_ranst_discrete_get(rng, ran);
    double pdf;
    CHK(k < nweights);
    pdf = ssp_ranst_discrete_pdf(k, ran);
    ++tmp[k];
    CHK(pdf == weights[k]);
    CHK(pdf >= 0.f);
    CHK(pdf <= 1.f);
  }
  FOR_EACH(i, 0, nweights) {
    CHK(tmp[i] != 0);
  }

  CHK(ssp_ranst_discrete_setup(ran, weights, nweights-1) == RES_OK);
  FOR_EACH(i, 0, NSAMPS) {
    const size_t k = ssp_ranst_discrete_get(rng, ran);
    double pdf;
    CHK(k < nweights-1);
    pdf = ssp_ranst_discrete_pdf(k, ran);
    CHK(pdf >= 0.f);
    CHK(pdf <= 1.f);
  }
  accum = 0;
  FOR_EACH(i, 0, nweights-1) accum += ssp_ranst_discrete_pdf(i, ran);
  CHK(eq_eps(accum, 1, 1.e-8) == 1);
  CHK(ssp_ranst_discrete_ref_put(ran) == RES_OK);

  CHK(ssp_rng_ref_put(rng) == RES_OK);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

