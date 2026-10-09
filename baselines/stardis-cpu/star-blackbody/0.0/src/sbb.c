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
#include <rsys_math.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static double
wiebelt(const double v)
{
  int m;
  double w, v2, v4;
  /*.153989717364e+00;*/
  const double fifteen_over_pi_power_4 = 15.0/(PI*PI*PI*PI);
  const double z0 = 1.0/3.0;
  const double z1 = 1.0/8.0;
  const double z2 = 1.0/60.0;
  const double z4 = 1.0/5040.0;
  const double z6 = 1.0/272160.0;
  const double z8 = 1.0/13305600.0;

  if(v >= 2.) {
    w = 0;
    for(m=1; m<6; ++m) {
      w += exp(-m*v)/(m*m*m*m) * (((m*v+3)*m*v+6)*m*v+6);
    }
    w = w * fifteen_over_pi_power_4;
  } else {
    v2 = v*v;
    v4 = v2*v2;
    w = z0 - z1*v + z2*v2 - z4*v2*v2 + z6*v4*v2 - z8*v4*v4;
    w = 1. - fifteen_over_pi_power_4*v2*v*w;
  }
  ASSERT(w >= 0.0 && w <= 1.0);
  return w;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
double
sbb_blackbody_fraction
  (const double lambda0, /* [m] */
   const double lambda1, /* [m] */
   const double temperature) /* [K] */
{
  const double C2 = 1.43877735e-2; /* [m.K] */
  const double x0 = C2 / lambda0;
  const double x1 = C2 / lambda1;
  const double v0 = x0 / temperature;
  const double v1 = x1 / temperature;
  const double w0 = wiebelt(v0);
  const double w1 = wiebelt(v1);
  return w1 - w0;
}

double /* [W/m^2/sr/m ] */
sbb_planck_monochromatic
  (const double lambda, /* [m] */
   const double temperature) /* [K] */
{
  const double c = 299792458; /* [m/s] */
  const double h = 6.62607015e-34; /* [J.s] */
  const double k = 1.380649e-23; /* [J/K] */
  const double lambda2 = lambda*lambda;
  const double lambda5 = lambda2*lambda2*lambda;
  const double B = /* [W/m²/sr/m] */
    ((2.0 * h * c*c) / lambda5)
  / (exp(h*c/(lambda*k*temperature))-1.0);
  ASSERT(temperature > 0);
  return B;
}

double /* [W/m^2/sr/m ] */
sbb_planck_interval
  (const double lambda_min, /* [m] */
   const double lambda_max, /* [m] */
   const double temperature) /* [K] */
{
  const double T2 = temperature*temperature;
  const double T4 = T2*T2;
  const double BOLTZMANN_CONSTANT = 5.6696e-8; /* [W/m^2/K^4] */
  ASSERT(lambda_min < lambda_max && temperature > 0);
  return sbb_blackbody_fraction(lambda_min, lambda_max, temperature)
       * BOLTZMANN_CONSTANT * T4 / (PI * (lambda_max-lambda_min));
}

res_T
sbb_brightness_temperature
  (const double lambda_min, /* [m] */
   const double lambda_max, /* [m] */
   const double radiance, /* [W/m^2/sr/m] */
   double* temperature) /* [K] */
{
  const size_t MAX_ITER = 100;
  const double epsilon_T = 1e-4; /* [K] */
  const double epsilon_B = radiance * 1e-8;
  double T, T0, T1, T2;
  double B, B0;
  size_t i;
  res_T res = RES_OK;

  if(lambda_min < 0
  || lambda_max < 0
  || lambda_min > lambda_max
  || radiance < 0
  || temperature == NULL) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Search for a brightness temperature whose radiance is greater than or
   * equal to the estimated radiance */
  T2 = 200;
  FOR_EACH(i, 0, MAX_ITER) {
    const double B2 = sbb_planck(lambda_min, lambda_max, T2);
    if(B2 >= radiance) break;
    T2 *= 2;
  }
  if(i >= MAX_ITER) { res = RES_BAD_OP; goto error; }

  B0 = T0 = T1 = 0;
  FOR_EACH(i, 0, MAX_ITER) {
    T = (T1+T2)*0.5;
    B = sbb_planck(lambda_min, lambda_max, T);

    if(B < radiance) {
      T1 = T;
    } else {
      T2 = T;
    }

    if(fabs(T-T0) < epsilon_T || fabs(B-B0) < epsilon_B)
      break;

    T0 = T;
    B0 = B;
  }
  if(i >= MAX_ITER) { res = RES_BAD_OP; goto error; }

  *temperature = T;

exit:
  return res;
error:
  goto exit;
}

res_T
sbb_radiance_temperature
  (const double lambda_min, /* [m] */
   const double lambda_max, /* [m] */
   const double radiance, /* [W/m^2/sr] */
   double* temperature)
{
  double radiance_avg = 0;
  double T = 0;
  res_T res = RES_OK;

  if(lambda_min < 0
  || lambda_max < 0
  || lambda_min > lambda_max
  || radiance < 0
  || temperature == NULL) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* From integrated radiance to average radiance in W/m^2/sr/m */
  radiance_avg = radiance;
  if(lambda_min != lambda_max) { /* !monochromatic */
    radiance_avg /= (lambda_max - lambda_min);
  }

  res = sbb_brightness_temperature(lambda_min, lambda_max, radiance_avg, &T);
  if(res != RES_OK) goto error;

exit:
  if(temperature) *temperature = T;
  return res;
error:
  T = 0;
  goto exit;
}
