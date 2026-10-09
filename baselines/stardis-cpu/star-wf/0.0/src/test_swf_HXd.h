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

#include "swf.h"

#include <rsys/rsys.h>
#include <rsys_math.h>

#include <stdlib.h>

#ifndef TEST_SWF_DIMENSION
  #error "The TEST_SWF_DIMENSION macro must be defined"
#endif

/* 2D H function */
#if TEST_SWF_DIMENSION == 2
  #define SWF_HXD_TABULATE_ARGS_DEFAULT SWF_H2D_TABULATE_ARGS_DEFAULT
  #define swf_HXd_eval swf_H2d_eval
  #define swf_HXd_inverse swf_H2d_inverse
  #define swf_HXd_tabulate swf_H2d_tabulate

/* 3D H function */
#elif TEST_SWF_DIMENSION == 3
  #define SWF_HXD_TABULATE_ARGS_DEFAULT SWF_H3D_TABULATE_ARGS_DEFAULT
  #define swf_HXd_eval swf_H3d_eval
  #define swf_HXd_inverse swf_H3d_inverse
  #define swf_HXd_tabulate swf_H3d_tabulate
#else
  #error "Invalid TEST_SWF_DIMENSION"
#endif

/* Macro defining a generic function name for the dimension*/
#define Xd(Name) CONCAT(CONCAT(CONCAT(Name, _), TEST_SWF_DIMENSION), d)

static INLINE void
Xd(check_tabulation_creation)(void)
{
  struct swf_H_tabulate_args args = SWF_HXD_TABULATE_ARGS_DEFAULT;
  struct swf_tabulation* tab = NULL;

  CHK(swf_HXd_tabulate(NULL, &tab) == RES_BAD_ARG);
  CHK(swf_HXd_tabulate(&args, NULL) == RES_BAD_ARG);
  CHK(swf_HXd_tabulate(&args, &tab) == RES_OK);

  CHK(swf_tabulation_ref_get(NULL) == RES_BAD_ARG);
  CHK(swf_tabulation_ref_get(tab) == RES_OK);
  CHK(swf_tabulation_ref_put(NULL) == RES_BAD_ARG);
  CHK(swf_tabulation_ref_put(tab) == RES_OK);
  CHK(swf_tabulation_ref_put(tab) == RES_OK);

  args.x_min = SWF_HXD_TABULATE_ARGS_DEFAULT.x_max;
  args.x_max = SWF_HXD_TABULATE_ARGS_DEFAULT.x_min;
  CHK(swf_HXd_tabulate(&args, &tab) == RES_BAD_ARG);

  args.x_max = args.x_min;
  CHK(swf_HXd_tabulate(&args, &tab) == RES_BAD_ARG);

  args.x_min = SWF_HXD_TABULATE_ARGS_DEFAULT.x_min;
  args.x_max = SWF_HXD_TABULATE_ARGS_DEFAULT.x_max;
  args.step = 0;
  CHK(swf_HXd_tabulate(&args, &tab) == RES_BAD_ARG);

  args.step = 1.0e-2;
  CHK(swf_HXd_tabulate(&args, &tab) == RES_OK);
  CHK(swf_tabulation_ref_put(tab) == RES_OK);
}

static void
Xd(check_inversion)(void)
{
  FILE* fp = NULL;
  const double nsteps = 10;

  struct swf_H_tabulate_args args = SWF_HXD_TABULATE_ARGS_DEFAULT;
  struct swf_tabulation* tab = NULL;
  double pHx = 0;
  double x0 = 0;
  size_t i = 0;

  fp=fopen("H"STR(TEST_SWF_DIMENSION)"d.dat", "w");
  CHK(fp != NULL);

  CHK(swf_HXd_tabulate(&args, &tab) == RES_OK);

  for(x0 = args.x_min; x0 <= args.x_max; x0 += x0*args.step) {
    const double x1 = MMIN(x0 + x0*args.step, args.x_max);
    const double delta_x =  (x1 - x0) / (double)nsteps;

    for(i = 0; i < nsteps; ++i) {
      const double x = x0 + (double)i*delta_x;
      const double Hx = swf_HXd_eval(x);
      const double xl = swf_tabulation_inverse(tab, SWF_LINEAR, Hx);
      const double xq = swf_tabulation_inverse(tab, SWF_QUADRATIC, Hx);
      const double xi = swf_HXd_inverse(Hx);
      const double errl = fabs((x - xl) / x);
      const double errq = fabs((x - xq) / x);
      const double erri = fabs((x - xi) / x);

      /* Do not check the value of x if the corresponding H is indistinguishable
       * from an H calculated from a previous x. In other words, the H's are
       * numerically equal and any of the corresponding x-values is a valid
       * inversion result, numerically speaking. */
      if(Hx == pHx) continue;

      fprintf(fp, "%e %e %e %e %e\n", x, Hx, errq, errl, erri);
      if(1e-9 < Hx && Hx < (1.0 - 1e-9)) {
        CHK(errl < 1.e-5);
        CHK(errq < 1.e-7);
        CHK(erri < 1.e-7);
      } else {
        CHK(errl < 1.e-3);
        CHK(errq < 1.e-3);
        CHK(erri < 1.e-3);
      }
      pHx = Hx;
    }
  }

  CHK(fclose(fp) == 0);
  CHK(swf_tabulation_ref_put(tab) == RES_OK);
}

/* Undefine macros used for genericity */
#undef TEST_SWF_DIMENSION
#undef SWF_HXD_TABULATE_ARGS_DEFAULT
#undef swf_HXd_eval
#undef swf_HXd_inverse
#undef swf_HXd_tabulate
#undef Xd
