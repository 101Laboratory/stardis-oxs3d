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
  const size_t NSTEPS = 100000;
  struct mem_allocator allocator;
  struct ssp_rng* rng;
  struct ssf_bsdf* brdf;
  struct ssf_bsdf* dummy;
  struct ssf_fresnel* fresnel;
  double wo[3], wi[3];
  double N[3];
  double pdf;
  double R; /* Directional reflectance */
  size_t i;
  int type;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng) == RES_OK);
  CHK(ssf_bsdf_create(&allocator, &ssf_specular_reflection, &brdf) == RES_OK);
  CHK(ssf_bsdf_create(&allocator, &bsdf_dummy, &dummy) == RES_OK);
  CHK(ssf_fresnel_create
    (&allocator, &ssf_fresnel_dielectric_dielectric, &fresnel) == RES_OK);
  CHK(ssf_fresnel_dielectric_dielectric_setup(fresnel, 0.7, 1) == RES_OK);

  CHK(ssf_specular_reflection_setup(NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_specular_reflection_setup(brdf, NULL) == RES_BAD_ARG);
  CHK(ssf_specular_reflection_setup(NULL, fresnel) == RES_BAD_ARG);
  CHK(ssf_specular_reflection_setup(brdf, fresnel) == RES_OK);
  CHK(ssf_specular_reflection_setup(dummy, fresnel) == RES_BAD_ARG);

  d3(N, 0.0, 1.0, 0.0);
  d3_normalize(wo, d3(wo, 1.0, 1.0, 0.0));
  R = ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf);
  CHK(eq_eps(R, ssf_fresnel_eval(fresnel, d3_dot(N, wi)), 1.e-6) == 1);
  CHK(IS_INF(pdf) == 1);
  CHK(type == (SSF_SPECULAR|SSF_REFLECTION));

  pdf = 0, type = 0;
  CHK(ssf_bsdf_sample(brdf, rng, wo, N, wi, NULL, &pdf) == R);
  CHK(IS_INF(pdf));
  CHK(ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, NULL) == R);
  CHK(type == (SSF_SPECULAR|SSF_REFLECTION));
  CHK(ssf_bsdf_sample(brdf, rng, wo, N, wi, NULL, NULL) == R);

  d3_normalize(wo, d3(wo, 1.0, 1.0, 0.0));
  CHK(ssf_specular_reflection_setup(brdf, fresnel) == RES_OK);
  R = ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf);
  CHK(eq_eps(R, ssf_fresnel_eval(fresnel, d3_dot(N, wi)), 1.e-6) == 1);
  CHK(IS_INF(pdf) == 1);
  CHK(d3_eq_eps(d3(wo, -wo[0], wo[1], 0.0), wi, 1.e-6) == 1);
  CHK(type == (SSF_SPECULAR|SSF_REFLECTION));

  CHK(ssf_bsdf_eval(brdf, wo, N, wi) == 0.0);
  CHK(ssf_bsdf_pdf(brdf, wo, N, wi) == 0.0);

  d3(wo, 0.0, 1.0, 0.0);
  R = ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf);
  CHK(eq_eps(R, ssf_fresnel_eval(fresnel, d3_dot(N, wi)), 1.e-6) == 1);
  CHK(IS_INF(pdf) == 1);
  CHK(d3_eq_eps(d3(wo, 0.0, 1.0, 0.0), wi, 1.e-6) == 1);
  CHK(type == (SSF_SPECULAR|SSF_REFLECTION));

  d3_normalize(wo, d3(wo, -1.0, 1.0, 0.0));
  R = ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf);
  CHK(eq_eps(R, ssf_fresnel_eval(fresnel, d3_dot(N, wi)), 1.e-6) == 1);
  CHK(IS_INF(pdf) == 1);
  CHK(d3_eq_eps(d3(wo, -wo[0], wo[1], 0.0), wi, 1.e-6) == 1);
  CHK(type == (SSF_SPECULAR|SSF_REFLECTION));

  wo[0] = ssp_rng_uniform_double(rng, -1, 1);
  wo[1] = ssp_rng_uniform_double(rng, 0, 1);
  wo[2] = ssp_rng_uniform_double(rng, -1, 1);
  d3_normalize(wo, wo);
  R = ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf);
  CHK(type == (SSF_SPECULAR|SSF_REFLECTION));

  FOR_EACH(i, 0, NSTEPS) {
    CHK(eq_eps(ssf_bsdf_sample(brdf, rng, wo, N, wi, &type, &pdf), R, 1.e-6));
    CHK(type == (SSF_SPECULAR|SSF_REFLECTION));
  }

  CHK(ssf_bsdf_ref_put(brdf) == RES_OK);
  CHK(ssf_bsdf_ref_put(dummy) == RES_OK);
  CHK(ssf_fresnel_ref_put(fresnel) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
