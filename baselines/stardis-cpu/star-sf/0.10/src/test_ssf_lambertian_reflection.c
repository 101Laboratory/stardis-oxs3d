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

#include <rsys/double3.h>
#include<rsys/double33.h>

int
main(int argc, char** argv)
{
  const size_t NSTEPS = 100000;
  struct mem_allocator allocator;
  struct ssp_rng* rng;
  struct ssf_bsdf* brdf;
  struct ssf_bsdf* dummy;
  double E, SE, V;
  double sum;
  double sum_sqr;
  double basis[9];
  double wo[3], wi[3];
  double N[3];
  double pdf;
  double R;
  size_t i;
  int type;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng) == RES_OK);

  CHK(ssf_bsdf_create(&allocator, &ssf_lambertian_reflection, &brdf) == RES_OK);
  CHK(ssf_bsdf_create(&allocator, &bsdf_dummy, &dummy) == RES_OK);

  CHK(ssf_lambertian_reflection_setup(NULL, -1.0) == RES_BAD_ARG);
  CHK(ssf_lambertian_reflection_setup(brdf, -1.0) == RES_BAD_ARG);
  CHK(ssf_lambertian_reflection_setup(NULL, 1.0) == RES_BAD_ARG);
  CHK(ssf_lambertian_reflection_setup(brdf, 1.0) == RES_OK);
  CHK(ssf_lambertian_reflection_setup(brdf, 0.0) == RES_OK);
  CHK(ssf_lambertian_reflection_setup(brdf, 1.1) == RES_BAD_ARG);
  CHK(ssf_lambertian_reflection_setup(dummy, 0.0) == RES_BAD_ARG);

  d3(N, 0.0, 0.0, 1.0);
  d3_normalize(wo, d3(wo, 1.0, 0.0, 1.0));
  R = ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf);
  CHK(eq_eps(wi[2]/PI, pdf, 1.e-6) == 1);
  CHK(R == 0);
  CHK(type == (SSF_DIFFUSE|SSF_REFLECTION));

  CHK(ssf_lambertian_reflection_setup(brdf, 0.7) == RES_OK);
  R = ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf);
  CHK(eq_eps(R, 0.7, 1.e-6) == 1);
  CHK(eq_eps(pdf, d3_dot(wi, N)/PI, 1.e-6) == 1);
  CHK(type == (SSF_DIFFUSE|SSF_REFLECTION));

  pdf = 0, type = 0;
  CHK(ssf_bsdf_sample(brdf, rng, wo, N, wi, NULL, &pdf) == R);
  CHK(eq_eps(pdf, d3_dot(wi, N)/PI, 1.e-6) == 1);
  CHK(ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, NULL) == R);
  CHK(type == (SSF_DIFFUSE|SSF_REFLECTION));
  CHK(ssf_bsdf_sample(brdf, rng, wo, N, wi, NULL, NULL) == R);

  d3_normalize(wo, d3(wo, 1.0, 0.0, 1.0));
  N[0] = ssp_rng_uniform_double(rng, -1, 1);
  N[1] = ssp_rng_uniform_double(rng, -1, 1);
  N[2] = ssp_rng_uniform_double(rng, -1, 1);
  d3_normalize(N, N);
  d33_basis(basis, N);
  d33_muld3(wo, basis, wo);

  sum = sum_sqr = 0;
  FOR_EACH(i, 0, NSTEPS) {
    double cos_wi_N;
    R = ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf) / PI;
    cos_wi_N = d3_dot(wi, N);
    CHK(eq_eps(R, 0.7/PI, 1.e-6) == 1);
    CHK(eq_eps(cos_wi_N/PI, pdf, 1.e-6) == 1);
    CHK(type == (SSF_DIFFUSE|SSF_REFLECTION));
    sum += cos_wi_N;
    sum_sqr += cos_wi_N * cos_wi_N;
  }
  E = sum/(double)NSTEPS;
  V = sum_sqr/(double)NSTEPS - E*E;
  SE = sqrt(V/(double)NSTEPS);
  CHK(eq_eps(E, 2.0/3.0, SE) == 1);

  sum = sum_sqr = 0;
  FOR_EACH(i, 0, NSTEPS) {
    double cos_wi_N;
    double w;

    R = ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf)/PI;
    cos_wi_N = d3_dot(wi, N);
    CHK(eq_eps(R, 0.7/PI, 1.e-6) == 1);
    CHK(eq_eps(cos_wi_N/PI, pdf, 1.e-6) == 1);
    CHK(type == (SSF_DIFFUSE|SSF_REFLECTION));
    w = cos_wi_N*cos_wi_N;
    sum += w;
    sum_sqr += w*w;
  }

  E = sum/(double)NSTEPS;
  V = sum_sqr/(double)NSTEPS - E*E;
  SE = sqrt(V/(double)NSTEPS);
  CHK(eq_eps(E, 2.0/4.0, 2.0*SE) == 1);
  CHK(eq_eps(SE, 1.0/sqrt((double)NSTEPS) * sqrt(1.0/3.0 - 1.0/4.0), 1.e-6) == 1);

  FOR_EACH(i, 0, NSTEPS) {
    double val;
    R = ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf);
    CHK(eq_eps(R, 0.7, 1.e-6) == 1);
    val = ssf_bsdf_eval(brdf, wo, N, wi) * d3_dot(wi, N);
    CHK(eq_eps(val, R * pdf, 1.e-6) == 1);
    CHK(type == (SSF_DIFFUSE|SSF_REFLECTION));
  }

  CHK(ssf_bsdf_ref_put(brdf) == RES_OK);
  CHK(ssf_bsdf_ref_put(dummy) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

