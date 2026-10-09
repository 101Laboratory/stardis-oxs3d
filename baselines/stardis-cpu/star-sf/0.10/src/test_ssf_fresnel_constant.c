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
  struct ssf_fresnel* fresnel;
  struct ssf_fresnel* dummy;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssf_fresnel_create(&allocator, &ssf_fresnel_constant, &fresnel) == RES_OK);
  CHK(ssf_fresnel_create(&allocator, &fresnel_dummy, &dummy) == RES_OK);

  CHK(ssf_fresnel_constant_setup(NULL, -1) == RES_BAD_ARG);
  CHK(ssf_fresnel_constant_setup(fresnel, -1) == RES_BAD_ARG);
  CHK(ssf_fresnel_constant_setup(NULL, 0.5) == RES_BAD_ARG);
  CHK(ssf_fresnel_constant_setup(fresnel, 0) == RES_OK);
  CHK(ssf_fresnel_constant_setup(fresnel, 1) == RES_OK);
  CHK(ssf_fresnel_constant_setup(fresnel, 0.5) == RES_OK);
  CHK(ssf_fresnel_constant_setup(fresnel, nextafter(0, -1)) == RES_BAD_ARG);
  CHK(ssf_fresnel_constant_setup(fresnel, nextafter(1, 2)) == RES_BAD_ARG);
  CHK(ssf_fresnel_constant_setup(dummy, 0.5) == RES_BAD_ARG);

  CHK(ssf_fresnel_eval(fresnel, 0) == 0.5);
  CHK(ssf_fresnel_eval(fresnel, 1) == 0.5);
  CHK(ssf_fresnel_eval(fresnel, 0.1234) == 0.5);
  CHK(ssf_fresnel_constant_setup(fresnel, 0.123) == RES_OK);
  CHK(ssf_fresnel_eval(fresnel, 0) == 0.123);
  CHK(ssf_fresnel_eval(fresnel, 1) == 0.123);
  CHK(ssf_fresnel_eval(fresnel, 0.25) == 0.123);

  CHK(ssf_fresnel_ref_put(fresnel) == RES_OK);
  CHK(ssf_fresnel_ref_put(dummy) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
