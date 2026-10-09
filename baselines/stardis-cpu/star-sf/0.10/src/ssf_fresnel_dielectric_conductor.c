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
#include "ssf_fresnel_c.h"

#include <rsys_math.h>

struct fresnel_dielectric_conductor {
  double eta_i; /* Refraction id of the dielectric medium */
  double eta_t; /* Real part of the refraction index */
  double eta_k_t; /* Imaginary part of the refraction index */
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
/* Code from
 * https://seblagarde.wordpress.com/2013/04/29/memo-on-fresnel-equations/ */
static double
fresnel_dielectric_conductor_eval(void* fresnel, const double cos_theta_i)
{
  const struct fresnel_dielectric_conductor* fc = fresnel;
  double cos_theta_i2;
  double sin_theta_i2;
  double eta, eta_k, eta2, eta_k2;
  double t0, t1, t2, t3, t4;
  double a;
  double a2_add_b2;
  double Rp, Rs;
  ASSERT(fresnel && cos_theta_i >= 0 && fc->eta_i > 0);

  cos_theta_i2 = cos_theta_i * cos_theta_i;
  sin_theta_i2 = 1.0 - cos_theta_i2;
  eta = fc->eta_t / fc->eta_i; /* Real part */
  eta_k = fc->eta_k_t / fc->eta_i; /* Imaginary part */
  eta2 = eta*eta;
  eta_k2 = eta_k*eta_k;

  t0 = eta2 - eta_k2 - sin_theta_i2;
  a2_add_b2 = sqrt(t0*t0 + 4*eta2*eta_k2);
  t1 = a2_add_b2 + cos_theta_i2;
  a = sqrt(0.5 * (a2_add_b2 + t0));
  t2 = 2.0*a*cos_theta_i;
  Rs = (t1 - t2) / (t1 + t2);

  t3 = cos_theta_i2 * a2_add_b2 + sin_theta_i2 * sin_theta_i2;
  t4 = t2 * sin_theta_i2;
  Rp = Rs * (t3 - t4) / (t3 + t4);
  return 0.5 * (Rp + Rs);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_fresnel_type ssf_fresnel_dielectric_conductor = {
  NULL,
  NULL,
  fresnel_dielectric_conductor_eval,
  sizeof(struct fresnel_dielectric_conductor),
  ALIGNOF(struct fresnel_dielectric_conductor)
};

res_T
ssf_fresnel_dielectric_conductor_setup
  (struct ssf_fresnel* fresnel,
   const double eta_i,
   const double eta_t,
   const double eta_k_t)
{
  struct fresnel_dielectric_conductor* fc;
  if(!fresnel
  || !FRESNEL_TYPE_EQ(&fresnel->type, &ssf_fresnel_dielectric_conductor)) {
    return RES_BAD_ARG;
  }
  fc = fresnel->data;
  fc->eta_i = eta_i;
  fc->eta_t = eta_t;
  fc->eta_k_t = eta_k_t;
  return RES_OK;
}

