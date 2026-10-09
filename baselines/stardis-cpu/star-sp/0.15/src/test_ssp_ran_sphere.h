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

#ifndef TEST_SSP_RAN_SPHERE_H
#define TEST_SSP_RAN_SPHERE_H

#include "ssp.h"
#include "test_ssp_utils.h"

#define NSAMPS 128

#endif /* TEST_SSP_RAN_SPHERE_H */

#if TYPE_FLOAT==0
#define REAL double
#define TEST test_double
#define RAN_SPHERE_UNIFORM ssp_ran_sphere_uniform
#define RAN_SPHERE_UNIFORM_PDF ssp_ran_sphere_uniform_pdf
#define EQ_EPS eq_eps
#define R3_EQ_EPS d3_eq_eps
#define R3_IS_NORMALIZED d3_is_normalized

#elif TYPE_FLOAT==1
#define REAL float
#define TEST test_float
#define RAN_SPHERE_UNIFORM ssp_ran_sphere_uniform_float
#define RAN_SPHERE_UNIFORM_PDF ssp_ran_sphere_uniform_float_pdf
#define EQ_EPS eq_epsf
#define R3_EQ_EPS f3_eq_eps
#define R3_IS_NORMALIZED f3_is_normalized

#else
#error "TYPE_FLOAT must be defined either 0 or 1"
#endif

static void
TEST()
{
  struct ssp_rng* rng;
  struct mem_allocator allocator;
  REAL samps[NSAMPS][4];
  REAL* f = NULL;
  int i = 0, j = 0;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);

  FOR_EACH(i, 0, NSAMPS) {
    f = RAN_SPHERE_UNIFORM(rng, samps[i], &samps[i][3]);
    CHK(f == samps[i]);
    CHK(R3_IS_NORMALIZED(f) == 1);
    CHK(EQ_EPS(samps[i][3], 1/(4*(REAL)PI), (REAL)1.e-6) == 1);
    CHK(EQ_EPS(samps[i][3], RAN_SPHERE_UNIFORM_PDF(), (REAL)1.e-6) == 1);
    FOR_EACH(j, 0, i) {
      CHK(R3_EQ_EPS(samps[j], samps[i], (REAL)1.e-6) == 0);
    }
  }

  ssp_rng_ref_put(rng);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);

  CHK(mem_allocated_size() == 0);
}

#undef REAL
#undef TEST
#undef RAN_SPHERE_UNIFORM
#undef RAN_SPHERE_UNIFORM_PDF
#undef EQ_EPS
#undef R3_EQ_EPS
#undef R3_IS_NORMALIZED
#undef TYPE_FLOAT
