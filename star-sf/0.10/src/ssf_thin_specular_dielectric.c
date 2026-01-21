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
#include "ssf_bsdf_c.h"
#include "ssf_optics.h"

#include <rsys/double3.h>
#include <star/ssp.h>

struct thin_specular_dielectric {
  struct ssf_fresnel* fresnel;
  double absorption; /* In [0, 1] */
  double eta_i; /* Refractive index of the incoming medium */
  double eta_t; /* Refractive index of the transmissive medium */
  double thickness;
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
thin_specular_dielectric_init(struct mem_allocator* allocator, void* data)
{
  struct thin_specular_dielectric* bsdf = data;
  ASSERT(bsdf);
  return ssf_fresnel_create
    (allocator, &ssf_fresnel_dielectric_dielectric, &bsdf->fresnel);
}

static void
thin_specular_dielectric_release(void* data)
{
  struct thin_specular_dielectric* bsdf = data;
  SSF(fresnel_ref_put(bsdf->fresnel));
}

static double
thin_specular_dielectric_sample
  (void* data,
   struct ssp_rng* rng,
   const double wo[3],
   const double N[3],
   double wi[3],
   int* type,
   double* pdf)
{
  struct thin_specular_dielectric* bsdf = data;
  struct ssf_fresnel* fresnel;
  double wt[3], tmp[3];
  double cos_wo_N, cos_wt_N;
  double dst;
  double eta; /* Ratio of eta_i / eta_t */
  double R, T;
  double rho1, rho2, rho2_sqr;
  double tau, tau_sqr;
  ASSERT(bsdf && rng && wi && N && wo);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N));
  ASSERT(d3_dot(wo, N) > -1.e-6);
  (void)rng;

  eta = bsdf->eta_i / bsdf->eta_t;
  if(!refract(wt, wo, N, eta)) { /* Total reflection */
    reflect(wi, wo, N);
    if(pdf) *pdf = INF;
    if(type) *type = SSF_SPECULAR | SSF_REFLECTION;
    return 1;
  }
  fresnel = bsdf->fresnel;

  cos_wo_N = MMAX(0.0, d3_dot(wo, N));
  SSF(fresnel_dielectric_dielectric_setup(fresnel, bsdf->eta_i, bsdf->eta_t));
  rho1 = ssf_fresnel_eval(fresnel, cos_wo_N);

  cos_wt_N = MMAX(0.0, d3_dot(wt, d3_minus(tmp, N)));
  SSF(fresnel_dielectric_dielectric_setup(fresnel, bsdf->eta_t, bsdf->eta_i));
  rho2 = ssf_fresnel_eval(fresnel, cos_wt_N);

  dst = bsdf->thickness / cos_wt_N;
  tau = exp(-bsdf->absorption * dst);

  tau_sqr = tau * tau;
  rho2_sqr = rho2 * rho2;

  R = rho1 + (1-rho1) * (1-rho2) * (rho2 * tau_sqr) / (1 - rho2_sqr*tau_sqr);
  T = (tau * (1 - rho1) * ( 1 - rho2)) / (1-tau_sqr*rho2_sqr);
#ifndef NDEBUG
  {
    const double A = ((1-tau) * (1-rho1)) / (1-rho2*tau);
    ASSERT(eq_eps(R + A + T, 1, 1.e-6)); /* Check energy conservation */
  }
#endif

  if(pdf) *pdf = INF;

  /* Importance sample the BTDF wrt R */
  if(ssp_rng_uniform_double(rng, 0, R + T) < R) {
    reflect(wi, wo, N);
    if(type) *type = SSF_SPECULAR | SSF_REFLECTION;
  } else { /* Sample the transmissive part */
    d3_minus(wi, wo);
    if(type) *type = SSF_SPECULAR | SSF_TRANSMISSION;
  }
  if(bsdf->absorption == 0) {
    /* avoid numerical loss of energy if no absorption */
    return 1;
  } else {
    return R + T;
  }
}

static double
thin_specular_dielectric_eval
  (void* bsdf, const double wo[3], const double N[3], const double wi[3])
{
   (void)bsdf, (void)wo, (void)N, (void)wi;
   return 0.0;
}

static double
thin_specular_dielectric_pdf
  (void* bsdf, const double wo[3], const double N[3], const double wi[3])
{
  (void)bsdf, (void)wo, (void)N, (void)wi;
  return 0.0;
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_bsdf_type ssf_thin_specular_dielectric = {
  thin_specular_dielectric_init,
  thin_specular_dielectric_release,
  thin_specular_dielectric_sample,
  thin_specular_dielectric_eval,
  thin_specular_dielectric_pdf,
  sizeof(struct thin_specular_dielectric),
  ALIGNOF(struct thin_specular_dielectric)
};

res_T
ssf_thin_specular_dielectric_setup
  (struct ssf_bsdf* bsdf,
   const double absorption,
   const double eta_i,
   const double eta_t,
   const double thickness)
{
  struct thin_specular_dielectric* data;

  if(!bsdf || thickness <= 0 || eta_i <= 0 || eta_t <= 0 || absorption < 0
  || absorption > 1)
    return RES_BAD_ARG;
  if(!BSDF_TYPE_EQ(&bsdf->type, &ssf_thin_specular_dielectric))
    return RES_BAD_ARG;

  data = bsdf->data;

  data->absorption = absorption;
  data->thickness = thickness;
  data->eta_i = eta_i;
  data->eta_t = eta_t;
  return RES_OK;
}

