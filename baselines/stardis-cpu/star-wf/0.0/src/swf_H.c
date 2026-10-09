/* Copyright (C) 2024 |Méso|Star> (contact@meso-star.com)
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

#define _XOPEN_SOURCE /* j1 */

#include "swf.h"
#include "swf_tabulation.h"

#include <rsys_math.h> /* PI */

#include <limits.h>
#include <rsys_math.h>

/* Location of the first positive zeros of the Bessel function J0.
 *
 * They are precalculated using the gsl_sf_bessel_zero_J0 function and defined
 * in their hexadecimal floating-point representation, to ensure that they match
 * the value calculated by the GSL to the nearest bit. The C program used to
 * calculate these values can be summarized as follows:
 *
 *   #include <stdio.h>
 *   #include <gsl/gsl_sf_bessel.h>
 *   int main(void)
 *   {
 *     for(unsigned i=1; i<=100; printf("%a\n", gsl_sf_bessel_zero_J0(i++)));
 *     return 0;
 *   }
 */
static const double j0_roots[] = {
  NaN,
  0x1.33d152e971b3bp+1, 0x1.6148f5b2c2e3ep+2, 0x1.14eb56cccded1p+3,
  0x1.79544008272abp+3, 0x1.ddca13ef271e1p+3, 0x1.212313f8a19edp+4,
  0x1.5362dd173f795p+4, 0x1.85a3b930156e8p+4, 0x1.b7e54a5fd5f1cp+4,
  0x1.ea27591cbbed8p+4, 0x1.0e34e13a66fe6p+5, 0x1.275637a9619eap+5,
  0x1.4077a7ed62935p+5, 0x1.59992c65d0d86p+5, 0x1.72bac0f810807p+5,
  0x1.8bdc6293f064ep+5, 0x1.a4fe0ee444c7p+5,  0x1.be1fc41a4c5fcp+5,
  0x1.d74180c9e41eap+5, 0x1.f06343d0971c9p+5, 0x1.04c28621f11ep+6,
  0x1.11536cb22d724p+6, 0x1.1de4554a1c2d6p+6, 0x1.2a753fa82047ap+6,
  0x1.37062b9535d1p+6,  0x1.439718e2e3795p+6, 0x1.50280769a218fp+6,
  0x1.5cb8f7079c7aep+6, 0x1.6949e79fb1f06p+6, 0x1.75dad918abf93p+6,
  0x1.826bcb5c9b61dp+6, 0x1.8efcbe585425p+6,  0x1.9b8db1fb017fbp+6,
  0x1.a81ea635cd31dp+6, 0x1.b4af9afb9610bp+6, 0x1.c1409040b2ea7p+6,
  0x1.cdd185fabf635p+6, 0x1.da627c2070f25p+6, 0x1.e6f372a97287p+6,
  0x1.f384698e45aa8p+6, 0x1.000ab06414167p+7, 0x1.06532c287ecd1p+7,
  0x1.0c9ba8119dec5p+7, 0x1.12e4241ced385p+7, 0x1.192ca04822089p+7,
  0x1.1f751c9124fd1p+7, 0x1.25bd98f60c82fp+7, 0x1.2c06157518087p+7,
  0x1.324e920cabc8fp+7, 0x1.38970ebb4d19ep+7, 0x1.3edf8b7f9f282p+7,
  0x1.4528085860164p+7, 0x1.4b708544666ep+7,  0x1.51b902429edb7p+7,
  0x1.58017f520a27dp+7, 0x1.5e49fc71bb6b2p+7, 0x1.649279a0d6701p+7,
  0x1.6adaf6de8e417p+7, 0x1.7123742a23dep+7,  0x1.776bf182e50dcp+7,
  0x1.7db46ee82b546p+7, 0x1.83fcec595afe4p+7, 0x1.8a4569d5e2445p+7,
  0x1.908de75d3884dp+7, 0x1.96d664eedd8edp+7, 0x1.9d1ee28a58fdap+7,
  0x1.a367602f39a33p+7, 0x1.a9afdddd15001p+7, 0x1.aff85b9386c73p+7,
  0x1.b640d952306b7p+7, 0x1.bc895718b8b88p+7, 0x1.c2d1d4e6cb72dp+7,
  0x1.c91a52bc19007p+7, 0x1.cf62d0985618dp+7, 0x1.d5ab4e7b3b7a4p+7,
  0x1.dbf3cc6485a69p+7, 0x1.e23c4a53f4a4p+7,  0x1.e884c8494bc31p+7,
  0x1.eecd46445169ap+7, 0x1.f515c444cee1p+7,  0x1.fb5e424a90284p+7,
  0x1.00d3602ab1e5p+8,  0x1.03f79f328d5a5p+8, 0x1.071bde3cc40b1p+8,
  0x1.0a401d49409dp+8,  0x1.0d645c57eeb4ep+8, 0x1.10889b68bae7ap+8,
  0x1.13acda7b92acep+8, 0x1.16d119906452p+8,  0x1.19f558a71eee5p+8,
  0x1.1d1997bfb2581p+8, 0x1.203dd6da0f199p+8, 0x1.236215f626682p+8,
  0x1.26865513ea1a6p+8, 0x1.29aa94334ca02p+8, 0x1.2cced35440fa3p+8,
  0x1.2ff31276bab31p+8, 0x1.3317519aadd77p+8, 0x1.363b90c00ef04p+8,
  0x1.395fcfe6d2fbfp+8
};
static const size_t j0_nroots = sizeof(j0_roots)/sizeof(*j0_roots);

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE res_T
check_swf_H_tabulate_args(const struct swf_H_tabulate_args* args)
{
  if(!args) return RES_BAD_ARG;

  /* X cannot be negative */
  if(args->x_min < 0)
    return RES_BAD_ARG;

  /* X range cannot be degenerated */
  if(args->x_min >= args->x_max)
    return RES_BAD_ARG;

  /* Delta X cannot be null */
  if(args->step <= 0)
    return RES_BAD_ARG;

  return RES_OK;
}

/* H(x)  = 1 - 2 Sum(k=1..INF)[exp(-x*ak^2) / (ak*J1(ak))]
 * H'(x) =     2 Sum(k=1..INF)[exp(-x*ak^2) * ak/J1(ak)]
 * H"(x) =   - 2 Sum(k=1..INF)[exp(-x*ak^2) * ak^3/J1(ak)] */
static INLINE double
H2d
  (const double x,
   double* dHx, /* First derivative. NULL <=> do not compute it */
   double* d2Hx) /* Second derivative. NULL <=> do not compute it */
{
  double Hx = 0; /* Value of the function */

  /* Sum of the terms in the series */
  double sum = 0; /* Sum of the terms */
  double sumd = 0; /* Sum of the derivative terms */
  double sumd2 = 0; /* Sum of the second derivative terms */

  unsigned k = 1; /* Index of the serie term */

  /* Have the calculations converged or are there errors? */
  int error = 1;
  int errord = dHx != NULL;
  int errord2 = d2Hx != NULL;

  ASSERT(x >= 0);

  if(x == 0) return 0;

  do {
    double ak, j, t, term, sum_next;
    CHK(k < j0_nroots);

    ak = j0_roots[k];
    j = j1(ak);
    t = exp(-x*ak*ak);
    term = t / (ak*j);
    sum_next = sum + term;

    error = sum_next != sum;
    sum = sum_next;

    if(dHx) { /* Derivative */
      const double termd = t/j * ak;
      const double sumd_next = sumd + termd;
      errord = sumd_next != sumd;
      sumd = sumd_next;
    }

    if(d2Hx) { /* Second derivative */
      const double termd2 = t/j * ak*ak*ak;
      const double sumd2_next = sumd2 + termd2;
      errord2 = sumd2_next != sumd2;
      sumd2 = sumd2_next;
    }

  } while((error || errord || errord2) && ++k < UINT_MAX);

  Hx = 1.0 - 2.0 * sum;

  if(dHx)  *dHx  =  2.0 * sumd;  /* Derivative */
  if(d2Hx) *d2Hx = -2.0 * sumd2; /* Second Derivative */

  return Hx;
}

/* H(x)  = 1 + 2 Sum(k=1..INF)[(-1)^k * exp(-(PI*k)^2 * x)]
 * H'(x) =   - 2 Sum(k=1..INF)[(-1)^k * exp(-(PI*k)^2 * x) * (PI*k)^2]
 * H"(x) =   + 2 Sum(k=1..INF)[(-1)^k * exp(-(PI*k)^2 * x) * (PI*k)^4] */
static INLINE double
H3d
  (const double x,
   double* dHx, /* Derivative. NULL <=> do not compute it */
   double* d2Hx) /* Second derivative. NULL <=> do not compute it */
{
  double Hx = 0; /* Value of the function */

  /* Sum of the terms in the series */
  double sum = 0; /* Sum of the terms */
  double sumd = 0; /* Sum of the derivative terms */
  double sumd2 = 0; /* Sum of the second derivative terms */

  double sign = -1; /* Sign of the serie term */
  unsigned k = 1; /* Index of the serie term */

  /* Have the calculations converged or are there errors? */
  int error = 1;
  int errord = dHx != NULL;
  int errord2 = d2Hx != NULL;

  ASSERT(x >= 0); /* Check pre-condition */

  /* Do not attempt to calculate 0; it would be numerically catastrophic.
   * Simply return its value */
  if(x == 0) return 0;

  do {
    const double t = PI*(double)k;
    const double t2 = t*t;
    const double term =  sign * exp(-t2*x);
    const double sum_next = sum + term;

    error = sum_next != sum;
    sum = sum_next;
    sign = -sign;

    if(dHx) { /* Derivative */
      const double termd = term * t2;
      const double sumd_next = sumd + termd;
      errord = sumd_next != sumd;
      sumd = sumd_next;
    }

    if(d2Hx) { /* Second derivative */
      const double termd2 = term * t2*t2;
      const double sumd2_next = sumd2 + termd2;
      errord2 = sumd2_next != sumd2;
      sumd2 = sumd2_next;
    }

  } while((error || errord || errord2) && ++k < UINT_MAX);

  Hx = 1.0 + 2.0 * sum;

  if(dHx)  *dHx  = -2.0 * sumd;  /* Derivative */
  if(d2Hx) *d2Hx =  2.0 * sumd2; /* Second derivative */
  return Hx;
}

/* Define generic dimension functions */
#define SWF_DIMENSION 2
#include "swf_HXd.h"
#define SWF_DIMENSION 3
#include "swf_HXd.h"

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
double
swf_H2d_eval(const double x)
{
  return H2d(x, NULL, NULL);
}

double
swf_H3d_eval(const double x)
{
  return H3d(x, NULL, NULL);
}

double
swf_H2d_inverse(const double y)
{
  return H_inverse2d(y,
    SWF_H2D_TABULATE_ARGS_DEFAULT.x_min,
    SWF_H2D_TABULATE_ARGS_DEFAULT.x_max,
    1.0e-12); /* Epsilon */
}

double
swf_H3d_inverse(const double y)
{
  return H_inverse3d(y,
    SWF_H3D_TABULATE_ARGS_DEFAULT.x_min,
    SWF_H3D_TABULATE_ARGS_DEFAULT.x_max,
    1.0e-12); /* Epsilon */
}

res_T
swf_H2d_tabulate
  (const struct swf_H_tabulate_args* args,
   struct swf_tabulation** out_tab)
{
  return H_tabulate2d(args, out_tab);
}

res_T
swf_H3d_tabulate
  (const struct swf_H_tabulate_args* args,
   struct swf_tabulation** out_tab)
{
  return H_tabulate3d(args, out_tab);
}
