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

#ifndef TEST_SSP_RAN_PIECEWISE_LINEAR_H
#define TEST_SSP_RAN_PIECEWISE_LINEAR_H

#include "ssp.h"
#include "test_ssp_utils.h"

#define NBS 1000000

#endif /* TEST_SSP_RAN_PIECEWISE_LINEAR_H */

#if TYPE_FLOAT==0
  #define REAL double
  #define TEST test_double
  #define RANST_PIECEWISE_LINEAR ssp_ranst_piecewise_linear
  #define RANST_PIECEWISE_LINEAR_CREATE ssp_ranst_piecewise_linear_create
  #define RANST_PIECEWISE_LINEAR_SETUP ssp_ranst_piecewise_linear_setup
  #define RANST_PIECEWISE_LINEAR_GET ssp_ranst_piecewise_linear_get
  #define RANST_PIECEWISE_LINEAR_PDF ssp_ranst_piecewise_linear_pdf
  #define RANST_PIECEWISE_LINEAR_REF_GET ssp_ranst_piecewise_linear_ref_get
  #define RANST_PIECEWISE_LINEAR_REF_PUT ssp_ranst_piecewise_linear_ref_put
  #define EQ_EPS_R eq_eps
  #define EPS_R DBL_EPSILON
  #define SQRT sqrt

#elif TYPE_FLOAT==1
  #define REAL float
  #define TEST test_float
  #define RANST_PIECEWISE_LINEAR ssp_ranst_piecewise_linear_float
  #define RANST_PIECEWISE_LINEAR_CREATE ssp_ranst_piecewise_linear_float_create
  #define RANST_PIECEWISE_LINEAR_SETUP ssp_ranst_piecewise_linear_float_setup
  #define RANST_PIECEWISE_LINEAR_GET ssp_ranst_piecewise_linear_float_get
  #define RANST_PIECEWISE_LINEAR_PDF ssp_ranst_piecewise_linear_float_pdf
  #define RANST_PIECEWISE_LINEAR_REF_GET ssp_ranst_piecewise_linear_float_ref_get
  #define RANST_PIECEWISE_LINEAR_REF_PUT ssp_ranst_piecewise_linear_float_ref_put
  #define EQ_EPS_R eq_epsf
  #define EPS_R FLT_EPSILON
  #define SQRT (float)sqrt
#else
  #error "TYPE_FLOAT must be defined either 0 or 1"
#endif

static void
TEST()
{
  struct ssp_rng* rng;
  struct mem_allocator allocator;
  struct RANST_PIECEWISE_LINEAR *pwl;
  int i;
  REAL exp_mean = 5, mean;
  REAL exp_std = 10 / SQRT(12) /*sqrt((b - a)² / 12) */, std;
  REAL x = 0, x2 = 0;
  REAL intervals[] = { 0, 1, 3, 5, 7, 8, 10 };
  REAL weights[] = { 1, 1, 1, 1, 1, 1, 1 };

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);

  CHK(RANST_PIECEWISE_LINEAR_CREATE(NULL, NULL) == RES_BAD_ARG);
  CHK(RANST_PIECEWISE_LINEAR_CREATE(NULL, &pwl) == RES_OK);
  CHK(RANST_PIECEWISE_LINEAR_REF_PUT(pwl) == RES_OK);

  CHK(RANST_PIECEWISE_LINEAR_CREATE(&allocator, NULL) == RES_BAD_ARG);
  CHK(RANST_PIECEWISE_LINEAR_CREATE(&allocator, &pwl) == RES_OK);

  CHK(RANST_PIECEWISE_LINEAR_SETUP
    (NULL, intervals, weights, sizeof(intervals)/sizeof(REAL)) == RES_BAD_ARG);
  CHK(RANST_PIECEWISE_LINEAR_SETUP
    (pwl, NULL, weights, sizeof(intervals)/sizeof(REAL)) == RES_BAD_ARG);
  CHK(RANST_PIECEWISE_LINEAR_SETUP
    (pwl, intervals, NULL, sizeof(intervals)/sizeof(REAL)) == RES_BAD_ARG);
  CHK(RANST_PIECEWISE_LINEAR_SETUP
    (pwl, intervals, weights, 1) == RES_BAD_ARG);
  CHK(RANST_PIECEWISE_LINEAR_SETUP
    (pwl, intervals, weights, sizeof(intervals)/sizeof(REAL)) == RES_OK);

  CHK(RANST_PIECEWISE_LINEAR_REF_PUT(pwl) == RES_OK);
  CHK(RANST_PIECEWISE_LINEAR_CREATE(&allocator, &pwl) == RES_OK);

  weights[1] = -1;
  CHK(RANST_PIECEWISE_LINEAR_SETUP
    (pwl, intervals, weights, sizeof(intervals) / sizeof(REAL)) == RES_BAD_ARG);
  weights[1] = 1;

  intervals[1] = 4;
  CHK(RANST_PIECEWISE_LINEAR_SETUP
    (pwl, intervals, weights, sizeof(intervals) / sizeof(REAL)) == RES_BAD_ARG);
  intervals[1] = 1;

  intervals[1] = 3;
  CHK(RANST_PIECEWISE_LINEAR_SETUP
  (pwl, intervals, weights, sizeof(intervals) / sizeof(REAL)) == RES_BAD_ARG);
  intervals[1] = 1;

  CHK(RANST_PIECEWISE_LINEAR_SETUP
    (pwl, intervals, weights, sizeof(intervals) / sizeof(REAL)) == RES_OK);

  CHK(RANST_PIECEWISE_LINEAR_REF_GET(NULL) == RES_BAD_ARG);
  CHK(RANST_PIECEWISE_LINEAR_REF_GET(pwl) == RES_OK);

  CHK(RANST_PIECEWISE_LINEAR_REF_PUT(NULL) == RES_BAD_ARG);
  CHK(RANST_PIECEWISE_LINEAR_REF_PUT(pwl) == RES_OK);

  FOR_EACH(i, 0, NBS) {
    REAL pdf, r;
    r = RANST_PIECEWISE_LINEAR_GET(pwl, rng);
    CHK(0 <= r && r <= 10);
    pdf = RANST_PIECEWISE_LINEAR_PDF(pwl, r);
    CHK(EQ_EPS_R(pdf, (REAL)0.1, EPS_R) == 1);
    x += r;
    x2 += r * r;
  }
  CHK(EQ_EPS_R(RANST_PIECEWISE_LINEAR_PDF(pwl, 0), (REAL)0.1, EPS_R) == 1);
  CHK(EQ_EPS_R(RANST_PIECEWISE_LINEAR_PDF(pwl, 10), (REAL)0.1, EPS_R) == 1);
  CHK(RANST_PIECEWISE_LINEAR_PDF(pwl, -1) == 0);
  CHK(RANST_PIECEWISE_LINEAR_PDF(pwl, 11) == 0);

  mean = x/NBS;
  std = SQRT(x2/NBS - mean*mean);
  printf("%g %g\n", mean, std);
  CHK(EQ_EPS_R(mean, exp_mean, (REAL)1e-2) == 1);
  CHK(EQ_EPS_R(std, exp_std, (REAL)1.e-2) == 1);

  CHK(RANST_PIECEWISE_LINEAR_REF_PUT(pwl) == RES_OK);

  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
}

#undef REAL
#undef TEST
#undef RANST_PIECEWISE_LINEAR
#undef RANST_PIECEWISE_LINEAR_CREATE
#undef RANST_PIECEWISE_LINEAR_SETUP
#undef RANST_PIECEWISE_LINEAR_GET
#undef RANST_PIECEWISE_LINEAR_PDF
#undef RANST_PIECEWISE_LINEAR_REF_GET
#undef RANST_PIECEWISE_LINEAR_REF_PUT
#undef EQ_EPS_R
#undef EPS_R
#undef SQRT
#undef TYPE_FLOAT
