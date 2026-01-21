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

#ifndef TEST_SSP_RAN_TETRA_H
#define TEST_SSP_RAN_TETRA_H

#include "ssp.h"
#include "test_ssp_utils.h"

#include <rsys/float4.h>

#define NSAMPS 128

#endif /* TEST_SSP_RAN_TETRA_H */

#if TYPE_FLOAT==0
  #define REAL double
  #define TEST test_double
  #define RNG_UNIFORM_R ssp_rng_uniform_double
  #define RAN_TETRA_UNIFORM ssp_ran_tetrahedron_uniform
  #define RAN_TETRA_UNIFORM_PDF ssp_ran_tetrahedron_uniform_pdf
  #define EQ_EPS_R eq_eps
  #define FABS_R fabs
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
  #define RAN_TETRA_UNIFORM ssp_ran_tetrahedron_uniform_float
  #define RAN_TETRA_UNIFORM_PDF ssp_ran_tetrahedron_uniform_float_pdf
  #define EQ_EPS_R eq_epsf
  #define FABS_R absf
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
  REAL A[3], B[3], C[3], D[3];
  REAL v0[3], v1[3], v2[3], v3[3], v4[3];
  REAL in0[3], in1[3], in2[3], in3[3];
  REAL pdf[NSAMPS];
  size_t i, j;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);

  FOR_EACH(j, 0, 100) {
    FOR_EACH(i, 0, 3) {
      A[i] = RNG_UNIFORM_R(rng, 0, 1);
      B[i] = RNG_UNIFORM_R(rng, 0, 1);
      C[i] = RNG_UNIFORM_R(rng, 0, 1);
      D[i] = RNG_UNIFORM_R(rng, 0, 1);
    }

    R3_SUB(v0, B, A);
    R3_SUB(v1, C, A);
    R3_SUB(v2, C, B);
    R3_SUB(v3, D, A);
    R3_SUB(v4, D, C);
    /* Inward vectors perpandicular to faces */
    R3_CROSS(in0, v0, v1);
    if (R3_DOT(in0, v3) < 0) R3_MINUS(in0, in0);
    R3_CROSS(in1, v1, v3);
    if (R3_DOT(in1, v0) < 0) R3_MINUS(in1, in1);
    R3_CROSS(in2, v3, v0);
    if (R3_DOT(in2, v1) < 0) R3_MINUS(in2, in2);
    R3_CROSS(in3, v2, v4);
    if (R3_DOT(in3, v3) > 0) R3_MINUS(in3, in3);

    FOR_EACH(i, 0, NSAMPS) {
      REAL X[3], tmp[3];
      REAL dot = 0;
      REAL vol = 0;

      CHK(RAN_TETRA_UNIFORM(rng, A, B, C, D, samps[i], &pdf[i]) == samps[i]);

      /* Check sample is in the tetrahedron */
      R3_SUB(X, samps[i], A);
      dot = R3_DOT(X, in0);
      CHK(sign(dot) == 1);
      dot = R3_DOT(X, in1);
      CHK(sign(dot) == 1);
      dot = R3_DOT(X, in2);
      CHK(sign(dot) == 1);
      dot = R3_DOT(X, in3);
      CHK(sign(dot) == -1);

      /* Check pdf */
      vol = FABS_R(R3_DOT(R3_CROSS(tmp, v0, v1), v3)) / 6;
      CHK(EQ_EPS_R(pdf[i], RAN_TETRA_UNIFORM_PDF(A, B, C, D), (REAL)1.e-8) == 1);
      /* Pdf is 1/vol
       * But numerical value depends on the vector used to compute it */
      CHK(EQ_EPS_R(vol * pdf[i], 1, (REAL)1.e-5) == 1);
    }
  }

  ssp_rng_ref_put(rng);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
}

#undef REAL
#undef TEST
#undef RNG_UNIFORM_R
#undef RAN_TETRA_UNIFORM
#undef RAN_TETRA_UNIFORM_PDF
#undef EQ_EPS_R
#undef FABS_R
#undef R3
#undef R3_DOT
#undef R3_SUB
#undef R3_MINUS
#undef R3_CROSS
#undef R3_LEN
#undef TYPE_FLOAT
