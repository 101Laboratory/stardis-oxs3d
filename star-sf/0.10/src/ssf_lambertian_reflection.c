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

#include <rsys/double3.h>
#include<rsys/double33.h>

#include <star/ssp.h>

struct lambertian_reflection {
  double reflectivity;
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static double
lambertian_reflection_eval
  (void* data, const double wo[3], const double N[3], const double wi[3])
{
  struct lambertian_reflection* brdf = data;
  ASSERT(data && N && wi);
  ASSERT(d3_is_normalized(N) && d3_is_normalized(wi));
  ASSERT(d3_dot(wi, N) > 0 && d3_dot(wo, N) > 0);
  (void)wo, (void)N, (void)wi;
  return brdf->reflectivity / PI;
}

static double
lambertian_reflection_sample
  (void* data,
   struct ssp_rng* rng,
   const double wo[3],
   const double N[3],
   double wi[3],
   int* type,
   double* pdf)
{
  double sample[3];
  ASSERT(data && rng && wo && N && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N) && d3_dot(wo, N) > 0);
  (void)wo;

  ssp_ran_hemisphere_cos(rng, N, sample, pdf);
  d3_set(wi, sample);
  if(type) *type = SSF_REFLECTION | SSF_DIFFUSE;
  return ((struct lambertian_reflection*)data)->reflectivity;
}

static double
lambertian_reflection_pdf
  (void* data, const double wo[3], const double N[3], const double wi[3])
{
  double cos_wi_N;
  ASSERT(data && wi && N);
  ASSERT(d3_is_normalized(N) && d3_is_normalized(wi));
  (void)data, (void)wo;
  cos_wi_N = d3_dot(wi, N);
  return cos_wi_N <= 0.0 ? 0.0 : cos_wi_N / PI;
}

/*******************************************************************************
 * Exorted symbols
 ******************************************************************************/
const struct ssf_bsdf_type ssf_lambertian_reflection = {
  NULL,
  NULL,
  lambertian_reflection_sample,
  lambertian_reflection_eval,
  lambertian_reflection_pdf,
  sizeof(struct lambertian_reflection),
  ALIGNOF(struct lambertian_reflection)
};

res_T
ssf_lambertian_reflection_setup(struct ssf_bsdf* bsdf, const double reflectivity)
{
  if(!bsdf || reflectivity < 0 || reflectivity > 1) return RES_BAD_ARG;
  if(!BSDF_TYPE_EQ(&bsdf->type, &ssf_lambertian_reflection)) return RES_BAD_ARG;
  ((struct lambertian_reflection*)bsdf->data)->reflectivity = reflectivity;
  return RES_OK;
}

