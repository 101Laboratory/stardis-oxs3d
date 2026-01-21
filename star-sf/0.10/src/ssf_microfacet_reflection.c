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

struct microfacet_reflection {
  struct ssf_fresnel* fresnel;
  struct ssf_microfacet_distribution* distrib;
};

/*******************************************************************************
 * Microfacet functions
 ******************************************************************************/
static void
microfacet_reflection_release(void* data)
{
  struct microfacet_reflection* bsdf = data;
  ASSERT(bsdf);
  if(bsdf->fresnel)
    SSF(fresnel_ref_put(bsdf->fresnel));
  if(bsdf->distrib)
    SSF(microfacet_distribution_ref_put(bsdf->distrib));
}

static FINLINE double
microfacet_reflection_eval
  (void* data, const double wo[3], const double N[3], const double wi[3])
{
  struct microfacet_reflection* bsdf = data;
  double wh[3];
  double cos_wo_N, cos_wi_N, cos_wh_N, cos_wh_wo, cos_wh_wi;
  double F, D, G;
  ASSERT(bsdf && bsdf->fresnel && bsdf->distrib);
  ASSERT(wo && N && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N) && d3_is_normalized(wi));
  ASSERT(d3_dot(wo, N) > 0 && d3_dot(wi, N) > 0);

  cos_wi_N = d3_dot(wi, N);
  cos_wo_N = d3_dot(wo, N);

  d3_normalize(wh, d3_add(wh, wo, wi));
  cos_wh_N = d3_dot(wh, N);
  cos_wh_wi = d3_dot(wh, wi);
  cos_wh_wo = cos_wh_wi;

  F = ssf_fresnel_eval(bsdf->fresnel, cos_wh_wi);
  D = ssf_microfacet_distribution_eval(bsdf->distrib, N, wh);
  /* Cook Torrance geometry term */
  G = MMIN((2*cos_wh_N*cos_wo_N)/cos_wh_wo, (2*cos_wh_N*cos_wi_N)/cos_wh_wo);
  G = MMIN(1, G);

  return F*D*G/(4*cos_wi_N*cos_wo_N);
}

static FINLINE double
microfacet_reflection_sample
  (void* data,
   struct ssp_rng* rng,
   const double wo[3],
   const double N[3],
   double wi[3],
   int* type,
   double* pdf)
{
  struct microfacet_reflection* bsdf = data;
  double dir[3];
  double wh[3];
  double pdf_wh;
  double R;
  ASSERT(data && wo && N && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N) && d3_dot(wo, N) > 0);
  ASSERT(bsdf->distrib && bsdf->fresnel);

  ssf_microfacet_distribution_sample(bsdf->distrib, rng, N, wh, &pdf_wh);
  reflect(dir, wo, wh);
  if(pdf) *pdf = pdf_wh / (4.0*fabs(d3_dot(wo, N)));
  if(type) *type = SSF_REFLECTION | SSF_GLOSSY;
  R = d3_dot(dir, N) > 0 ? ssf_fresnel_eval(bsdf->fresnel, d3_dot(dir, wh)) : 0;
  d3_set(wi, dir);
  return R;
}

static FINLINE double
microfacet_reflection_pdf
  (void* data, const double wo[3], const double N[3], const double wi[3])
{
  struct microfacet_reflection* bsdf = data;
  double wh[3];
  double pdf_wh;
  ASSERT(data && wo && N && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N) && d3_is_normalized(wi));
  ASSERT(d3_dot(wo, N) > 0);
  ASSERT(bsdf->distrib);
  d3_normalize(wh, d3_add(wh, wi, wo));
  if(d3_dot(wh, N) < 0) d3_minus(wh, wh);
  pdf_wh = ssf_microfacet_distribution_pdf(bsdf->distrib, N, wh);
  return pdf_wh / (4.0*fabs(d3_dot(wo, N)));
}

/*******************************************************************************
 * Microfacet 2 functions
 ******************************************************************************/
static FINLINE double
microfacet2_reflection_sample
  (void* data,
   struct ssp_rng* rng,
   const double wo[3],
   const double N[3],
   double wi[3],
   int* type,
   double* pdf)
{
  struct microfacet_reflection* bsdf = data;
  double dir[3];
  double wh[3];
  double p;
  double troughput = 1;
  ASSERT(data && wo && N && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N) && d3_dot(N, wo) > 0.0);
  ASSERT(bsdf->distrib);

  do { /* Sample a micro facet that front faces 'wo' */
    ssf_microfacet_distribution_sample(bsdf->distrib, rng, N, wh, &p);
  } while(d3_dot(wo, wh) <= 0);

  reflect(dir, wo, wh);
  for(;;) {
    troughput *= ssf_fresnel_eval(bsdf->fresnel, d3_dot(wh, dir));

    /* Do not take care of inter-reflections for sampled directions that point
     * outward the macro surface, i.e. simply stop the "random walk". */
    if(d3_dot(dir, N) > 0) break;

    /* Handle directions that point toward the macro surface has
     * inter-reflections */
    do { /* Sample a microfacet that front faces 'wi' */
      ssf_microfacet_distribution_sample(bsdf->distrib, rng, N, wh, &p);
    } while(d3_dot(dir, wh) <= 0);
    reflect(dir, dir, wh);
  }

  if(pdf) *pdf = NaN;
  if(type) *type = SSF_REFLECTION | SSF_GLOSSY;
  d3_set(wi, dir);
  return troughput;
}

static FINLINE double
microfacet2_reflection_eval
  (void* data, const double wo[3], const double N[3], const double wi[3])
{
  (void)data, (void)wo, (void)N, (void)wi;
  return NaN;
}

static FINLINE double
microfacet2_reflection_pdf
  (void* data, const double wo[3], const double N[3], const double wi[3])
{
  (void)data, (void)wo, (void)N, (void)wi;
  return NaN;
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_bsdf_type ssf_microfacet_reflection = {
  NULL,
  microfacet_reflection_release,
  microfacet_reflection_sample,
  microfacet_reflection_eval,
  microfacet_reflection_pdf,
  sizeof(struct microfacet_reflection),
  ALIGNOF(struct microfacet_reflection)
};

const struct ssf_bsdf_type ssf_microfacet2_reflection = {
  NULL,
  microfacet_reflection_release,
  microfacet2_reflection_sample,
  microfacet2_reflection_eval,
  microfacet2_reflection_pdf,
  sizeof(struct microfacet_reflection),
  ALIGNOF(struct microfacet_reflection)
};

res_T
ssf_microfacet_reflection_setup
  (struct ssf_bsdf* bsdf,
   struct ssf_fresnel* fresnel,
   struct ssf_microfacet_distribution* distrib)
{
  struct microfacet_reflection* microfacet;
  if(!bsdf || !fresnel || !distrib)
    return RES_BAD_ARG;
  if(!BSDF_TYPE_EQ(&bsdf->type, &ssf_microfacet_reflection)
  && !BSDF_TYPE_EQ(&bsdf->type, &ssf_microfacet2_reflection))
    return RES_BAD_ARG;

  microfacet = bsdf->data;
  if(microfacet->fresnel != fresnel) {
    if(microfacet->fresnel) SSF(fresnel_ref_put(fresnel));
    SSF(fresnel_ref_get(fresnel));
    microfacet->fresnel = fresnel;
  }
  if(microfacet->distrib != distrib) {
    if(microfacet->distrib) SSF(microfacet_distribution_ref_put(distrib));
    SSF(microfacet_distribution_ref_get(distrib));
    microfacet->distrib = distrib;
  }
  return RES_OK;
}

