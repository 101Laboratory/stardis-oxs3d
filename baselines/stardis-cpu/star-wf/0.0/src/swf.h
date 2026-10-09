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

#ifndef SWF_H
#define SWF_H

#include <rsys/rsys.h>

/* Library symbol management */
#if defined(SWF_SHARED_BUILD)
  #define SWF_API extern EXPORT_SYM
#else
  #define SWF_API extern
#endif

#ifndef NDEBUG
  #define SWF(Func) ASSERT(swf_ ## Func == RES_OK)
#else
  #define SWF(Func) swf_ ## Func
#endif

enum swf_prediction {
  SWF_LINEAR,
  SWF_QUADRATIC
};

/* Forward declarations of external data types */
struct mem_allocator;

struct swf_H_tabulate_args {
  double x_min;
  double x_max;

  /* The step member variable is used to calculate the x arguments to be
   * tabulated. Each x to be tabulated is calculated by adding its product with
   * the step value to the previous one. The delta x is therefore relative to
   * each x value: the smaller the x, the smaller the tabulation step. And this
   * is what we're looking for, since the function is more difficult to estimate
   * near 0 */
  double step; /* x_next = x + x*step: */

  /* Force renormalization of the function. Note that H is already normalized to
   * the correct interval, so normalization may be unnecessary. Worse still! It
   * could introduce numerical inaccuracy. So don't normalize if you don't
   * have to. */
  int normalize;

  struct mem_allocator* allocator; /* NULL <=> use default allocator */
};

/* The H function's default tab values guarantee both numerical accuracy and a
 * small memory footprint */
#define SWF_H2D_TABULATE_ARGS_DEFAULT__ {8.0e-3, 6.05, 1e-3, 0, NULL}
static const struct swf_H_tabulate_args SWF_H2D_TABULATE_ARGS_DEFAULT =
  SWF_H2D_TABULATE_ARGS_DEFAULT__;
#define SWF_H3D_TABULATE_ARGS_DEFAULT__ {7.5e-3, 3.337, 1e-3, 0, NULL}
static const struct swf_H_tabulate_args SWF_H3D_TABULATE_ARGS_DEFAULT =
  SWF_H3D_TABULATE_ARGS_DEFAULT__;

/* Forward declarations of opaque data types */
struct swf_tabulation;

BEGIN_DECLS

/* These functions evaluate the H function, as specified in:
 *
 *    "The floating random walk and its application to Monte Carlo solutions of
 *    heat equations" - Haji-Sheikh A. and Sparrow E.M., SIAM Journal on Applied
 *    Mathematics, 14(2): 370-389, 1966
 *
 * These function are expressed as an infinite series, which is numerically
 * evaluated by summing all terms, until convergence is reached.
 * Two functions are defined here, H2d and H3d, which are used in 2D and 3D
 * respectively:
 *                  +INF
 *                   __
 *    H2d(x) = 1 - 2 >_ exp(-x*ak^2) / (ak*J1(ak)); ak is the k^th root of J0
 *                   k=1
 *
 *                  +INF
 *                   __
 *    H3d(x) = 1 + 2 >_  (-1)^k * exp(-(PI*k)^2*x)
 *                   k=1
 *
 * The H function is the cumulated function of the probability density used to
 * sample passage times for a given sphere radius. Its parameter x is positive
 * and is equal to alpha*tau/r^2, with:
 *    - alpha the diffusivity of the solid material: lambda/(rho*C) [m^2/s]
 *    - tau > 0 the time interval [s]
 *    - r > 0 is the radius of the sphere [m] */
SWF_API double
swf_H2d_eval
  (const double x);

SWF_API double
swf_H3d_eval
  (const double x);

SWF_API double
swf_H3d_inverse
  (const double y); /* in [0, 1[ */

SWF_API double
swf_H2d_inverse
  (const double y); /* in [0, 1[ */

SWF_API res_T
swf_H2d_tabulate
  (const struct swf_H_tabulate_args* args,
   struct swf_tabulation** tab);

SWF_API res_T
swf_H3d_tabulate
  (const struct swf_H_tabulate_args* args,
   struct swf_tabulation** tab);

SWF_API res_T
swf_tabulation_ref_get
  (struct swf_tabulation* tab);

SWF_API res_T
swf_tabulation_ref_put
  (struct swf_tabulation* tab);

SWF_API double /* x */
swf_tabulation_inverse
  (const struct swf_tabulation* tab,
   const enum swf_prediction prediction,
   const double y); /* in [0, 1[ */

END_DECLS

#endif /* SWF_H */
