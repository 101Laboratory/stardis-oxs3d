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

struct specular_reflection {
  struct ssf_fresnel* fresnel;
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static void
specular_reflection_release(void* data)
{
  struct specular_reflection* brdf = data;
  ASSERT(data);
  if(brdf->fresnel) SSF(fresnel_ref_put(brdf->fresnel));
}

static double
specular_reflection_sample
  (void* data,
   struct ssp_rng* rng,
   const double wo[3],
   const double N[3],
   double wi[3],
   int* type,
   double* pdf)
{
  struct specular_reflection* brdf = data;
  double cos_wo_N;
  double cos_wi_N;
  ASSERT(rng && wi && N && wo);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N) && d3_dot(wo, N) > 0);
  (void)rng;

  /* Reflect the outgoing direction wo with respect to the surface normal N */
  reflect(wi, wo, N);
  if(pdf) *pdf = INF;
  if(type) *type = SSF_REFLECTION | SSF_SPECULAR;

  cos_wi_N = cos_wo_N = d3_dot(wo, N);
  return ssf_fresnel_eval(brdf->fresnel, cos_wi_N);
}

static double
specular_reflection_eval
  (void* data, const double wo[3], const double N[3], const double wi[3])
{
  (void)data, (void)wi, (void)N, (void)wo;
  return 0.0;
}

static double
specular_reflection_pdf
  (void* data, const double wo[3], const double N[3], const double wi[3])
{
  (void)data, (void)wi, (void)N, (void)wo;
  return 0.0;
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_bsdf_type ssf_specular_reflection = {
  NULL,
  specular_reflection_release,
  specular_reflection_sample,
  specular_reflection_eval,
  specular_reflection_pdf,
  sizeof(struct specular_reflection),
  ALIGNOF(struct specular_reflection)
};

res_T
ssf_specular_reflection_setup
  (struct ssf_bsdf* bsdf,
   struct ssf_fresnel* fresnel)
{
  struct specular_reflection* spec;
  if(!bsdf || !fresnel)
    return RES_BAD_ARG;
  if(!BSDF_TYPE_EQ(&bsdf->type, &ssf_specular_reflection))
    return RES_BAD_ARG;

  spec = bsdf->data;

  if(spec->fresnel != fresnel) {
    if(spec->fresnel) SSF(fresnel_ref_put(spec->fresnel));
    SSF(fresnel_ref_get(fresnel));
    spec->fresnel = fresnel;
  }
  return RES_OK;
}

