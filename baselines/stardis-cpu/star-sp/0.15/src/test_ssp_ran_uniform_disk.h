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

#ifndef TEST_SSP_RAN_UNIFORM_DISK_H
#define TEST_SSP_RAN_UNIFORM_DISK_H

#include "ssp.h"
#include "test_ssp_utils.h"
#include <rsys_math.h>

#include <string.h>

#define NBS 1000000

#endif /* TEST_SSP_RAN_UNIFORM_DISK_H */

#if TYPE_FLOAT==0
  #define REAL double
  #define TEST test_double
  #define RNG_UNIFORM_R ssp_rng_uniform_double
  #define RNG_UNIFORM_DISK_LOCAL ssp_ran_uniform_disk_local
  #define RNG_UNIFORM_DISK ssp_ran_uniform_disk
  #define R33_BASIS d33_basis
  #define R33_MULR3 d33_muld3
  #define R3_EQ_EPS d3_eq_eps
  #define R3_NORMALIZE d3_normalize
  #define SQRT sqrt

#elif TYPE_FLOAT==1
  #define REAL float
  #define TEST test_float
  #define RNG_UNIFORM_R ssp_rng_uniform_float
  #define RNG_UNIFORM_DISK_LOCAL ssp_ran_uniform_disk_float_local
  #define RNG_UNIFORM_DISK ssp_ran_uniform_disk_float
  #define R33_BASIS f33_basis
  #define R33_MULR3 f33_mulf3
  #define R3_EQ_EPS f3_eq_eps
  #define R3_NORMALIZE f3_normalize
  #define SQRT (float)sqrt
#else
  #error "TYPE_FLOAT must be defined either 0 or 1"
#endif

static void
TEST()
{
  struct ssp_rng* rng0, *rng1;
  struct mem_allocator allocator;
  int i;
  int r, c, nb = 0;
  REAL pt[3], pt2[3], up[3];
  REAL frame[9];
  REAL x_sum = 0, x2_sum = 0;
  REAL mean, std;
  REAL exp_mean = NBS * (20 * 20 / ((REAL)PI * 100 * 100)), exp_std = 0;
  unsigned counts[10][10];
  uint64_t seed = 1234;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng0) == RES_OK);
  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng1) == RES_OK);

  up[0] = RNG_UNIFORM_R(rng1, -1, 1);
  up[1] = RNG_UNIFORM_R(rng1, -1, 1);
  up[2] = RNG_UNIFORM_R(rng1, -1, 1);
  R3_NORMALIZE(up, up);
  R33_BASIS(frame, up);

  ssp_rng_set(rng0, seed);
  ssp_rng_set(rng1, seed);

  memset(counts, 0, sizeof(counts));
  FOR_EACH(i, 0, NBS) {
    REAL tmp[3];
    RNG_UNIFORM_DISK_LOCAL(rng0, 100, pt, NULL);
    RNG_UNIFORM_DISK(rng1, 100, up, pt2, NULL);
    R33_MULR3(tmp, frame, pt);
    CHK(R3_EQ_EPS(tmp, pt2, (REAL)1.e-6) == 1);
    ASSERT(pt[2] == 0);
    /* locate pt in a 10x10 grid */
    r = (int)((100 + pt[0]) / 20);
    c = (int)((100 + pt[1]) / 20);
    ++counts[r][c];
  }

  FOR_EACH(r, 0, 10) {
    int x = (r >= 5 ? r - 4 : r - 5) * 20;
    FOR_EACH(c, 0, 10) {
      int y = (c >= 5 ? c - 4 : c - 5) * 20;
      int r2 = x * x + y * y;
      if(r2 > 100 * 100)
        /* this square is not (fully) in the disk */
        continue;
      ++nb;
      x_sum += (REAL)counts[r][c];
      x2_sum += (REAL)(counts[r][c] * counts[r][c]);
    }
  }
  mean = x_sum / (REAL)nb;
  std = x2_sum / (REAL)nb - mean*mean;
  std = std > 0 ? SQRT(std) : 0;
  printf("%g %g\n", mean, std);
  CHK(fabs(mean - exp_mean) < 10);
  CHK(fabs(std - exp_std) < 200);

  CHK(ssp_rng_ref_put(rng0) == RES_OK);
  CHK(ssp_rng_ref_put(rng1) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
}

#undef REAL
#undef TEST
#undef RNG_UNIFORM_R
#undef RNG_UNIFORM_DISK_LOCAL
#undef RNG_UNIFORM_DISK
#undef R33_BASIS
#undef R33_MULR3
#undef R3_EQ_EPS
#undef R3_NORMALIZE
#undef SQRT
#undef TYPE_FLOAT
