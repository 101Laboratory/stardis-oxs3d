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

static double
rand_canonic(void)
{
  return (double)rand() / (double)((size_t)RAND_MAX+1);
}

static void
check_planck(void)
{
  struct sbb_ran_planck_create_args args = SBB_RAN_PLANCK_CREATE_ARGS_DEFAULT;
  struct sbb_ran_planck* planck = NULL;

  args.range[0] = 3.6e-6;
  args.range[1] = 12.3e-6;
  args.ref_temperature = 500;
  args.nbands = 0;

  CHK(sbb_ran_planck_create(NULL, &planck) == RES_BAD_ARG);
  CHK(sbb_ran_planck_create(&args, NULL) == RES_BAD_ARG);
  CHK(sbb_ran_planck_create(&args, &planck) == RES_OK);

  CHK(sbb_ran_planck_ref_get(NULL) == RES_BAD_ARG);
  CHK(sbb_ran_planck_ref_get(planck) == RES_OK);
  CHK(sbb_ran_planck_ref_put(NULL) == RES_BAD_ARG);
  CHK(sbb_ran_planck_ref_put(planck) == RES_OK);
  CHK(sbb_ran_planck_ref_put(planck) == RES_OK);

  args.range[0] = 1;
  args.range[1] = 0;
  CHK(sbb_ran_planck_create(&args, &planck) == RES_BAD_ARG);
  args.range[0] = 0;
  args.range[1] = 1;
  args.ref_temperature = -100;
  CHK(sbb_ran_planck_create(&args, &planck) == RES_BAD_ARG);
  args.range[0] = 3.6e-6;
  args.range[1] =-12.3e-6;
  args.ref_temperature = 300;
  CHK(sbb_ran_planck_create(&args, &planck) == RES_BAD_ARG);
}

static void
planck_integration
  (const double lambda_lo, /* [m] */
   const double lambda_hi, /* [m] */
   const double Tref, /* [K] */
   const double T0, /* [K] */
   const size_t nbands) /* > 0 <=> Speed up Planck sampling */
{
  struct sbb_ran_planck_create_args args = SBB_RAN_PLANCK_CREATE_ARGS_DEFAULT;
  struct sbb_ran_planck* planck = NULL;

  const double delta_lambda = lambda_hi - lambda_lo; /* [m] */

  /* Variables of the Monte Carlo integration */
  const size_t N = 10000; /* #realisations */
  size_t irealisation = 0;
  double sum = 0; /* Sum of weights */
  double sum2 = 0; /* Sum of weights squared */
  double E = 0; /* Expected value */
  double V = 0; /* Variance */
  double SE = 0; /* Standard Error */

  double ref = 0;

  /* Setup the planck Distribution */
  args.range[0] = lambda_lo;
  args.range[1] = lambda_hi;
  args.ref_temperature = Tref;
  args.nbands = nbands;
  CHK(sbb_ran_planck_create(&args, &planck) == RES_OK);

  FOR_EACH(irealisation, 0, N) {
    const double r0 = rand_canonic();
    const double r1 = rand_canonic();
    double lambda = 0;
    double pdf = 0;
    double w = 0;

    lambda = sbb_ran_planck_sample(planck, r0, r1, &pdf);
    w = sbb_planck_monochromatic(lambda, T0) / pdf;
    sum += w;
    sum2 += w*w;
  }

  E = sum / (double)N;
  V = MMAX(sum2 / (double)N - E*E, 0);
  SE = sqrt(V/(double)N);

  ref = sbb_planck(lambda_lo, lambda_hi, T0) * delta_lambda;

  printf("planck(%g m, %g m, %g K) = %g ~ %g +/- %g [W/m^2/sr] (Tref = %g K)\n",
    lambda_lo, lambda_hi, T0, ref, E, SE, Tref);
  CHK(eq_eps(ref, E, 3*SE));

  CHK(sbb_ran_planck_ref_put(planck) == RES_OK);
}

int
main(int argc, char** argv)
{
  /* Input parameters */
  const double lambda_lo = 3.6e-6; /* [m] */
  const double lambda_hi = 12.3e-6; /* [m] */
  const double delta_lambda = lambda_hi - lambda_lo;
  size_t nbands = 0;
  (void)argc, (void)argv;

  check_planck();

  nbands = (size_t)(delta_lambda * 1e9);

  planck_integration(lambda_lo, lambda_hi, 500, 550, 0);
  planck_integration(lambda_lo, lambda_hi, 500, 550, nbands);
  planck_integration(lambda_lo, lambda_hi, 300, 550, 0);
  planck_integration(lambda_lo, lambda_hi, 300, 550, nbands);
  planck_integration(lambda_lo, lambda_hi, 300, 550, nbands/4);
  return 0;
}
