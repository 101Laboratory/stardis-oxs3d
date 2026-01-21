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

#include "ssf.h"
#include "test_ssf_utils.h"

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct ssf_fresnel* fresnel;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssf_fresnel_create(&allocator, &ssf_fresnel_no_op, &fresnel) == RES_OK);

  CHK(ssf_fresnel_eval(fresnel, 0) == 1.0);
  CHK(ssf_fresnel_eval(fresnel, 1) == 1.0);
  CHK(ssf_fresnel_eval(fresnel, 0.48) == 1.0);

  CHK(ssf_fresnel_ref_put(fresnel) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
