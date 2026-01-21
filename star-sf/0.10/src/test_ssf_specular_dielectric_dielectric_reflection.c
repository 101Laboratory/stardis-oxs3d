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
 * ALOng with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "ssf.h"
#include "ssf_optics.h"
#include "test_ssf_utils.h"

#include <rsys/double3.h>

int
main(int argc, char** argv)
{
  const size_t NSTEPS = 10000;
  struct mem_allocator allocator;
  struct ssp_rng* rng;
  struct ssf_bsdf* bsdf;
  struct ssf_bsdf* dummy;
  const double eta_i = 1.00027;
  const double eta_t = 1.5;
  const double eta = eta_i/eta_t;
  double N[3];
  double wo[3];
  double wi[3];
  double tmp[3];
  double R;
  double pdf;
  double sum_t, sum_t2, t;
  double sum_r, sum_r2, r;
  size_t i;
  int type;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_MT19937_64, &rng) == RES_OK);
  CHK(ssf_bsdf_create(&allocator,
    &ssf_specular_dielectric_dielectric_interface, &bsdf) ==  RES_OK);
  CHK(ssf_bsdf_create(&allocator, &bsdf_dummy, &dummy) == RES_OK);

  #define SETUP ssf_specular_dielectric_dielectric_interface_setup
  CHK(SETUP(NULL, 0, 0) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 0, 0) == RES_BAD_ARG);
  CHK(SETUP(NULL, 1, 0) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1, 0) == RES_BAD_ARG);
  CHK(SETUP(NULL, 0, 1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 0, 1) == RES_BAD_ARG);
  CHK(SETUP(NULL, 1, 1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1, 1) == RES_OK);
  CHK(SETUP(dummy, 1, 1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1.00027, 1.5) == RES_OK);
  #undef SETUP

  d3(N, 0, 1, 0);

  /* Test transmission */
  d3(wo, 0, 1, 0);
  R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
  CHK(R != 0);
  CHK(IS_INF(pdf));
  CHK(d3_eq_eps(wi, d3(tmp,0,-1,0), 1.e-6));
  CHK(type == (SSF_SPECULAR | SSF_TRANSMISSION));

  /* Test reflection */
  d3(wo, 1, 0, 0);
  R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
  CHK(R != 0);
  CHK(IS_INF(pdf));
  CHK(d3_eq_eps(wi, d3(tmp,-1,0,0), 1.e-6));
  CHK(type == (SSF_SPECULAR | SSF_REFLECTION));

  d3_normalize(wo, d3(wo, 1, 0.0000001, 0));
  R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
  CHK(R == 1);
  CHK(IS_INF(pdf));
  CHK(d3_eq_eps(wi, d3(tmp, -wo[0], wo[1], 0), 1.e-6) == 1);
  CHK(type == (SSF_SPECULAR | SSF_REFLECTION));

  pdf = 0, type = 0;
  CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, NULL, &pdf) == R);
  CHK(IS_INF(pdf));
  CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, NULL) == R);
  CHK(type == (SSF_SPECULAR | SSF_REFLECTION));
  CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, NULL, NULL) == R);

  /* Check energy conservation */
  sum_t = sum_t2 = 0;
  sum_r = sum_r2 = 0;
  FOR_EACH(i, 0, NSTEPS) {
    ssp_ran_hemisphere_cos(rng, wo, N, NULL);
    R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, NULL);
    if(type & SSF_TRANSMISSION) {
      sum_t += R;
      sum_t2 += R*R;
    } else {
      sum_r += R;
      sum_r2 += R*R;
    }
  }

  t = sum_t / (double)NSTEPS;
  r = sum_r / (double)NSTEPS;
  CHK(t > 0);
  CHK(r > 0);
  CHK(eq_eps(t + r, 1, 1.e-6));

  FOR_EACH(i, 0, NSTEPS) {
    d3_normalize(wo, d3(wo,1,1,0));
    R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
    CHK(R != 0);
    CHK(type & SSF_SPECULAR);
    CHK(IS_INF(pdf));
    if(d3_eq_eps(wi, refract(tmp, wo, N, eta), 1.e-6)) {
      CHK(type & SSF_TRANSMISSION);
    } else {
      CHK(d3_eq_eps(wi, reflect(tmp, wo, N), 1.e-6));
      CHK(type & SSF_REFLECTION);
    }
    CHK(ssf_bsdf_pdf(bsdf, wo, N, wi) == 0);
    CHK(ssf_bsdf_eval(bsdf, wo, N, wi) == 0);
  }

  CHK(ssp_rng_ref_put(rng) == RES_OK);
  CHK(ssf_bsdf_ref_put(bsdf) == RES_OK);
  CHK(ssf_bsdf_ref_put(dummy) == RES_OK);

  return 0;
}
