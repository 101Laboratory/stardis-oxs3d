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

#define _POSIX_C_SOURCE 200112L /* cbrt support */

#include "ssf.h"
#include "ssf_phase_c.h"

#include <rsys/double3.h>
#include <star/ssp.h>
#include <rsys_math.h>

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
rayleigh_init(struct mem_allocator* allocator, void* phase)
{
  (void)allocator, (void)phase;
  return RES_OK;
}

static void
rayleigh_release(void* phase)
{ (void)phase; }


/* Rayleigh(theta) = 3/(16*PI)*(1+cos(theta)^2) */
static double
rayleigh_eval(void* data, const double wo[3], const double wi[3])
{
  double cos_theta;
  double w[3];
  ASSERT(wo && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(wi));
  (void)data;
  /* By convention wo points outward the scattering point. Revert it to point
   * inward the scattering point in order to compute the cosine of the
   * scattering angle */
  d3_minus(w, wo);
  cos_theta = d3_dot(w, wi);
  return 3.0/(16.0*PI)*(1.0+cos_theta*cos_theta);
}

static void
rayleigh_sample
  (void* data,
   struct ssp_rng* rng,
   const double wo[3],
   double wi[3],
   double* pdf)
{
  double frame[9];
  double sample[3];
  double w[3];
  double phi, cos_theta, sin_theta;
  double u;
  double s;
  ASSERT(rng && wo && wi);
  (void)data;

  phi = ssp_rng_uniform_double(rng, 0, 2*PI);

  u = 4*ssp_rng_canonical(rng)-2;
  s = cbrt(u + sqrt(1+u*u));
  cos_theta = s - 1.0/s;
  sin_theta = cos2sin(cos_theta);

  sample[0] = cos(phi) * sin_theta;
  sample[1] = sin(phi) * sin_theta;
  sample[2] = cos_theta;

  /* By convention wo points outward the scattering point. Revert it to point
   * inward the scattering point */
  d3_minus(w, wo);
  d33_muld3(wi, d33_basis(frame, w), sample);

  if(pdf) *pdf = rayleigh_eval(data, wo, wi);
}

static double
rayleigh_pdf(void* data, const double wo[3], const double wi[3])
{
  return rayleigh_eval(data, wo, wi);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_phase_type ssf_phase_rayleigh = {
  rayleigh_init,
  rayleigh_release,
  rayleigh_sample,
  rayleigh_eval,
  rayleigh_pdf,
  0,
  1
};

