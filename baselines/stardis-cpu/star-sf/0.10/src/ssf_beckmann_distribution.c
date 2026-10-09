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

struct beckmann_distribution {
  double roughness;
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
beckmann_distribution_init(struct mem_allocator* allocator, void* distrib)
{
  ASSERT(distrib);
  (void)allocator;
  ((struct beckmann_distribution*)distrib)->roughness = 1.0;
  return RES_OK;
}

static double
beckmann_distribution_eval
  (void* distrib, const double N[3], const double wh[3])
{
  struct beckmann_distribution* beckmann = distrib;
  double cos_wh_N, cos2_wh_N, sin2_wh_N, cos4_wh_N;
  double m2;
  ASSERT(distrib && N && wh);
  ASSERT(d3_is_normalized(N) && d3_is_normalized(wh));
  ASSERT(beckmann->roughness > 0);
  m2 = beckmann->roughness * beckmann->roughness;
  cos_wh_N = d3_dot(wh, N);
  cos2_wh_N = cos_wh_N * cos_wh_N;
  sin2_wh_N = 1.0 - cos2_wh_N;
  cos4_wh_N = cos2_wh_N * cos2_wh_N;
  return exp(-sin2_wh_N / (cos2_wh_N * m2)) / (PI*m2*cos4_wh_N);
}

static void
beckmann_distribution_sample
  (void* distrib,
   struct ssp_rng* rng,
   const double N[3],
   double wh[3],
   double* pdf)
{
  struct beckmann_distribution* beckmann = distrib;
  double basis[9];
  double dir[3];
  double m2;
  double phi;
  double rcp_T;
  double cos_theta_T, sin_theta_T;
  double cos_theta, sin_theta;
  double cos2_theta, sin2_theta;
  double u, v;
  ASSERT(rng && wh && N);
  ASSERT(d3_is_normalized(N));

  u = ssp_rng_canonical(rng);
  v = ssp_rng_canonical(rng);

  phi = 2.0*PI*u;
  m2 = beckmann->roughness * beckmann->roughness;
  sin_theta_T = sqrt(-log(v));
  cos_theta_T = sqrt(1.0/m2);
  rcp_T = 1.0 / sqrt(cos_theta_T*cos_theta_T + sin_theta_T*sin_theta_T);
  cos_theta = cos_theta_T * rcp_T;
  sin_theta = sin_theta_T * rcp_T;
  cos2_theta = cos_theta * cos_theta;
  sin2_theta = sin_theta * sin_theta;
  dir[0] = cos(phi) * sin_theta;
  dir[1] = sin(phi) * sin_theta;
  dir[2] = cos_theta;
  d33_muld3(wh, d33_basis(basis, N), dir);
  ASSERT(d3_dot(wh, N) > 0);
  if(pdf) *pdf = exp(-sin2_theta/(cos2_theta*m2))/(PI*m2*cos_theta*cos2_theta);
}

static double
beckmann_distribution_pdf
  (void* distrib, const double N[3], const double wh[3])
{
  struct beckmann_distribution* beckmann = distrib;
  double cos_wh_N, cos2_wh_N, sin2_wh_N;
  double m2;
  ASSERT(distrib && N && wh);
  ASSERT(d3_is_normalized(wh) && d3_is_normalized(N));
  cos_wh_N = d3_dot(wh, N);
  if(cos_wh_N < 0.0) return 0.0;
  m2 = beckmann->roughness * beckmann->roughness;
  cos2_wh_N = cos_wh_N * cos_wh_N;
  sin2_wh_N = 1.0 - cos2_wh_N;
  return exp(-sin2_wh_N/(cos2_wh_N*m2)) / (PI * m2 * cos_wh_N * cos2_wh_N);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_microfacet_distribution_type ssf_beckmann_distribution = {
  beckmann_distribution_init,
  NULL,
  beckmann_distribution_sample,
  beckmann_distribution_eval,
  beckmann_distribution_pdf,
  sizeof(struct beckmann_distribution),
  ALIGNOF(struct beckmann_distribution)
};

res_T
ssf_beckmann_distribution_setup
  (struct ssf_microfacet_distribution* distrib,
   const double roughness)
{
  if(!distrib || roughness <= 0 || roughness > 1) return RES_BAD_ARG;
  if(!MICROFACET_DISTRIBUTION_TYPE_EQ(&distrib->type, &ssf_beckmann_distribution))
    return RES_BAD_ARG;
  ((struct beckmann_distribution*)distrib->data)->roughness = roughness;
  return RES_OK;
}

