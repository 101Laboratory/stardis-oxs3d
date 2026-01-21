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

#ifndef TEST_SSF_UTILS_H
#define TEST_SSF_UTILS_H

#include<rsys/double33.h>
#include <rsys/math.h>
#include<rsys/mem_allocator.h>

#include <star/ssp.h>

#include <stdio.h>

#ifdef COMPILER_CL
  #pragma warning(disable:4324) /* Structre was padded due to alignment */
#endif

/*******************************************************************************
 * Dummy BSDF type
 ******************************************************************************/
static res_T
bsdf_dummy_init(struct mem_allocator* allocator, void* bsdf)
{
  (void)allocator, (void)bsdf;
  return RES_OK;
}

static void
bsdf_dummy_release(void* bsdf)
{
  (void)bsdf;
}

static double
bsdf_dummy_sample
  (void* bsdf,
   struct ssp_rng* rng,
   const double w[3],
   const double N[3],
   double dir[3],
   int* type,
   double* pdf)
{
  (void)bsdf, (void)rng, (void)w, (void)N, (void)dir, (void)type, (void)pdf;
  return 0.0;
}

static double
bsdf_dummy_eval
  (void* bsdf,
   const double wo[3],
   const double N[3],
   const double wi[3])
{
  (void)bsdf, (void)wo, (void)N, (void)wi;
  return 0.0;
}

static double
bsdf_dummy_pdf
  (void* bsdf,
   const double wo[3],
   const double N[3],
   const double wi[3])
{
  (void)bsdf, (void)wo, (void)N, (void)wi;
  return 0.0;
}

static const struct ssf_bsdf_type bsdf_dummy = {
  bsdf_dummy_init,
  bsdf_dummy_release,
  bsdf_dummy_sample,
  bsdf_dummy_eval,
  bsdf_dummy_pdf,
  0, 1
};

/*******************************************************************************
 * Dummy Fresnel type
 ******************************************************************************/
static res_T
fresnel_dummy_init(struct mem_allocator* allocator, void* fresnel)
{
  (void)allocator, (void)fresnel;
  return RES_OK;
}

static void
fresnel_dummy_release(void* fresnel)
{
  (void)fresnel;
}

static double
fresnel_dummy_eval
  (void* fresnel,
   const double cos_theta_i)
{
  (void)fresnel, (void)cos_theta_i;
  return 0.0;
}

static const struct ssf_fresnel_type fresnel_dummy = {
  fresnel_dummy_init,
  fresnel_dummy_release,
  fresnel_dummy_eval,
  0, 1
};

/*******************************************************************************
 * Dummy microfacet distribution type
 ******************************************************************************/
static res_T
microfacet_dummy_init(struct mem_allocator* allocator, void* distrib)
{
  (void)allocator, (void)distrib;
  return RES_OK;
}

static void
microfacet_dummy_release(void* distrib)
{
  (void)distrib;
}

static void
microfacet_dummy_sample
  (void* distrib,
   struct ssp_rng* rng,
   const double N[3],
   double dir[3],
   double* pdf)
{
  (void)distrib, (void)rng, (void)N, (void)dir, (void)pdf;
}

static double
microfacet_dummy_eval
  (void* distrib,
   const double N[3],
   const double wi[3])
{
  (void)distrib, (void)N, (void)wi;
  return 0.0;
}

static double
microfacet_dummy_pdf
  (void* distrib,
   const double N[3],
   const double wi[3])
{
  (void)distrib, (void)N, (void)wi;
  return 0.0;
}

static const struct ssf_microfacet_distribution_type microfacet_dummy = {
  microfacet_dummy_init,
  microfacet_dummy_release,
  microfacet_dummy_sample,
  microfacet_dummy_eval,
  microfacet_dummy_pdf,
  0, 1
};

/*******************************************************************************
 * Dummy Phase function type
 ******************************************************************************/
static res_T
phase_dummy_init(struct mem_allocator* allocator, void* phase)
{
  (void)allocator, (void)phase;
  return RES_OK;
}

static void
phase_dummy_release(void* phase)
{
  (void)phase;
}

static void
phase_dummy_sample
  (void* phase,
   struct ssp_rng* rng,
   const double w[3],
   double dir[3],
   double* pdf)
{
  (void)phase, (void)rng, (void)w, (void)dir, (void)pdf;
}

static double
phase_dummy_eval
  (void* phase,
   const double wo[3],
   const double wi[3])
{
  (void)phase, (void)wo, (void)wi;
  return 0.0;
}

static double
phase_dummy_pdf
  (void* phase,
   const double wo[3],
   const double wi[3])
{
  (void)phase, (void)wo, (void)wi;
  return 0.0;
}

static const struct ssf_phase_type phase_dummy = {
  phase_dummy_init,
  phase_dummy_release,
  phase_dummy_sample,
  phase_dummy_eval,
  phase_dummy_pdf,
  0, 1
};

/*******************************************************************************
 * Miscellaneous functions
 ******************************************************************************/
static INLINE void
check_microfacet_distribution
  (struct ssf_microfacet_distribution* distrib,
   struct ssp_rng* rng)
{
  double N[3];
  const size_t NSTEPS = 10000;
  size_t i;
  size_t n;
  double sample[3];
  double E, V, SE;
  double sum, sum2;

  N[0] = ssp_rng_uniform_double(rng, -1, 1);
  N[1] = ssp_rng_uniform_double(rng, -1, 1);
  N[2] = ssp_rng_uniform_double(rng, -1, 1);
  d3_normalize(N, N);

  /* Check that D(wh) is normalized wrt \int_{2PI} D(wh) |wh.n| dwh */
  sum = sum2 = 0;
  n = 0;
  do {
    FOR_EACH(i, 0, NSTEPS) {
      double wh[3], pdf, D, weight;
      ssp_ran_hemisphere_cos(rng, N, sample, &pdf);
      d3_set(wh, sample);
      D = ssf_microfacet_distribution_eval(distrib, N, wh);
      weight = D * d3_dot(wh, N) / pdf;
      sum += weight;
      sum2 += weight*weight;
    }
    n += NSTEPS;
    E = sum / (double)n;
    V = MMAX(sum2 / (double)n - E*E, 0);
    SE = sqrt(V/(double)n);
  } while(SE > E * 0.05);
  CHK(eq_eps(E, 1.0, 3*SE) == 1);

  /* Check the sampling of a direction wh and the returned pdf */
  FOR_EACH(i, 0, NSTEPS) {
    double wh[3], pdf, D, weight;

    ssf_microfacet_distribution_sample(distrib, rng, N, wh, &pdf);
    D = ssf_microfacet_distribution_eval(distrib, N, wh);
    weight = D * d3_dot(wh, N) / pdf;
    CHK(eq_eps(weight, 1.0, 1.e-6) == 1);
    CHK(eq_eps(pdf, ssf_microfacet_distribution_pdf(distrib, N, wh), 1.e-6f));

    ssf_microfacet_distribution_sample(distrib, rng, N, wh, NULL);
  }
}

static INLINE void
check_memory_allocator(struct mem_allocator* allocator)
{
  if(MEM_ALLOCATED_SIZE(allocator)) {
    char dump[512];
    MEM_DUMP(allocator, dump, sizeof(dump)/sizeof(char));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks\n");
  }
}

#endif /* TEST_SSF_UTILS_H */

