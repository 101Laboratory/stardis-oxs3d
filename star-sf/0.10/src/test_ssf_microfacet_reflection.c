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
#include <star/ssp.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct ssf_bsdf* bsdf;
  struct ssf_bsdf* dummy;
  struct ssf_fresnel* F;
  struct ssf_microfacet_distribution* D;
  struct ssp_rng* rng;
  double N[3];
  double wo[3];
  double wi[3];
  double pdf;
  double R;
  size_t i, NSTEPS=10000;
  int type;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng) == RES_OK);
  CHK(ssf_bsdf_create(&allocator, &ssf_microfacet2_reflection, &bsdf) == RES_OK);
  CHK(ssf_bsdf_create(&allocator, &bsdf_dummy, &dummy) == RES_OK);
  CHK(ssf_fresnel_create(&allocator, &ssf_fresnel_no_op, &F) == RES_OK);
  CHK(ssf_microfacet_distribution_create
    (&allocator, &ssf_beckmann_distribution, &D) == RES_OK);

  CHK(ssf_beckmann_distribution_setup(D, 0.9) == RES_OK);

  CHK(ssf_microfacet_reflection_setup(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(bsdf, NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(NULL, F, NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(bsdf, F, NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(NULL, NULL, D) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(bsdf, NULL, D) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(NULL, F, D) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(bsdf, F, D) == RES_OK);

  /* Check energy conservation */
  ssp_ran_sphere_uniform(rng, N, NULL);
  ssp_ran_hemisphere_cos(rng, N, wo, NULL);
  FOR_EACH(i, 0, NSTEPS) {
    CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf) == 1);
    CHK(type == (SSF_GLOSSY | SSF_REFLECTION));
    CHK(IS_NaN(pdf));
    CHK(d3_dot(wi, N) > 0);

    pdf = 0, type = 0;
    CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, NULL, &pdf) == 1);
    CHK(IS_NaN(pdf));
    CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, NULL) == 1);
    CHK(type == (SSF_GLOSSY | SSF_REFLECTION));
    CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, NULL, NULL) == 1);
  }

  CHK(ssf_bsdf_ref_put(bsdf) == RES_OK);
  CHK(ssf_bsdf_create(&allocator, &ssf_microfacet_reflection, &bsdf) == RES_OK);

  CHK(ssf_microfacet_reflection_setup(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(bsdf, NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(NULL, F, NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(bsdf, F, NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(NULL, NULL, D) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(bsdf, NULL, D) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(NULL, F, D) == RES_BAD_ARG);
  CHK(ssf_microfacet_reflection_setup(bsdf, F, D) == RES_OK);

  FOR_EACH(i, 0, NSTEPS) {
    R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, &pdf);
    CHK(R == (d3_dot(wi, N) > 0 ? 1 : 0));
    CHK(type == (SSF_GLOSSY | SSF_REFLECTION));
    CHK(eq_eps(pdf, ssf_bsdf_pdf(bsdf, wo, N, wi), 1.e-6));

    pdf = 0, type = 0;
    R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, NULL, &pdf);
    CHK(R == (d3_dot(wi, N) > 0 ? 1 : 0));
    CHK(eq_eps(pdf, ssf_bsdf_pdf(bsdf, wo, N, wi), 1.e-6));
    R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, &type, NULL);
    CHK(R == (d3_dot(wi, N) > 0 ? 1 : 0));
    CHK(type == (SSF_GLOSSY | SSF_REFLECTION));
    R = ssf_bsdf_sample(bsdf, rng, wo, N, wi, NULL, NULL);
    CHK(R == (d3_dot(wi, N) > 0 ? 1 : 0));
  }

  CHK(ssf_bsdf_ref_put(bsdf) == RES_OK);
  CHK(ssf_bsdf_ref_put(dummy) == RES_OK);
  CHK(ssf_fresnel_ref_put(F) == RES_OK);
  CHK(ssf_microfacet_distribution_ref_put(D) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

