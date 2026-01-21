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

#ifndef TEST_SSP_RAN_SPHERE_CAP_H
#define TEST_SSP_RAN_SPHERE_CAP_H

#include "ssp.h"
#include "test_ssp_utils.h"

#define NSAMPS 1024

#endif /* TEST_SSP_RAN_SPHERE_H */

#if TYPE_FLOAT==0
  #define REAL double
  #define TEST test_double
  #define RAN_SPHERE_CAP_UNIFORM_LOCAL ssp_ran_sphere_cap_uniform_local
  #define RAN_SPHERE_CAP_UNIFORM_PDF ssp_ran_sphere_cap_uniform_pdf
  #define RAN_SPHERE_CAP_UNIFORM ssp_ran_sphere_cap_uniform
  #define RAN_SPHERE_UNIFORM ssp_ran_sphere_uniform
  #define EQ_EPS eq_eps
  #define R3_EQ_EPS d3_eq_eps
  #define R3_IS_NORMALIZED d3_is_normalized
  #define R3_NORMALIZE d3_normalize
  #define R3_DOT d3_dot
#elif TYPE_FLOAT==1
  #define REAL float
  #define TEST test_float
  #define RAN_SPHERE_CAP_UNIFORM_LOCAL ssp_ran_sphere_cap_uniform_float_local
  #define RAN_SPHERE_CAP_UNIFORM_PDF ssp_ran_sphere_cap_uniform_float_pdf
  #define RAN_SPHERE_CAP_UNIFORM ssp_ran_sphere_cap_uniform_float
  #define RAN_SPHERE_UNIFORM ssp_ran_sphere_uniform_float
  #define EQ_EPS eq_epsf
  #define R3_EQ_EPS f3_eq_eps
  #define R3_IS_NORMALIZED f3_is_normalized
  #define R3_NORMALIZE f3_normalize
  #define R3_DOT f3_dot
#else
  #error "TYPE_FLOAT must be defined either 0 or 1"
#endif

static void
TEST(void)
{
  struct ssp_rng* rng;
  struct mem_allocator allocator;
  REAL pdf;
  REAL samps[NSAMPS][3];
  REAL up[3];
  int i, j;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);

  CHK(RAN_SPHERE_CAP_UNIFORM_LOCAL(rng, 1, samps[0], &pdf) == samps[0]);
  CHK(samps[0][0] == 0);
  CHK(samps[0][1] == 0);
  CHK(samps[0][2] == 1);
  CHK(IS_INF(pdf));

  CHK(RAN_SPHERE_CAP_UNIFORM_LOCAL(rng,-1, samps[0], &pdf) == samps[0]);
  CHK(EQ_EPS(pdf, 1/(4*(REAL)PI), (REAL)1.e-6));

  /* Check NULL pdf */
  CHK(RAN_SPHERE_CAP_UNIFORM_LOCAL(rng, (REAL)0.2, samps[0], NULL) == samps[0]);

  FOR_EACH(i, 0, NSAMPS) {
    const REAL height = (REAL)-0.7;
    CHK(RAN_SPHERE_CAP_UNIFORM_LOCAL(rng, height, samps[i], &pdf) == samps[i]);
    CHK(R3_IS_NORMALIZED(samps[i]));
    CHK(EQ_EPS(pdf, 1/(2*(REAL)PI*(1-height)), (REAL)1.e-6));
    CHK(EQ_EPS(pdf, RAN_SPHERE_CAP_UNIFORM_PDF(height), (REAL)1.e-6));
    CHK(samps[i][2] >= height);
    FOR_EACH(j, 0, i) {
      CHK(!R3_EQ_EPS(samps[j], samps[i], (REAL)1.e-6));
    }
  }

  /* Sample an up vector */
  RAN_SPHERE_UNIFORM(rng, up, NULL);

  CHK(RAN_SPHERE_CAP_UNIFORM(rng, up, 1, samps[0], &pdf) == samps[0]);
  CHK(R3_EQ_EPS(samps[0], up, (REAL)1.e-6));
  CHK(IS_INF(pdf));

  CHK(RAN_SPHERE_CAP_UNIFORM(rng, up, -1, samps[0], &pdf) == samps[0]);
  CHK(EQ_EPS(pdf, 1/(4*(REAL)PI), (REAL)1.e-6));

  /* Check NULL pdf */
  CHK(RAN_SPHERE_CAP_UNIFORM(rng, up, (REAL)0.2, samps[0], NULL) == samps[0]);

  FOR_EACH(i, 0, NSAMPS) {
    const REAL height = (REAL)0.3;
    CHK(RAN_SPHERE_CAP_UNIFORM(rng, up, height, samps[i], &pdf) == samps[i]);
    CHK(R3_IS_NORMALIZED(samps[i]));
    CHK(EQ_EPS(pdf, 1/(2*(REAL)PI*(1-height)), (REAL)1.e-6));
    CHK(EQ_EPS(pdf, RAN_SPHERE_CAP_UNIFORM_PDF(height), (REAL)1.e-6));
    CHK(R3_DOT(up, samps[i]) >= height);
    FOR_EACH(j, 0, i) {
      CHK(!R3_EQ_EPS(samps[j], samps[i], (REAL)1.e-6));
    }
  }

  ssp_rng_ref_put(rng);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);

  CHK(mem_allocated_size() == 0);
}

#undef REAL
#undef TEST
#undef RAN_SPHERE_CAP_UNIFORM_LOCAL
#undef RAN_SPHERE_CAP_UNIFORM_PDF
#undef RAN_SPHERE_CAP_UNIFORM
#undef RAN_SPHERE_UNIFORM
#undef EQ_EPS
#undef R3_EQ_EPS
#undef R3_IS_NORMALIZED
#undef R3_NORMALIZE
#undef R3_DOT
#undef TYPE_FLOAT
