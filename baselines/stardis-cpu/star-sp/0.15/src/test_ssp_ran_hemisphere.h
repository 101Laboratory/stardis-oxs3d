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

#ifndef TEST_SSP_RAN_HEMISPHERE_H
#define TEST_SSP_RAN_HEMISPHERE_H

#include "ssp.h"
#include "test_ssp_utils.h"

#include <rsys/double4.h>
#include <rsys/float4.h>

#define NSAMPS 128

#endif /* TEST_SSP_RAN_HEMISPHERE_H */

#if TYPE_FLOAT==0
  #define REAL double
  #define TEST test_double
  #define RNG_UNIFORM_R ssp_rng_uniform_double
  #define RAN_HEMISPHERE_UNIFORM_LOCAL ssp_ran_hemisphere_uniform_local
  #define RAN_HEMISPHERE_UNIFORM_LOCAL_PDF ssp_ran_hemisphere_uniform_local_pdf
  #define RAN_HEMISPHERE_COS_LOCAL ssp_ran_hemisphere_cos_local
  #define RAN_HEMISPHERE_COS_LOCAL_PDF ssp_ran_hemisphere_cos_local_pdf
  #define RAN_HEMISPHERE_UNIFORM ssp_ran_hemisphere_uniform
  #define RAN_HEMISPHERE_COS ssp_ran_hemisphere_cos
  #define RAN_HEMISPHERE_COS_PDF ssp_ran_hemisphere_cos_pdf
  #define RAN_HEMISPHERE_UNIFORM_PDF ssp_ran_hemisphere_uniform_pdf
  #define R3_NORMALIZE d3_normalize
  #define R3_IS_NORMALIZED d3_is_normalized
  #define R3_DOT d3_dot
  #define R4_EQ_EPS d4_eq_eps
  #define R4_EQ d4_eq
  #define R3_EQ_EPS d3_eq_eps
  #define EQ_EPS_R eq_eps
  #define R33_BASIS d33_basis
  #define R33_MULR3 d33_muld3

#elif TYPE_FLOAT==1
  #define REAL float
  #define TEST test_float
  #define RNG_UNIFORM_R ssp_rng_uniform_float
  #define RAN_HEMISPHERE_UNIFORM_LOCAL ssp_ran_hemisphere_uniform_float_local
  #define RAN_HEMISPHERE_UNIFORM_LOCAL_PDF ssp_ran_hemisphere_uniform_float_local_pdf
  #define RAN_HEMISPHERE_COS_LOCAL ssp_ran_hemisphere_cos_float_local
  #define RAN_HEMISPHERE_COS_LOCAL_PDF ssp_ran_hemisphere_cos_float_local_pdf
  #define RAN_HEMISPHERE_UNIFORM ssp_ran_hemisphere_uniform_float
  #define RAN_HEMISPHERE_COS ssp_ran_hemisphere_cos_float
  #define RAN_HEMISPHERE_COS_PDF ssp_ran_hemisphere_cos_float_pdf
  #define RAN_HEMISPHERE_UNIFORM_PDF ssp_ran_hemisphere_uniform_float_pdf
  #define R3_NORMALIZE f3_normalize
  #define R3_IS_NORMALIZED f3_is_normalized
  #define R3_DOT f3_dot
  #define R4_EQ_EPS f4_eq_eps
  #define R4_EQ f4_eq
  #define R3_EQ_EPS f3_eq_eps
  #define EQ_EPS_R eq_eps
  #define R33_BASIS f33_basis
  #define R33_MULR3 f33_mulf3

#else
  #error "TYPE_FLOAT must be defined either 0 or 1"
#endif

static void
TEST()
{
  struct ssp_rng* rng0, *rng1;
  struct mem_allocator allocator;
  REAL samps0[NSAMPS][4];
  REAL samps1[NSAMPS][4];
  REAL samps2[NSAMPS][4];
  REAL samps3[NSAMPS][4];
  int i = 0, j = 0;
  REAL* f;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng0) == RES_OK);
  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng1) == RES_OK);

  samps0[0][0] = 0; samps0[0][1] = 0; samps0[0][2] = 1;
  f = RAN_HEMISPHERE_UNIFORM(rng1, samps0[0], samps1[0], NULL);
  f = RAN_HEMISPHERE_UNIFORM(rng1, samps0[0], samps2[0], &samps2[0][3]);

  ssp_rng_set(rng0, 0);
  FOR_EACH(i, 0, NSAMPS) {
    REAL frame[9];
    REAL up[3] = {0, 0, 1};
    REAL xyz[3];
    uint64_t seed = ssp_rng_get(rng0);

    ssp_rng_set(rng1, seed);
    f = RAN_HEMISPHERE_UNIFORM_LOCAL(rng1, samps0[i], &samps0[i][3]);
    CHK(f == samps0[i]);
    CHK(R3_IS_NORMALIZED(f));
    CHK(EQ_EPS_R(f[3], (1/(2*(REAL)PI)), (REAL)1.e-6) == 1);
    CHK(EQ_EPS_R(f[3], RAN_HEMISPHERE_UNIFORM_LOCAL_PDF(f), (REAL)1.e-6) == 1);
    CHK(R3_DOT(f, up) >= 0);

    ssp_rng_set(rng1, seed);
    f = RAN_HEMISPHERE_UNIFORM(rng1, up, samps1[i], &samps1[i][3]);
    CHK(f == samps1[i]);
    CHK(R4_EQ_EPS(f, samps0[i], (REAL)1.e-6) == 1);

    up[0] = RNG_UNIFORM_R(rng1, -1, 1);
    up[1] = RNG_UNIFORM_R(rng1, -1, 1);
    up[2] = RNG_UNIFORM_R(rng1, -1, 1);
    R3_NORMALIZE(up, up);

    ssp_rng_set(rng1, seed);
    f = RAN_HEMISPHERE_UNIFORM(rng1, up, samps1[i], &samps1[i][3]);
    CHK(R3_EQ_EPS(samps0[i], samps1[i], (REAL)1.e-6) == 0);
    CHK(R3_IS_NORMALIZED(f));
    CHK(R3_DOT(f, up) >= 0);
    CHK(EQ_EPS_R(f[3], 1/(2*(REAL)PI), (REAL)1.e-6 ));
    CHK(EQ_EPS_R(f[3], RAN_HEMISPHERE_UNIFORM_PDF(up, f), (REAL)1.e-6));

    R33_BASIS(frame, up);
    R33_MULR3(xyz, frame, samps0[i]);
    CHK(R3_EQ_EPS(samps1[i], xyz, (REAL)1.e-6) == 1);
    FOR_EACH(j, 0, i) {
      CHK(R3_EQ_EPS(samps0[i], samps0[j], (REAL)1.e-6) == 0);
      CHK(R3_EQ_EPS(samps1[i], samps1[j], (REAL)1.e-6) == 0);
    }
  }

  samps1[1][0] = RNG_UNIFORM_R(rng1, -1, 1);
  samps1[1][1] = RNG_UNIFORM_R(rng1, -1, 1);
  samps1[1][2] = RNG_UNIFORM_R(rng1, -1, 1);
  R3_NORMALIZE(samps1[1], samps1[1]);

  ssp_rng_set(rng0, 0);
  RAN_HEMISPHERE_UNIFORM(rng0, samps1[1], samps0[0], &samps0[0][3]);
  ssp_rng_set(rng0, 0);
  RAN_HEMISPHERE_UNIFORM(rng0, samps1[1], samps1[1], &samps1[1][3]);
  CHK(R4_EQ(samps0[0], samps1[1]) == 1);

  ssp_rng_set(rng0, 0);
  FOR_EACH(i, 0, NSAMPS) {
    REAL frame[9];
    REAL up[3] = { 0, 0, 1 };
    REAL xyz[3];
    uint64_t seed = ssp_rng_get(rng0);

    ssp_rng_set(rng1, seed);
    f = RAN_HEMISPHERE_COS_LOCAL(rng1, samps2[i], &samps2[i][3]);
    CHK(f == samps2[i]);
    CHK(R3_EQ_EPS(samps0[i], samps2[i], (REAL)1.e-6) == 0);
    CHK(R3_IS_NORMALIZED(f) == 1);
    CHK(EQ_EPS_R(f[3], f[2]/(REAL)PI, (REAL)1.e-6) == 1);
    CHK(EQ_EPS_R(f[3], RAN_HEMISPHERE_COS_LOCAL_PDF(f), (REAL)1.e-6) == 1);
    CHK(R3_DOT(f, up) >= 0);

    ssp_rng_set(rng1, seed);
    f = RAN_HEMISPHERE_COS(rng1, up, samps3[i], &samps3[i][3]);
    CHK(f == samps3[i]);
    CHK(R4_EQ_EPS(f, samps2[i], (REAL)1.e-6) == 1);

    up[0] = RNG_UNIFORM_R(rng1, -1, 1);
    up[1] = RNG_UNIFORM_R(rng1, -1, 1);
    up[2] = RNG_UNIFORM_R(rng1, -1, 1);
    R3_NORMALIZE(up, up);

    ssp_rng_set(rng1, seed);
    f = RAN_HEMISPHERE_COS(rng1, up, samps3[i], &samps3[i][3]);
    CHK(R3_EQ_EPS(samps2[i], samps3[i], (REAL)1.e-6) == 0);
    CHK(R3_EQ_EPS(samps1[i], samps3[i], (REAL)1.e-6) == 0);
    CHK(R3_IS_NORMALIZED(f) == 1);
    CHK(R3_DOT(f, up) >= 0.f);
    CHK(EQ_EPS_R(f[3], R3_DOT(f, up)/PI, (REAL)1.e-6) == 1);
    CHK(EQ_EPS_R(f[3], RAN_HEMISPHERE_COS_PDF(f, up), (REAL)1.e-6) == 1);

    R33_BASIS(frame, up);
    R33_MULR3(xyz, frame, samps2[i]);
    CHK(R3_EQ_EPS(samps3[i], xyz, (REAL)1.e-6) == 1);
    FOR_EACH(j, 0, i) {
      CHK(R3_EQ_EPS(samps2[i], samps2[j], (REAL)1.e-6) == 0);
      CHK(R3_EQ_EPS(samps3[i], samps3[j], (REAL)1.e-6) == 0);
    }
  }

  samps1[1][0] = RNG_UNIFORM_R(rng1, -1, 1);
  samps1[1][1] = RNG_UNIFORM_R(rng1, -1, 1);
  samps1[1][2] = RNG_UNIFORM_R(rng1, -1, 1);
  R3_NORMALIZE(samps1[1], samps1[1]);

  ssp_rng_set(rng0, 0);
  RAN_HEMISPHERE_COS(rng0, samps1[1], samps0[0], &samps0[0][3]);
  ssp_rng_set(rng0, 0);
  RAN_HEMISPHERE_COS(rng0, samps1[1], samps1[1], &samps1[1][3]);
  CHK(R4_EQ(samps0[0], samps1[1]) == 1);

  ssp_rng_ref_put(rng0);
  ssp_rng_ref_put(rng1);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);

  CHK(mem_allocated_size() == 0);
}

#undef REAL
#undef TEST
#undef RNG_UNIFORM_R
#undef RAN_HEMISPHERE_UNIFORM_LOCAL
#undef RAN_HEMISPHERE_UNIFORM_LOCAL_PDF
#undef RAN_HEMISPHERE_COS_LOCAL
#undef RAN_HEMISPHERE_COS_LOCAL_PDF
#undef RAN_HEMISPHERE_UNIFORM
#undef RAN_HEMISPHERE_COS
#undef RAN_HEMISPHERE_COS_PDF
#undef RAN_HEMISPHERE_UNIFORM_PDF
#undef R3_NORMALIZE
#undef R3_IS_NORMALIZED
#undef R3_DOT
#undef R4_EQ_EPS
#undef R4_EQ
#undef R3_EQ_EPS
#undef EQ_EPS_R
#undef R33_BASIS
#undef R33_MULR3
#undef TYPE_FLOAT
