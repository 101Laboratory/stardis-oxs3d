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

#define NG 10
#define NSAMPS 10000

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct ssf_phase* phase;
  struct ssf_phase* dummy;
  struct ssp_rng* rng;
  double wo[3];
  double wi[3];
  size_t i;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);

  CHK(ssf_phase_create(&allocator, &ssf_phase_hg, &phase) == RES_OK);
  CHK(ssf_phase_create(&allocator, &phase_dummy, &dummy) == RES_OK);

  CHK(ssf_phase_hg_setup(NULL, -2) == RES_BAD_ARG);
  CHK(ssf_phase_hg_setup(phase, -2) == RES_BAD_ARG);
  CHK(ssf_phase_hg_setup(NULL, -1) == RES_BAD_ARG);
  CHK(ssf_phase_hg_setup(phase, -1) == RES_OK);
  CHK(ssf_phase_hg_setup(phase, 0) == RES_OK);
  CHK(ssf_phase_hg_setup(phase, 1) == RES_OK);
  CHK(ssf_phase_hg_setup(dummy, 1) == RES_BAD_ARG);

  FOR_EACH(i, 0, NG) {
    const double g = ssp_rng_uniform_double(rng, -1, +1);
    double sum_cos = 0;
    double sum_cos_sqr = 0;
    double E, V, SE;
    size_t isamp;

    CHK(ssf_phase_hg_setup(phase, g) == RES_OK);

    ssp_ran_sphere_uniform(rng, wo, NULL);
    FOR_EACH(isamp, 0, NSAMPS) {
      double w[3];
      double weight;
      double pdf;
      double r;
      double ref;
      ssf_phase_sample(phase, rng, wo, wi, &pdf);
      CHK(d3_is_normalized(wi));
      CHK(eq_eps(ssf_phase_pdf(phase, wo, wi), pdf, 1.e-6));
      d3_minus(w, wo); /* Match the convention */
      weight = d3_dot(w, wi);
      sum_cos += weight;
      sum_cos_sqr += weight*weight;

      /* HG(theta) = 1/(4*PI) * (1 - g^2) / (1 + g^2 - 2*g*cos(theta))^3/2 */
      r = ssf_phase_eval(phase, wo, wi);
      ref = 1/(4*PI) * (1-g*g) / pow(1+g*g-2*g*d3_dot(w,wi), 1.5);
      CHK(eq_eps(r, ref, 1.e-6));
      CHK(eq_eps(r, pdf, 1.e-6));
    }
    /* On average cos(-wo,wi) should be g */
    E = sum_cos / NSAMPS;
    V = sum_cos_sqr / NSAMPS - E*E;
    SE = sqrt(V/NSAMPS);
    CHK(eq_eps(E, g, 3*SE));
  }

  ssp_ran_sphere_uniform(rng, wo, NULL);
  ssf_phase_sample(phase, rng, wo, wi, NULL);
  CHK(d3_is_normalized(wi));

  CHK(ssf_phase_ref_put(phase) == RES_OK);
  CHK(ssf_phase_ref_put(dummy) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

