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

#ifndef SSF_OPTICS_H
#define SSF_OPTICS_H

#include <rsys/double3.h>

/* Reflect the vector V wrt the normal N. By convention V points outward the
 * surface. */
static INLINE double*
reflect(double res[3], const double V[3], const double N[3])
{
  double tmp[3];
  double cos_V_N;
  ASSERT(res && V && N);
  ASSERT(d3_is_normalized(V) && d3_is_normalized(N));
  cos_V_N = d3_dot(V, N);
  d3_muld(tmp, N, 2*cos_V_N);
  d3_sub(res, tmp, V);
  return res;
}

/* Refract the vect V wrt the normal N using the relative refractive index eta.
 * Eta is the refraction index of the outside medium (where N points into)
 * devided by the refraction index of the inside medium. By convention N and V
 * points on the same side of the surface. Return res or NULL if the refraction
 * is impossible. */
static INLINE double*
refract(double res[3], const double V[3], const double N[3], const double eta)
{
  double tmp0[3];
  double tmp1[3];
  double cos_theta_i;
  double cos_theta_t;
  double sin2_theta_i;
  double sin2_theta_t;

  ASSERT(res && V && N);
  ASSERT(d3_is_normalized(V) && d3_is_normalized(N));
  cos_theta_i = d3_dot(V, N);
  sin2_theta_i = MMAX(0, 1.0 - cos_theta_i*cos_theta_i);
  sin2_theta_t = eta * eta * sin2_theta_i;
  if(sin2_theta_t >= 1) return NULL; /* Total reflection */
  cos_theta_t = sqrt(1 - sin2_theta_t);

  d3_muld(tmp0, V, eta);
  d3_muld(tmp1, N, eta * cos_theta_i - cos_theta_t);
  return d3_sub(res, tmp1, tmp0);
}

#endif /* SSF_OPTICS_H */

