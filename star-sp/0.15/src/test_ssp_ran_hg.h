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

#ifndef TEST_SSP_RAN_HG_H
#define TEST_SSP_RAN_HG_H

#include "ssp.h"
#include "test_ssp_utils.h"

#define NG 100
#define NSAMPS 10000

#endif /* TEST_SSP_RAN_HG_H */

#if TYPE_FLOAT==0
#define REAL double
#define TEST test_double
#define RNG_UNIFORM_R ssp_rng_uniform_double
#define RAN_SPHERE_HG ssp_ran_sphere_hg
#define RAN_SPHERE_HG_LOCAL ssp_ran_sphere_hg_local
#define RAN_HEMISPHERE_UNIFORM_LOCAL ssp_ran_hemisphere_uniform_local
#define EQ_EPS_R eq_eps
#define R3_DOT d3_dot

#elif TYPE_FLOAT==1
#define REAL float
#define TEST test_float
#define RNG_UNIFORM_R ssp_rng_uniform_float
#define RAN_SPHERE_HG ssp_ran_sphere_hg_float
#define RAN_SPHERE_HG_LOCAL ssp_ran_sphere_hg_float_local
#define RAN_HEMISPHERE_UNIFORM_LOCAL ssp_ran_hemisphere_uniform_float_local
#define EQ_EPS_R eq_epsf
#define R3_DOT f3_dot
#else
#error "TYPE_FLOAT must be defined either 0 or 1"
#endif

static void
TEST()
{
  struct ssp_rng* rng;
  struct mem_allocator allocator;
  int i, j;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng) == RES_OK);

  FOR_EACH(i, 0, NG) {
    /* for any value of g... */
    REAL g = RNG_UNIFORM_R(rng, -1, +1);
    REAL sum_cos = 0;
    REAL sum_cos_local = 0;
    REAL dir[3], pdf, up[4] = {0, 0, 1};
    FOR_EACH(j, 0, NSAMPS) {
      /* HG relative to the Z axis */
      RAN_SPHERE_HG_LOCAL(rng, g, dir, &pdf);
      sum_cos_local += R3_DOT(up, dir);
    }
    FOR_EACH(j, 0, NSAMPS) {
      /* HG relative to a up uniformaly sampled */
      RAN_HEMISPHERE_UNIFORM_LOCAL(rng, up, &pdf);
      RAN_SPHERE_HG(rng, up, g, dir, &pdf);
      sum_cos += R3_DOT(up, dir);
    }
    /* ...on average cos(up, dir) should be g */
    CHK(EQ_EPS_R(sum_cos_local / NSAMPS, g, (REAL)2.5e-2) == 1);
    CHK(EQ_EPS_R(sum_cos / NSAMPS, g, (REAL)2.5e-2) == 1);
  }

  ssp_rng_ref_put(rng);
  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);

  CHK(mem_allocated_size() == 0);
}

#undef REAL
#undef TEST
#undef RNG_UNIFORM_R
#undef RAN_SPHERE_HG
#undef RAN_SPHERE_HG_LOCAL
#undef RAN_HEMISPHERE_UNIFORM_LOCAL
#undef EQ_EPS_R
#undef R3_DOT
#undef TYPE_FLOAT
