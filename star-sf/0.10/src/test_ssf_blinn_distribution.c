/* Copyright (C) 2016-2018, 2021-2025 |Méso|Star> (contact@meso-star.com)
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

#define _POSIX_C_SOURCE 200112L /* nextafter support */

#include "ssf.h"
#include "test_ssf_utils.h"

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct ssp_rng* rng;
  struct ssf_microfacet_distribution* distrib;
  struct ssf_microfacet_distribution* dummy;
  const size_t NTESTS = 5;
  size_t itest;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng) == RES_OK);

  CHK(ssf_microfacet_distribution_create
    (&allocator, &ssf_blinn_distribution, &distrib) == RES_OK);
  CHK(ssf_microfacet_distribution_create
    (&allocator, &microfacet_dummy, &dummy) == RES_OK);

  CHK(ssf_blinn_distribution_setup(NULL, -1) == RES_BAD_ARG);
  CHK(ssf_blinn_distribution_setup(distrib, -1) == RES_BAD_ARG);
  CHK(ssf_blinn_distribution_setup(NULL, 8) == RES_BAD_ARG);
  CHK(ssf_blinn_distribution_setup(distrib, 8) == RES_OK);
  CHK(ssf_blinn_distribution_setup(distrib, 0) == RES_OK);
  CHK(ssf_blinn_distribution_setup(dummy, 0) == RES_BAD_ARG);

  CHK(ssf_blinn_distribution_setup
    (distrib, SSF_BLINN_DISTRIBUTION_MAX_EXPONENT) ==RES_OK);
  CHK(ssf_blinn_distribution_setup
    (distrib, nextafter(SSF_BLINN_DISTRIBUTION_MAX_EXPONENT, DBL_MAX)) ==
     RES_BAD_ARG);
  CHK(ssf_blinn_distribution_setup(distrib, nextafter(0,-1)) == RES_BAD_ARG);
  CHK(ssf_blinn_distribution_setup(distrib, 32.32) == RES_OK);

  FOR_EACH(itest, 0, NTESTS) {
    const double exponent = ssp_rng_uniform_double
      (rng, 0, SSF_BLINN_DISTRIBUTION_MAX_EXPONENT);
    CHK(ssf_blinn_distribution_setup(distrib, exponent) == RES_OK);
    check_microfacet_distribution(distrib, rng);
  }

  CHK(ssf_microfacet_distribution_ref_put(distrib) == RES_OK);
  CHK(ssf_microfacet_distribution_ref_put(dummy) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
