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

int
main(int argc, char** argv)
{
  const size_t NSTEPS = 1000000;
  struct mem_allocator allocator;
  struct ssp_rng* rng;
  struct ssf_bsdf* bsdf;
  struct ssf_bsdf* dummy;
  double wo[3], wi[3];
  double N[3];
  double tmp[3];
  double reflect[3];
  double refract[3];
  double pdf;
  double R; /* Directional reflectance */
  double SR, ST;
  double SR2 = 0, ST2 = 0;
  size_t i;
  int type;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng) == RES_OK);
  CHK(ssf_bsdf_create
    (&allocator, &ssf_thin_specular_dielectric, &bsdf) == RES_OK);
  CHK(ssf_bsdf_create(&allocator, &bsdf_dummy, &dummy) == RES_OK);

  #define SETUP ssf_thin_specular_dielectric_setup
  CHK(SETUP(NULL, -1, 0, 0, -1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, -1, 0, 0, -1) == RES_BAD_ARG);
  CHK(SETUP(NULL, 1, 0, 0, -1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1, 0, 0, -1) == RES_BAD_ARG);
  CHK(SETUP(NULL, -1, 1.00027, 0, -1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, -1, 1.00027, 0, -1) == RES_BAD_ARG);
  CHK(SETUP(NULL, 1, 1.00027,  0, -1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1, 1.00027, 0, -1) == RES_BAD_ARG);
  CHK(SETUP(NULL, -1, 0, 1.5, -1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, -1, 0, 1.5, -1) == RES_BAD_ARG);
  CHK(SETUP(NULL, 1, 0, 1.5, -1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1, 0, 1.5, -1) == RES_BAD_ARG);
  CHK(SETUP(NULL, -1, 1.00027, 1.5, -1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, -1, 1.00027, 1.5, -1) == RES_BAD_ARG);
  CHK(SETUP(NULL, 1, 1.00027,  1.5, -1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1, 1.00027, 1.5, -1) == RES_BAD_ARG);
  CHK(SETUP(NULL, -1, 0, 0, 0.1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, -1, 0, 0, 0.1) == RES_BAD_ARG);
  CHK(SETUP(NULL, 1, 0, 0, 0.1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1, 0, 0, 0.1) == RES_BAD_ARG);
  CHK(SETUP(NULL, -1, 1.00027, 0, 0.1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, -1, 1.00027, 0, 0.1) == RES_BAD_ARG);
  CHK(SETUP(NULL, 1, 1.00027,  0, 0.1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1, 1.00027, 0, 0.1) == RES_BAD_ARG);
  CHK(SETUP(NULL, -1, 0, 1.5, 0.1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, -1, 0, 1.5, 0.1) == RES_BAD_ARG);
  CHK(SETUP(NULL, 1, 0, 1.5, 0.1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1, 0, 1.5, 0.1) == RES_BAD_ARG);
  CHK(SETUP(NULL, -1, 1.00027, 1.5, 0.1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, -1, 1.00027, 1.5, 0.1) == RES_BAD_ARG);
  CHK(SETUP(NULL, 1, 1.00027,  1.5, 0.1) == RES_BAD_ARG);
  CHK(SETUP(bsdf, 1, 1.00027, 1.5, 0.1) == RES_OK);
  CHK(SETUP(dummy, 1, 1.00027, 1.5, 0.1) == RES_BAD_ARG);
  #undef SETUP

  d3(N, 0.0, 1.0, 0.0);
  d3_normalize(wo, d3(wo, 1.0, 1.0, 0.0));
  R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
  CHK(R != 0);
  CHK(type & SSF_SPECULAR);
  CHK(IS_INF(pdf) == 1);
  CHK(d3_eq_eps(wi, d3(tmp, -wo[0], wo[1], 0), 1.e-6)
   || d3_eq_eps(wi, d3(tmp, -wo[0],-wo[1], 0), 1.e-6));

  CHK(ssf_bsdf_eval(bsdf, wo, N, wi) == 0.0);
  CHK(ssf_bsdf_pdf(bsdf, wo, N, wi) == 0.0);

  d3(wo, 0.0, 1.0, 0.0);
  R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
  CHK(R != 0);
  CHK(IS_INF(pdf) == 1);
  CHK(d3_eq_eps(wi, d3(tmp, -wo[0], -wo[1], 0), 1.e-6) == 1);
  CHK(type == (SSF_SPECULAR | SSF_TRANSMISSION));

  d3(wo, 1.0, 0.0, 0.0);
  R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
  CHK(R != 0);
  CHK(IS_INF(pdf) == 1);
  CHK(d3_eq_eps(wi, d3(tmp, -wo[0], wo[1], 0), 1.e-6) == 1);
  CHK(type == (SSF_SPECULAR | SSF_REFLECTION));

  /* Check total internal reflection, i.e. no transmission */
  d3_normalize(wo, d3(wo, 1, 0.0000001, 0));
  CHK(ssf_thin_specular_dielectric_setup(bsdf, 0, 1.4, 1.0, 1) == RES_OK);
  R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
  CHK(R == 1);
  CHK(IS_INF(pdf) == 1);
  CHK(d3_eq_eps(wi, d3(tmp, -wo[0], wo[1], 0), 1.e-6) == 1);
  CHK(type == (SSF_SPECULAR | SSF_REFLECTION));

  /* Check optional arguments */
  pdf = 0, type = 0;
  CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, NULL, &pdf) == R);
  CHK(IS_INF(pdf));
  CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, NULL) == R);
  CHK(type == (SSF_SPECULAR | SSF_REFLECTION));
  CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, NULL, NULL) == R);

  FOR_EACH(i, 0, NSTEPS) {
    R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
    CHK(type & SSF_REFLECTION);
    CHK(R == 1);
  }

  /* Check T VS R proportion and E conservation */
  SR = ST = 0;
  SR2 = ST2 = 0;
  d3(wo, 0.0, 1.0, 0.0);
  FOR_EACH(i, 0, NSTEPS) {
    R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
    if (type & SSF_TRANSMISSION) {
      ST += R; ST2 += R*R;
    } else {
      SR += R; SR2 += R*R;
    }
  }

  #define MEAN(x, n) ((x) / (double)(n))
  #define VAR(x, x2, n) (MEAN((x2), (n)) - MEAN((x), (n))*MEAN((x), (n)))
  #define STD(x, x2, n) \
    (VAR((x), (x2), (n)) > 0 ? sqrt(VAR((x), (x2), (n)) / (double)(n)) : 0)
  /* Check conservation of energy */
  CHK(MEAN(SR+ST, NSTEPS) == 1);
  /* Check T VS R proportion */
  CHK(eq_eps(MEAN(SR, NSTEPS), 0.0540540540, 3 * STD(SR,SR2,NSTEPS)) == 1);
  #undef MEAN
  #undef VAR
  #undef STD

  wo[0] = ssp_rng_uniform_double(rng, -1, 1);
  wo[1] = ssp_rng_uniform_double(rng, -1, 1);
  wo[2] = ssp_rng_uniform_double(rng, -1, 1);
  if(d3_dot(wo, N) < 0) d3_minus(N, N);
  d3_normalize(wo, wo);
  d3_sub(reflect, d3_muld(reflect, N, 2*d3_dot(wo, N)), wo);
  d3_minus(refract, wo);

  FOR_EACH(i, 0, NSTEPS) {
    R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
    CHK(R != 0);
    CHK(IS_INF(pdf) == 1);
    if(d3_eq_eps(wi, reflect, 1.e-6)) {
      CHK(type == (SSF_SPECULAR | SSF_REFLECTION));
    } else if(d3_eq_eps(wi, refract, 1e-6), 1) {
      CHK(type == (SSF_SPECULAR | SSF_TRANSMISSION));
    } else {
      FATAL("Unexpected value.\n");
    }
  }

  CHK(ssp_rng_ref_put(rng) == RES_OK);
  CHK(ssf_bsdf_ref_put(bsdf) == RES_OK);
  CHK(ssf_bsdf_ref_put(dummy) == RES_OK);

  return 0;
}
