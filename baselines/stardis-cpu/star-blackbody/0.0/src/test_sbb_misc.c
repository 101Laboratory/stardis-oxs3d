/* Copyright (C) 2018-2023 |Méso|Star> (contact@meso-star.com)
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

#include "sbb.h"
#include <rsys/math.h>

int
main(int argc, char** argv)
{
  const double lambda_lo = 3.6e-6; /* [m] */
  const double lambda_hi = 12.3e-6; /* [m] */
  const double Tref = 550; /* [K] */
  const double delta_lambda = lambda_hi - lambda_lo;

  double L; /* Radiance */
  double d; /* Temporary double */
  double T; /* Temperature */
  double ref; /* Reference value to be verified */
  (void)argc, (void)argv;

  d = sbb_blackbody_fraction(lambda_lo, lambda_hi, Tref);
  ref = 0.73033200108935725;
  CHK(eq_eps(d, ref, ref*1e-3));

  d = sbb_planck_monochromatic(lambda_lo, Tref);
  ref = 137.68759834775730e6;
  CHK(eq_eps(d, 137.68759834775730e6, ref*1e-5));

  d = sbb_planck_interval(lambda_lo, lambda_hi, Tref);
  ref = 1206.0731201171875/delta_lambda;
  CHK(eq_eps(d, ref, ref*1e-3));

  L = 1206.0731201171875/delta_lambda; /* [W/m^2/sr/m ] */
  CHK(sbb_brightness_temperature(lambda_hi, lambda_lo, L, &T) == RES_BAD_ARG);
  CHK(sbb_brightness_temperature(-lambda_lo, lambda_hi,-L, &T) == RES_BAD_ARG);
  CHK(sbb_brightness_temperature(-lambda_lo,-lambda_hi,-L, &T) == RES_BAD_ARG);
  CHK(sbb_brightness_temperature(lambda_lo, lambda_hi,-L, &T) == RES_BAD_ARG);
  CHK(sbb_brightness_temperature(lambda_lo, lambda_hi, L, NULL) == RES_BAD_ARG);
  CHK(sbb_brightness_temperature(lambda_lo, lambda_hi, L, &T) == RES_OK);
  CHK(eq_eps(T, Tref, Tref*1.e-4));

  L = 1206.0731201171875; /* [W/m^2/sr/m] */
  CHK(sbb_radiance_temperature(lambda_hi, lambda_lo, L, &T) == RES_BAD_ARG);
  CHK(sbb_radiance_temperature(-lambda_lo, lambda_hi, L, &T) == RES_BAD_ARG);
  CHK(sbb_radiance_temperature(-lambda_lo,-lambda_hi, L, &T) == RES_BAD_ARG);
  CHK(sbb_radiance_temperature(lambda_lo, lambda_hi,-L, &T) == RES_BAD_ARG);
  CHK(sbb_radiance_temperature(lambda_lo, lambda_hi, L, NULL) == RES_BAD_ARG);
  CHK(sbb_radiance_temperature(lambda_lo, lambda_hi, L, &T) == RES_OK);
  CHK(eq_eps(T, Tref, Tref*1e-4));

  return 0;
}
