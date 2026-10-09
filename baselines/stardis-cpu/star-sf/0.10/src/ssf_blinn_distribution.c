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
#include "ssf_microfacet_distribution_c.h"

#include <rsys/double3.h>
#include<rsys/double33.h>

#include <star/ssp.h>

struct blinn_distribution {
  double exponent;
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
blinn_distribution_init(struct mem_allocator* allocator, void* distrib)
{
  ASSERT(distrib);
  (void)allocator;
  ((struct blinn_distribution*)distrib)->exponent = 32.0;
  return RES_OK;
}

static double
blinn_distribution_eval(void* distrib, const double N[3], const double wh[3])
{
  struct blinn_distribution* blinn = distrib;
  double cos_wh_N;
  ASSERT(distrib && N && wh);
  ASSERT(d3_is_normalized(N) && d3_is_normalized(wh));
  ASSERT(blinn->exponent >= 0.0);
  ASSERT(blinn->exponent <= SSF_BLINN_DISTRIBUTION_MAX_EXPONENT);
  cos_wh_N = d3_dot(wh, N);
  return (blinn->exponent + 2) / (2*PI) * pow(cos_wh_N, blinn->exponent);
}

static void
blinn_distribution_sample
  (void* distrib,
   struct ssp_rng* rng,
   const double N[3],
   double wh[3],
   double* pdf)
{
  struct blinn_distribution* blinn = distrib;
  double basis[9];
  double dir[3];
  double cos_theta, sin_theta;
  double phi;
  double u, v;
  ASSERT(distrib && rng && N && wh);
  ASSERT(d3_is_normalized(N));

  u = ssp_rng_canonical(rng);
  v = ssp_rng_canonical(rng);

  phi = v*2*PI;
  cos_theta = pow(u, 1/(blinn->exponent+1));
  sin_theta = sqrt(MMAX(0, 1-cos_theta*cos_theta));
  dir[0] = cos(phi) * sin_theta;
  dir[1] = sin(phi) * sin_theta;
  dir[2] = cos_theta;

  d33_muld3(dir, d33_basis(basis, N),  dir);
  d3_set(wh, dir);

  if(pdf) *pdf = (blinn->exponent+2)/(2*PI)*pow(cos_theta, blinn->exponent+1);
}

static double
blinn_distribution_pdf(void* distrib, const double N[3], const double wh[3])
{
  struct blinn_distribution* blinn = distrib;
  double cos_wh_N;
  ASSERT(distrib && N && wh);
  ASSERT(d3_is_normalized(N) && d3_is_normalized(wh));
  cos_wh_N = fabs(d3_dot(wh, N));
  return (blinn->exponent+2)/(2*PI)*pow(cos_wh_N, blinn->exponent+1);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_microfacet_distribution_type ssf_blinn_distribution = {
  blinn_distribution_init,
  NULL,
  blinn_distribution_sample,
  blinn_distribution_eval,
  blinn_distribution_pdf,
  sizeof(struct blinn_distribution),
  ALIGNOF(struct blinn_distribution)
};

res_T
ssf_blinn_distribution_setup
  (struct ssf_microfacet_distribution* distrib,
   const double exponent)
{
  if(!distrib || exponent < 0 || exponent > SSF_BLINN_DISTRIBUTION_MAX_EXPONENT)
    return RES_BAD_ARG;
  if(!MICROFACET_DISTRIBUTION_TYPE_EQ(&distrib->type, &ssf_blinn_distribution))
    return RES_BAD_ARG;
  ((struct blinn_distribution*)distrib->data)->exponent = exponent;
  return RES_OK;
}


