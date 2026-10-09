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

#ifndef TEST_SSP_RAN_TRIANGLE_H
#define TEST_SSP_RAN_TRIANGLE_H

#include "ssp.h"
#include "test_ssp_utils.h"

#include <rsys/float4.h>

#define NSAMPS 128

#endif /* TEST_SSP_RAN_TRIANGLE_H */

#if TYPE_FLOAT==0
  #define REAL double
  #define TEST test_double
  #define RNG_UNIFORM_R ssp_rng_uniform_double
  #define RAN_TRIANGLE_UNIFORM ssp_ran_triangle_uniform
  #define RAN_TRIANGLE_UNIFORM_PDF ssp_ran_triangle_uniform_pdf
  #define EQ_EPS_R eq_eps
  #define R3 d3
  #define R3_DOT d3_dot
  #define R3_SUB d3_sub
  #define R3_MINUS d3_minus
  #define R3_CROSS d3_cross
  #define R3_LEN d3_len

#elif TYPE_FLOAT==1
  #define REAL float
  #define TEST test_float
  #define RNG_UNIFORM_R ssp_rng_uniform_float
  #define RAN_TRIANGLE_UNIFORM ssp_ran_triangle_uniform_float
  #define RAN_TRIANGLE_UNIFORM_PDF ssp_ran_triangle_uniform_float_pdf
  #define EQ_EPS_R eq_epsf
  #define R3 f3
  #define R3_DOT f3_dot
  #define R3_SUB f3_sub
  #define R3_MINUS f3_minus
  #define R3_CROSS f3_cross
  #define R3_LEN f3_len

#else
  #error "TYPE_FLOAT must be defined either 0 or 1"
#endif

static void
TEST()
{
  struct ssp_rng* rng;
  struct mem_allocator allocator;
  REAL samps[NSAMPS][3];
  REAL A[3], B[3], C[3];
  REAL v0[3], v1[3], v2[3], m0[3], m1[3], m2[3];
  REAL plane[4];
  REAL pdf;
  size_t counter[2];
  size_t nsteps;
  size_t i;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);

  FOR_EACH(i, 0, 3) {
    A[i] = RNG_UNIFORM_R(rng, 0, 1);
    B[i] = RNG_UNIFORM_R(rng, 0, 1);
    C[i] = RNG_UNIFORM_R(rng, 0, 1);
  }

  R3_SUB(v0, B, A);
  R3_SUB(v1, C, A);
  R3_SUB(v2, C, B);
  R3_MINUS(m0, v0);
  R3_MINUS(m1, v1);
  R3_MINUS(m2, v2);
  R3_CROSS(plane, v0, v1);
  plane[3] = -R3_DOT(plane, C);

  FOR_EACH(i, 0, NSAMPS) {
    REAL tmp0[3], tmp1[3];
    REAL dot = 0;
    REAL area = 0;

    CHK(RAN_TRIANGLE_UNIFORM(rng, A, B, C, samps[i], &pdf) == samps[i]);
    CHK(EQ_EPS_R(R3_DOT(plane, samps[i]), -plane[3], (REAL)1.e-6) == 1);

    R3_SUB(tmp0, samps[i], A);
    dot = R3_DOT(R3_CROSS(tmp0, tmp0, v0), R3_CROSS(tmp1, v1, v0));
    CHK(sign(dot) == 1);
    R3_SUB(tmp0, samps[i], B);
    dot = R3_DOT(R3_CROSS(tmp0, tmp0, v2), R3_CROSS(tmp1, m0, v2));
    CHK(sign(dot) == 1);
    R3_SUB(tmp0, samps[i], C);
    dot = R3_DOT(R3_CROSS(tmp0, tmp0, m1), R3_CROSS(tmp1, m2, m1));
    CHK(sign(dot) == 1);

    area = R3_LEN(tmp1) * 0.5f;
    CHK(EQ_EPS_R(pdf, RAN_TRIANGLE_UNIFORM_PDF(A, B, C), (REAL)1.e-8) == 1);
    CHK(EQ_EPS_R(1 / area, pdf, (REAL)1.e-6) == 1);
  }

  nsteps = 10000;
  counter[0] = counter[1] = 0;
  R3(A, -1, 0, 0);
  R3(B,  1, 0, 0);
  R3(C,  0, 1, 0);
  FOR_EACH(i, 0, nsteps) {
    RAN_TRIANGLE_UNIFORM(rng, A, B, C, samps[0], NULL);
    if(samps[0][0] < 0.0)
      counter[0] += 1;
    else
      counter[1] += 1;
  }
  CHK(labs((long)(counter[1] - counter[0])) < 100);

  counter[0] = counter[1] = 0;
  FOR_EACH(i, 0, nsteps) {
    RAN_TRIANGLE_UNIFORM(rng, A, B, C, samps[0], NULL);
    if(samps[0][1] < 1 - 1/sqrt(2))
      counter[0] += 1;
    else
      counter[1] += 1;
  }
  CHK(labs((long)(counter[1] - counter[0])) < 100);

  ssp_rng_ref_put(rng);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
}

#undef REAL
#undef TEST
#undef RNG_UNIFORM_R
#undef RAN_TRIANGLE_UNIFORM
#undef RAN_TRIANGLE_UNIFORM_PDF
#undef EQ_EPS_R
#undef R3
#undef R3_DOT
#undef R3_SUB
#undef R3_MINUS
#undef R3_CROSS
#undef R3_LEN
#undef TYPE_FLOAT
