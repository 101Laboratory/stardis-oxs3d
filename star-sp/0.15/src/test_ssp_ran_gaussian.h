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

#ifndef TEST_SSP_RAN_GAUSSIAN_H
#define TEST_SSP_RAN_GAUSSIAN_H

#include "ssp.h"
#include "test_ssp_utils.h"

#include <string.h>
#include <rsys/clock_time.h>

#define NBS 1000000

#endif /* TEST_SSP_RAN_GAUSSIAN_H */

#if TYPE_FLOAT==0
  #define REAL double
  #define TEST test_double
  #define RAN_GAUSSIAN ssp_ran_gaussian
  #define RANST_GAUSSIAN ssp_ranst_gaussian
  #define RANST_GAUSSIAN_CREATE ssp_ranst_gaussian_create
  #define RANST_GAUSSIAN_REF_GET ssp_ranst_gaussian_ref_get
  #define RANST_GAUSSIAN_REF_PUT ssp_ranst_gaussian_ref_put
  #define RANST_GAUSSIAN_SETUP ssp_ranst_gaussian_setup
  #define RANST_GAUSSIAN_GET ssp_ranst_gaussian_get
  #define SQRT sqrt

#elif TYPE_FLOAT==1
  #define REAL float
  #define TEST test_float
  #define RAN_GAUSSIAN ssp_ran_gaussian_float
  #define RANST_GAUSSIAN ssp_ranst_gaussian_float
  #define RANST_GAUSSIAN_CREATE ssp_ranst_gaussian_float_create
  #define RANST_GAUSSIAN_REF_GET ssp_ranst_gaussian_float_ref_get
  #define RANST_GAUSSIAN_REF_PUT ssp_ranst_gaussian_float_ref_put
  #define RANST_GAUSSIAN_SETUP ssp_ranst_gaussian_float_setup
  #define RANST_GAUSSIAN_GET ssp_ranst_gaussian_float_get
  #define SQRT (float)sqrt

#else
  #error "TYPE_FLOAT must be defined either 0 or 1"
#endif

static void
TEST()
{
  struct ssp_rng* rng;
  struct mem_allocator allocator;
  struct RANST_GAUSSIAN *gaussian;
  int i;
  REAL x = 0, x2 = 0;
  REAL exp_mean = 10, mean;
  REAL exp_std = 3, std;
  struct time start, end, res;
  char dump[512];

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);

  CHK(RANST_GAUSSIAN_CREATE(NULL, NULL) == RES_BAD_ARG);
  CHK(RANST_GAUSSIAN_CREATE(NULL, &gaussian) == RES_OK);
  CHK(RANST_GAUSSIAN_REF_PUT(gaussian) == RES_OK);

  CHK(RANST_GAUSSIAN_CREATE(&allocator, NULL) == RES_BAD_ARG);
  CHK(RANST_GAUSSIAN_CREATE(&allocator, &gaussian) == RES_OK);

  CHK(RANST_GAUSSIAN_SETUP(NULL, exp_mean, exp_std) == RES_BAD_ARG);
  CHK(RANST_GAUSSIAN_SETUP(gaussian, exp_mean, -1) == RES_BAD_ARG);
  CHK(RANST_GAUSSIAN_SETUP(gaussian, exp_mean, exp_std) == RES_OK);

  CHK(RANST_GAUSSIAN_REF_GET(NULL) == RES_BAD_ARG);
  CHK(RANST_GAUSSIAN_REF_GET(gaussian) == RES_OK);

  CHK(RANST_GAUSSIAN_REF_PUT(NULL) == RES_BAD_ARG);
  CHK(RANST_GAUSSIAN_REF_PUT(gaussian) == RES_OK);

  time_current(&start);
  FOR_EACH(i, 0, NBS) {
    const REAL r = RANST_GAUSSIAN_GET(gaussian, rng);
    x += r;
    x2 += r * r;
  }
  time_current(&end);
  time_sub(&res, &end, &start);
  time_dump(&res, TIME_SEC | TIME_MSEC | TIME_USEC, NULL, dump, sizeof(dump));
  printf("%s--\n", dump);

  mean = x / NBS;
  std = SQRT(x2 / NBS - mean*mean);
  printf("%g %g\n", mean, std);
  CHK(eq_eps(mean, exp_mean, 1e-2) == 1);
  CHK(eq_eps(std, exp_std, 1e-2) == 1);

  /* Same test with inline gaussian generation */
  x = 0;
  x2 = 0;

  time_current(&start);
  FOR_EACH(i, 0, NBS) {
    const REAL r = RAN_GAUSSIAN(rng, exp_mean, exp_std);
    x += r;
    x2 += r * r;
  }
  time_current(&end);
  time_sub(&res, &end, &start);
  time_dump(&res, TIME_SEC | TIME_MSEC | TIME_USEC, NULL, dump, sizeof(dump));
  printf("%s--\n", dump);

  mean = x / NBS;
  std = SQRT(x2 / NBS - mean*mean);
  printf("%g %g\n", mean, std);
  CHK(eq_eps(mean, exp_mean, 1.e-2) == 1);
  CHK(eq_eps(std, exp_std, 1e-2) == 1);

  CHK(RANST_GAUSSIAN_REF_PUT(gaussian) == RES_OK);

  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
}

#undef TEST
#undef REAL
#undef RAN_GAUSSIAN
#undef RANST_GAUSSIAN
#undef RANST_GAUSSIAN_CREATE
#undef RANST_GAUSSIAN_REF_GET
#undef RANST_GAUSSIAN_REF_PUT
#undef RANST_GAUSSIAN_SETUP
#undef RANST_GAUSSIAN_GET
#undef SQRT
#undef TYPE_FLOAT
