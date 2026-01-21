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

#include <rsys/math.h>

int
main(int argc, char** argv)
{
  const size_t nsteps = 100;
  const double step = (PI/2.0) / (double)nsteps;
  size_t i;
  struct mem_allocator allocator;
  struct ssf_fresnel* fresnel;
  struct ssf_fresnel* dummy;
  double val;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssf_fresnel_create(&allocator, &fresnel_dummy, &dummy) == RES_OK);
  CHK(ssf_fresnel_create
    (&allocator, &ssf_fresnel_dielectric_dielectric, &fresnel) == RES_OK);

  CHK(ssf_fresnel_dielectric_dielectric_setup(NULL, -1.0, -1.0) == RES_BAD_ARG);
  CHK(ssf_fresnel_dielectric_dielectric_setup(fresnel, -1.0, -1.0) == RES_OK);
  CHK(ssf_fresnel_dielectric_dielectric_setup(dummy,-1.0, -1.0) == RES_BAD_ARG);
  CHK(ssf_fresnel_dielectric_dielectric_setup(fresnel,1.000277, 1.5) == RES_OK);

  val = ssf_fresnel_eval(fresnel, 1);
  CHK(eq_eps(val, 0.04, 1.e-4) == 1);

  FOR_EACH(i, 0, nsteps+1) {
    const double theta_i = MMIN((double)i*step, PI/2);
    const double cos_theta_i = cos(theta_i);
    val = ssf_fresnel_eval(fresnel, cos_theta_i);
    printf("%g %g\n", theta_i, val);
  }

  CHK(ssf_fresnel_ref_put(fresnel) == RES_OK);
  CHK(ssf_fresnel_ref_put(dummy) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
