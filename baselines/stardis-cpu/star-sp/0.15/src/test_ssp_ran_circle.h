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

#ifndef TEST_SSP_RAN_CIRCLE_H
#define TEST_SSP_RAN_CIRCLE_H

#include "ssp.h"
#include "test_ssp_utils.h"

#define NSAMPS 128

#endif /* TEST_SSP_RAN_CIRCLE_H */

#if TYPE_FLOAT == 0
#include <rsys/double2.h>

  #define REAL double
  #define TEST test_double
  #define RAN_CIRCLE_UNIFORM ssp_ran_circle_uniform
  #define RAN_CIRCLE_UNIFORM_PDF ssp_ran_circle_uniform_pdf
  #define EQ_EPS eq_eps
  #define R2_EQ_EPS d2_eq_eps
  #define R2_IS_NORMALIZED d2_is_normalized

#elif TYPE_FLOAT == 1
#include <rsys/float2.h>

  #define REAL float
  #define TEST test_float
  #define RAN_CIRCLE_UNIFORM ssp_ran_circle_uniform_float
  #define RAN_CIRCLE_UNIFORM_PDF ssp_ran_circle_uniform_float_pdf
  #define EQ_EPS eq_epsf
  #define R2_EQ_EPS f2_eq_eps
  #define R2_IS_NORMALIZED f2_is_normalized
#else
  #error "TYPE_FLOAT must be defined either 0 or 1"
#endif

static void
TEST(void)
{
  struct ssp_rng* rng;
  struct mem_allocator allocator;
  REAL samps[NSAMPS][4];
  int i = 0, j = 9;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);
  CHK(EQ_EPS(RAN_CIRCLE_UNIFORM_PDF(), 1/(2*(REAL)PI), (REAL)1.e-6));

  FOR_EACH(i, 0, NSAMPS) {
    CHK(RAN_CIRCLE_UNIFORM(rng, samps[i], &samps[i][3]) == samps[i]);
    CHK(R2_IS_NORMALIZED(samps[i]));
    CHK(EQ_EPS(samps[i][3], 1/(2*(REAL)PI), (REAL)1.e-6));
    FOR_EACH(j, 0, i) {
      CHK(!R2_EQ_EPS(samps[j], samps[i], (REAL)1.e-6));
    }
    printf("%g %g\n", SPLIT2(samps[i]));
  }

  ssp_rng_ref_put(rng);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);

  CHK(mem_allocated_size() == 0);
}

#undef REAL
#undef TEST
#undef RAN_CIRCLE_UNIFORM
#undef RAN_CIRCLE_UNIFORM_PDF
#undef EQ_EPS
#undef R2_EQ_EPS
#undef R2_IS_NORMALIZED
#undef TYPE_FLOAT

