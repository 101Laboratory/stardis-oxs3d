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
#include "ssf_phase_c.h"

#include <rsys/double3.h>

#include <star/ssp.h>

struct hg { double g; };

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
hg_init(struct mem_allocator* allocator, void* phase)
{
  ASSERT(phase);
  (void)allocator;
  ((struct hg*)phase)->g = 0.0;
  return RES_OK;
}

static double
hg_pdf
  (void* data,
   const double wo[3],
   const double wi[3])
{
  double w[3];
  const struct hg* hg = data;
  ASSERT(data && wo && wi);
  return ssp_ran_sphere_hg_pdf(d3_minus(w, wo), hg->g, wi);
}

/* HG(theta) =  1/(4*PI) * (1 - g^2) / (1 + g^2 - 2*g*cos(theta))^3/2 */
static double
hg_eval(void* data, const double wo[3], const double wi[3])
{
  const struct hg* hg = data;
  double w[3];
  double g;
  double cos_theta;
  double denom;
  double val;
  ASSERT(data && wo && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(wi));

  g = hg->g;
  /* By convention wo points outward the scattering point. Revert it to point
   * inward the scattering point in order to compute the cosine of the
   * scattering angle */
  d3_minus(w, wo);
  cos_theta = d3_dot(w, wi);
  denom = 1 + g*g - 2*g*cos_theta;
  ASSERT(denom != 0);
  val = 1.0/(4.0*PI) * (1 - g*g) / (denom*sqrt(denom));
  ASSERT(eq_eps(val, hg_pdf(data, wo, wi), 1.e-6));
  return val;
}

static void
hg_sample
  (void* data,
   struct ssp_rng* rng,
   const double wo[3],
   double wi[3],
   double* pdf)
{
  const struct hg* hg = data;
  double w[3];
  ASSERT(data && wo && wi);

  /* By convention wo points outward the scattering point. Revert it to point
   * inward the scattering point as expected by the SSP library */
  ssp_ran_sphere_hg(rng, d3_minus(w, wo), hg->g, wi, pdf);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_phase_type ssf_phase_hg = {
  hg_init,
  NULL,
  hg_sample,
  hg_eval,
  hg_pdf,
  sizeof(struct hg),
  ALIGNOF(struct hg)
};

res_T
ssf_phase_hg_setup(struct ssf_phase* phase, const double g)
{
  if(!phase || g < -1 || g > 1) return RES_BAD_ARG;
  if(!PHASE_TYPE_EQ(&phase->type, &ssf_phase_hg)) return RES_BAD_ARG;
  ((struct hg*)phase->data)->g = g;
  return RES_OK;
}
