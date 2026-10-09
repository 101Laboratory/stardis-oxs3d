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

struct pillbox_distribution {
  double sin2_theta_max;
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
pillbox_distribution_init(struct mem_allocator* allocator, void* distrib)
{
  ASSERT(distrib);
  (void)allocator;
  ((struct pillbox_distribution*)distrib)->sin2_theta_max = 1.0;
  return RES_OK;
}

static void
pillbox_distribution_release(void* distrib)
{ (void)distrib; }

static double
pillbox_distribution_eval(void* distrib, const double N[3], const double wh[3])
{
  struct pillbox_distribution* pillbox = distrib;
  double cos2_theta_max;
  double cos2_wh_N;
  ASSERT(distrib && N && wh);
  ASSERT(d3_is_normalized(wh) && d3_is_normalized(N));
  cos2_wh_N = d3_dot(N, wh);
  cos2_wh_N *= cos2_wh_N;
  cos2_theta_max = 1.0 - pillbox->sin2_theta_max;
  /*     if |wh.N| >= theta_max then 0
   * <=> if cos(|wh.N|)^2 < cos(theta_max)^2 then 0 */
  return (cos2_wh_N >= cos2_theta_max) ? 1.0 / (PI - PI*cos2_theta_max) : 0.0;
}

static void
pillbox_distribution_sample
  (void* distrib,
   struct ssp_rng* rng,
   const double N[3],
   double wh[3],
   double* pdf)
{
  struct pillbox_distribution* pillbox = distrib;
  double basis[9];
  double dir[3];
  double cos2_theta_max;
  double phi, sin2_theta, sin_theta, cos_theta;
  ASSERT(rng && wh && N);
  ASSERT(d3_is_normalized(N));

  cos2_theta_max = 1 - pillbox->sin2_theta_max;
  sin2_theta = ssp_rng_uniform_double(rng, 0, pillbox->sin2_theta_max);
  sin_theta = sqrt(sin2_theta);
  cos_theta = sqrt(1 - sin2_theta);
  phi = ssp_rng_uniform_double(rng, 0, 2 * PI);
  dir[0] = cos(phi) * sin_theta;
  dir[1] = sin(phi) * sin_theta;
  dir[2] = cos_theta;
  d33_muld3(wh, d33_basis(basis, N), dir);

  if(pdf) *pdf = cos_theta / (PI-PI*cos2_theta_max);
}

static double
pillbox_distribution_pdf(void* distrib, const double N[3], const double wh[3])
{
  struct pillbox_distribution* pillbox = distrib;
  double cos_wh_N;
  double cos2_wh_N;
  double cos2_theta_max;
  ASSERT(distrib && N && wh);
  ASSERT(d3_is_normalized(wh) && d3_is_normalized(N));
  cos_wh_N = d3_dot(wh, N);
  if(cos_wh_N < 0.0) return 0.0;
  cos2_theta_max = 1 - pillbox->sin2_theta_max;
  cos2_wh_N = cos_wh_N * cos_wh_N;
  if(cos2_wh_N < cos2_theta_max) return 0.0;
  return cos_wh_N / (PI-PI*cos2_theta_max);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_microfacet_distribution_type ssf_pillbox_distribution = {
  pillbox_distribution_init,
  pillbox_distribution_release,
  pillbox_distribution_sample,
  pillbox_distribution_eval,
  pillbox_distribution_pdf,
  sizeof(struct pillbox_distribution),
  ALIGNOF(struct pillbox_distribution)
};

res_T
ssf_pillbox_distribution_setup
  (struct ssf_microfacet_distribution* distrib,
   const double roughness)
{
  double sin2_theta_max;
  if(!distrib || roughness <= 0 || roughness > 1) return RES_BAD_ARG;
  if(!MICROFACET_DISTRIBUTION_TYPE_EQ(&distrib->type, &ssf_pillbox_distribution))
    return RES_BAD_ARG;
  sin2_theta_max = sin(roughness);
  sin2_theta_max *= sin2_theta_max;
  ((struct pillbox_distribution*)distrib->data)->sin2_theta_max = sin2_theta_max;
  return RES_OK;
}

