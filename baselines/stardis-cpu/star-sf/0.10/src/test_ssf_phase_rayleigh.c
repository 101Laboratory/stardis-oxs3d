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

#define NSAMPS 10000

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct ssf_phase* phase;
  struct ssp_rng* rng;
  double wo[3];
  double wi[3];
  double sum_cos = 0;
  double sum_cos_sqr = 0;
  double sum_cos_sqr2 = 0;
  double E, V, SE;
  size_t i;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);

  CHK(ssf_phase_create(&allocator, &ssf_phase_rayleigh, &phase) == RES_OK);

  ssp_ran_sphere_uniform(rng, wo, NULL);
  FOR_EACH(i, 0, NSAMPS) {
    double w[3];
    double weight;
    double pdf;
    double ref;
    double r;
    ssf_phase_sample(phase, rng, wo, wi, &pdf);
    CHK(d3_is_normalized(wi));
    CHK(eq_eps(ssf_phase_pdf(phase, wo, wi), pdf, 1.e-6f));
    d3_minus(w, wo);
    weight = d3_dot(wo, wi);
    sum_cos += weight;
    sum_cos_sqr += weight*weight;
    sum_cos_sqr2 += weight*weight*weight*weight;

    r = ssf_phase_eval(phase, wo, wi);
    ref = 3.0/(16.0*PI)*(1.0+weight*weight);
    CHK(eq_eps(r, ref, 1.e-6));
    CHK(eq_eps(r, pdf, 1.e-6));
  }

  /* On average cos(-wo,wi) should be 0 */
  E = sum_cos / NSAMPS;
  V = sum_cos_sqr / NSAMPS - E*E;
  SE = sqrt(V/NSAMPS);
  CHK(eq_eps(E, 0, SE*3));

  /* On average cos(-wo,wi)^2 should be 2/5 */
  E = sum_cos_sqr / NSAMPS;
  V = sum_cos_sqr2 / NSAMPS - E*E;
  SE = sqrt(V/NSAMPS);
  CHK(eq_eps(E, 2.0/5.0, SE*3));

  ssf_phase_sample(phase, rng, wo, wi, NULL);
  CHK(d3_is_normalized(wi));

  CHK(ssf_phase_ref_put(phase) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
