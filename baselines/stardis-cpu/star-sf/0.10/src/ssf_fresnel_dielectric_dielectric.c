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
#include <rsys/math.h>
#include <rsys_math.h>

struct fresnel_dielectric_dielectric {
  double eta_i;
  double eta_t;
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static double
fresnel_dielectric_dielectric_eval(void* fresnel, const double cos_theta_i)
{
  const struct fresnel_dielectric_dielectric* fd = fresnel;
  double sin_theta_i;
  double sin_theta_t;
  double cos_theta_t;
  double Rp; /* Parallel */
  double Rs; /* Orthogonal */
  ASSERT(fresnel && cos_theta_i >= 0 && fd->eta_t > 0 && fd->eta_i > 0);

  /* Use Snell's low to retrieve cos_theta_t:
   * eta_i * sin_theta_i = eta_t * sin_theta_t */
  sin_theta_i = sqrt(MMAX(0.0, 1.0 - cos_theta_i*cos_theta_i));
  sin_theta_t = fd->eta_i * sin_theta_i / fd->eta_t;
  if(sin_theta_t >= 1) return 1.0; /* Full reflection */
  cos_theta_t = sqrt(1.0 - sin_theta_t*sin_theta_t);

  /* Compute the reflectance for the light polarized with its electric field
   * parallel to the plane of incidence */
  Rp = (fd->eta_i*cos_theta_t - fd->eta_t*cos_theta_i)
     / (fd->eta_i*cos_theta_t + fd->eta_t*cos_theta_i);
  Rp = Rp * Rp;

  /* Compute the reflectance for the light polarized with its electric field
   * perpendicular to the plane of incidence */
  Rs = (fd->eta_i*cos_theta_i - fd->eta_t*cos_theta_t)
     / (fd->eta_i*cos_theta_i + fd->eta_t*cos_theta_t);
  Rs = Rs * Rs;

  return 0.5 * (Rp + Rs);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_fresnel_type ssf_fresnel_dielectric_dielectric = {
  NULL,
  NULL,
  fresnel_dielectric_dielectric_eval,
  sizeof(struct fresnel_dielectric_dielectric),
  ALIGNOF(struct fresnel_dielectric_dielectric)
};

res_T
ssf_fresnel_dielectric_dielectric_setup
  (struct ssf_fresnel* fresnel, const double eta_i, const double eta_t)
{
  struct fresnel_dielectric_dielectric* fd;
  if(!fresnel
  || !FRESNEL_TYPE_EQ(&fresnel->type, &ssf_fresnel_dielectric_dielectric)) {
    return RES_BAD_ARG;
  }
  fd = fresnel->data;
  fd->eta_i = eta_i;
  fd->eta_t = eta_t;
  return RES_OK;
}

