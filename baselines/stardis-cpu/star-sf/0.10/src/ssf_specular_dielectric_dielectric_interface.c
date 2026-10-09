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

struct specular_dielectric_dielectric_interface {
  struct ssf_fresnel* fresnel;
  double eta_i; /* Refractive index of the incoming medium */
  double eta_t; /* Refractive index of the transmissive medium */
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
ssf_specular_dielectric_dielectric_interface_init
  (struct mem_allocator* allocator, void* data)
{
  struct specular_dielectric_dielectric_interface* bsdf = data;
  ASSERT(bsdf);
  return ssf_fresnel_create
    (allocator, &ssf_fresnel_dielectric_dielectric, &bsdf->fresnel);
}

static void
ssf_specular_dielectric_dielectric_interface_release(void* data)
{
  struct specular_dielectric_dielectric_interface* bsdf = data;
  SSF(fresnel_ref_put(bsdf->fresnel));
}

static double
ssf_specular_dielectric_dielectric_interface_sample
  (void* data,
   struct ssp_rng* rng,
   const double wo[3],
   const double N[3],
   double wi[3],
   int* type,
   double* pdf)
{
  struct specular_dielectric_dielectric_interface* bsdf = data;
  struct ssf_fresnel* fresnel;
  double wt[3];
  double *refracted;
  double cos_wo_N;
  double eta; /* Ratio of eta_i / eta_t */
  double R;
  ASSERT(bsdf && rng && wi && N && wo);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N));
  ASSERT(d3_dot(wo, N) > -1.e-6);
  (void)rng;

  eta = bsdf->eta_i / bsdf->eta_t;
  refracted = refract(wt, wo, N, eta);
  if(!refracted) { /* Total reflection */
    reflect(wi, wo, N);
    if(pdf) *pdf = INF;
    if(type) *type = SSF_SPECULAR | SSF_REFLECTION;
    return 1;
  }
  fresnel = bsdf->fresnel;

  cos_wo_N = MMAX(0.0, d3_dot(wo, N));
  SSF(fresnel_dielectric_dielectric_setup(fresnel, bsdf->eta_i, bsdf->eta_t));
  R = ssf_fresnel_eval(fresnel, cos_wo_N);

  if(pdf) *pdf = INF;

  /* Sample the output direction wrt R */
  if(ssp_rng_canonical(rng) < R) {
    reflect(wi, wo, N);
    if(type) *type = SSF_SPECULAR | SSF_REFLECTION;
  } else {
    d3_set(wi, refracted);
    if(type) *type = SSF_SPECULAR | SSF_TRANSMISSION;
  }
  return 1;
}

static double
ssf_specular_dielectric_dielectric_interface_eval
  (void* bsdf, const double wo[3], const double N[3], const double wi[3])
{
   (void)bsdf, (void)wo, (void)N, (void)wi;
   return 0.0;
}

static double
ssf_specular_dielectric_dielectric_interface_pdf
  (void* bsdf, const double wo[3], const double N[3], const double wi[3])
{
  (void)bsdf, (void)wo, (void)N, (void)wi;
  return 0.0;
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_bsdf_type ssf_specular_dielectric_dielectric_interface = {
  ssf_specular_dielectric_dielectric_interface_init,
  ssf_specular_dielectric_dielectric_interface_release,
  ssf_specular_dielectric_dielectric_interface_sample,
  ssf_specular_dielectric_dielectric_interface_eval,
  ssf_specular_dielectric_dielectric_interface_pdf,
  sizeof(struct specular_dielectric_dielectric_interface),
  ALIGNOF(struct specular_dielectric_dielectric_interface)
};

res_T
ssf_specular_dielectric_dielectric_interface_setup
  (struct ssf_bsdf* bsdf,
   const double eta_i,
   const double eta_t)
{
  struct specular_dielectric_dielectric_interface* data;

  if(!bsdf || eta_i <= 0 || eta_t <= 0)
    return RES_BAD_ARG;
  if(!BSDF_TYPE_EQ(&bsdf->type, &ssf_specular_dielectric_dielectric_interface))
    return RES_BAD_ARG;

  data = bsdf->data;

  data->eta_i = eta_i;
  data->eta_t = eta_t;
  return RES_OK;
}

