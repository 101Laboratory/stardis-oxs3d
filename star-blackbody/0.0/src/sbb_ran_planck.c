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

#define _POSIX_C_SOURCE 200112L /* nextafter */

#include "sbb.h"
#include "sbb_c.h"

#include <rsys/algorithm.h>
#include <rsys/cstr.h>
#include <rsys/dynamic_array_double.h>
#include <rsys/logger.h>
#include<rsys/mem_allocator.h>
#include <rsys/ref_count.h>

struct sbb_ran_planck {
  struct darray_double pdf;
  struct darray_double cdf;
  double range[2]; /* Boundaries of the spectral integration interval [m] */
  double band_len; /* Length of a band [m] */
  double ref_temperature; /* Reference temperature [K] */
  size_t nbands; /* #bands */

  int verbose; /* Verbosity level */
  struct mem_allocator* allocator;
  struct logger* logger;
  ref_T ref;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
check_ranst_planck_create_args(struct sbb_ran_planck_create_args* args)
{
  struct logger* logger = NULL;

  if(!args) return RES_BAD_ARG;

  logger = args->logger ? args->logger : LOGGER_DEFAULT;

  /* Invalid spectral range */
  if(args->range[0] < 0
  || args->range[1] < 0
  || args->range[0] > args->range[1]) {
    if(args->verbose) {
      logger_print(logger, LOG_ERROR, MSG_ERROR_PREFIX
        "Invalid spectral range for the Planck distribution: [%g,%g] m\n",
        args->range[0], args->range[1]);
    }
    return RES_BAD_ARG;
  }

  /* Invalid reference temperature */
  if(args->ref_temperature < 0) {
    if(args->verbose) {
      logger_print(logger, LOG_ERROR, MSG_ERROR_PREFIX
        "Invalid reference temperature for the Planck distribution: %g K\n",
        args->ref_temperature);
    }
    return RES_BAD_ARG;
  }

  return RES_OK;
}

static res_T
setup_cdf(struct sbb_ran_planck* planck)
{
  double* pdf = NULL;
  double* cdf = NULL;
  double sum = 0;
  size_t i;
  res_T res = RES_OK;
  ASSERT(planck && planck->nbands != 0);

  res = darray_double_resize(&planck->cdf, planck->nbands);
  if(res != RES_OK) {
    logger_print(planck->logger, LOG_ERROR, MSG_ERROR_PREFIX
      "Error when allocating the CDF of the planck distribution -- %s\n",
      res_to_cstr(res));
    goto error;
  }

  res = darray_double_resize(&planck->pdf, planck->nbands);
  if(res != RES_OK) {
    logger_print(planck->logger, LOG_ERROR, MSG_ERROR_PREFIX
      "Error when allocating the PDF of the planck distribution -- %s\n",
      res_to_cstr(res));
    goto error;
  }

  cdf = darray_double_data_get(&planck->cdf);
  pdf = darray_double_data_get(&planck->pdf);

  /* Compute the *unnormalized* probability to sample a small band */
  FOR_EACH(i, 0, planck->nbands) {
    double lo = planck->range[0] + (double)i * planck->band_len;
    double hi = MMIN(lo + planck->band_len, planck->range[1]);
    ASSERT(lo <= hi);
    ASSERT(lo > planck->range[0] || eq_eps(lo, planck->range[0], 1.e-6));
    ASSERT(lo < planck->range[1] || eq_eps(lo, planck->range[1], 1.e-6));

    /* Compute the probability of the current band */
    pdf[i] = sbb_blackbody_fraction(lo, hi, planck->ref_temperature);

    /* Update the norm */
    sum += pdf[i];
  }

  /* Compute the cumulative of the previously computed probabilities */
  FOR_EACH(i, 0, planck->nbands) {
    /* Normalize the probability */
    pdf[i] /= sum;

    /* Setup the cumulative */
    if(i == 0) {
      cdf[i] = pdf[i];
    } else {
      cdf[i] = pdf[i] + cdf[i-1];
      ASSERT(cdf[i] >= cdf[i-1]);
    }
  }
  ASSERT(eq_eps(cdf[planck->nbands-1], 1, 1.e-6));
  cdf[planck->nbands - 1] = 1.0; /* Handle numerical issue */

exit:
  return res;
error:
  darray_double_clear(&planck->cdf);
  darray_double_clear(&planck->pdf);
  goto exit;
}

static double
sample_continue
  (const struct sbb_ran_planck* planck,
   const double r, /* In [0, 1[ */
   const double range[2], /* [m]*/
   double* pdf) /* May be NULL */
{
  /* Numerical parameters of the dichotomy search */
  const size_t MAX_ITER = 100;
  const double EPSILON_LAMBDA_M = 1e-15;
  const double EPSILON_BF = 1e-6;

  /* Local variables */
  double bf = 0;
  double bf_prev = 0;
  double bf_min_max = 0;
  double lambda = 0;
  double lambda_prev = 0;
  double lambda_min = 0;
  double lambda_max = 0;
  size_t i;

  /* Check precondition */
  ASSERT(planck && range);
  ASSERT(range[0] < range[1] && range[0] >= 0 && range[1] >= 0);
  ASSERT(0 <= r && r < 1);

  /* Setup the dichotomy search */
  lambda_min = range[0];
  lambda_max = range[1];
  bf_min_max = sbb_blackbody_fraction
    (range[0], range[1], planck->ref_temperature);

  /* Numerically search the lambda corresponding to the submitted canonical
   * number */
  FOR_EACH(i, 0, MAX_ITER) {
    double r_test;
    lambda = (lambda_min + lambda_max) * 0.5;
    bf = sbb_blackbody_fraction
      (range[0], lambda, planck->ref_temperature);

    r_test = bf / bf_min_max;
    if(r_test < r) {
      lambda_min = lambda;
    } else {
      lambda_max = lambda;
    }

    if(fabs(lambda_prev - lambda) < EPSILON_LAMBDA_M
    || fabs(bf_prev - bf) < EPSILON_BF)
      break;

    lambda_prev = lambda;
    bf_prev = bf;
  }
  if(i >= MAX_ITER && planck->verbose) {
    logger_print(planck->logger, LOG_WARNING, MSG_WARNING_PREFIX
      "Could not sample a wavelength wrt the Planck distribution "
      "(spectral range = [%g, %g] m; reference temperature = %g K)\n",
      range[0], range[1], planck->ref_temperature);
  }

  if(pdf) {
    const double Tref = planck->ref_temperature; /* K */

    /* W/m²/sr/m */
    const double B_lambda = sbb_planck(lambda, lambda, Tref);
    const double B_mean = sbb_planck(range[0], range[1], Tref);

    *pdf = B_lambda / (B_mean * (range[1]-range[0]));
  }

  return lambda;
}

static FINLINE int
cmp_dbl(const void* a, const void* b)
{
  const double d0 = *((const double*)a);
  const double d1 = *((const double*)b);
  return d0 < d1 ? -1 : (d0 > d1 ? 1 : 0);
}

static double
sample_discrete
  (const struct sbb_ran_planck* planck,
   const double r0, /* In [0, 1[ */
   const double r1, /* In [0, 1[ */
   double* pdf) /* May be NULL */
{
  const double* cdf = NULL;
  const double* find = NULL;
  double r0_next = nextafter(r0, DBL_MAX);
  double band_range[2];
  double lambda = 0;
  double pdf_continue = 0;
  double pdf_band = 0;
  size_t cdf_length = 0;
  size_t i;
  ASSERT(planck && planck->nbands != 0);
  ASSERT(0 <= r0 && r0 < 1);
  ASSERT(0 <= r1 && r1 < 1);
  (void)pdf_band;

  cdf = darray_double_cdata_get(&planck->cdf);
  cdf_length = darray_double_size_get(&planck->cdf);
  ASSERT(cdf_length > 0);

  /* Use r_next rather than r0 in order to find the first entry that is not less
   * than *or equal* to r0 */
  find = search_lower_bound(&r0_next, cdf, cdf_length, sizeof(double), cmp_dbl);
  ASSERT(find);

  i = (size_t)(find - cdf);
  ASSERT(i < cdf_length && cdf[i] > r0 && (!i || cdf[i-1] <= r0));

  band_range[0] = planck->range[0] + (double)i*planck->band_len;
  band_range[1] = band_range[0] + planck->band_len;

  /* Fetch the pdf of the sampled band */
  pdf_band = darray_double_cdata_get(&planck->pdf)[i];

  /* Uniformly sample a wavelength in the sampled band */
  lambda = band_range[0] + (band_range[1] - band_range[0]) * r1;
  pdf_continue = 1.0 / (band_range[1] - band_range[0]);

  if(pdf) {
    *pdf = pdf_band * pdf_continue;
  }

  return lambda;
}

static void
release_planck(ref_T* ref)
{
  struct sbb_ran_planck* planck = CONTAINER_OF(ref, struct sbb_ran_planck, ref);
  ASSERT(ref);
  darray_double_release(&planck->cdf);
  darray_double_release(&planck->pdf);
  MEM_RM(planck->allocator, planck);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sbb_ran_planck_create
  (struct sbb_ran_planck_create_args* args,
   struct sbb_ran_planck** out_planck)
{
  struct sbb_ran_planck* planck = NULL;
  struct mem_allocator* allocator = NULL;
  struct logger* logger = NULL;
  res_T res = RES_OK;

  if(!out_planck) return RES_BAD_ARG;
  res = check_ranst_planck_create_args(args);
  if(res != RES_OK) goto error;

  allocator = args->allocator ? args->allocator : &mem_default_allocator;
  logger = args->logger ? args->logger : LOGGER_DEFAULT;

  planck = MEM_CALLOC(allocator, 1, sizeof(*planck));
  if(!planck) {
    res = RES_MEM_ERR;
    if(args->verbose) {
      logger_print(logger, LOG_ERROR,
        "Planck distribution allocation error -- %s\n",
        res_to_cstr(res));
    }
    goto error;
  }
  planck->allocator = allocator;
  planck->logger = logger;
  ref_init(&planck->ref);
  darray_double_init(planck->allocator, &planck->cdf);
  darray_double_init(planck->allocator, &planck->pdf);
  planck->range[0] = args->range[0];
  planck->range[1] = args->range[1];
  planck->ref_temperature = args->ref_temperature;
  planck->nbands = args->nbands;
  planck->verbose = args->verbose;

  if(planck->nbands == 0) {
    planck->band_len = 0;
  } else {
    planck->band_len =
      (planck->range[1] - planck->range[0])
    / (double)planck->nbands;

    res = setup_cdf(planck);
    if(res != RES_OK) goto error;
  }

exit:
  if(out_planck) *out_planck = planck;
  return res;
error:
  if(planck) { SBB(ran_planck_ref_put(planck)); planck = NULL; }
  goto exit;
}

res_T
sbb_ran_planck_ref_get(struct sbb_ran_planck* planck)
{
  if(!planck) return RES_BAD_ARG;
  ref_get(&planck->ref);
  return RES_OK;
}

res_T
sbb_ran_planck_ref_put(struct sbb_ran_planck* planck)
{
  if(!planck) return RES_BAD_ARG;
  ref_put(&planck->ref, release_planck);
  return RES_OK;
}

double /* Wavelength [m] */
sbb_ran_planck_sample
  (struct sbb_ran_planck* planck,
   const double r0, /* Random number in [0, 1[ */
   const double r1, /* Random number in [0, 1[ */
   double* pdf) /* [m^-1]. May be NULL */
{
  ASSERT(planck && r0 >= 0 && r1 >= 0 && r0 < 1 && r1 < 1);

  if(eq_eps(planck->range[0], planck->range[1], 1.e-6)) {
    if(pdf) *pdf = 1;
    return planck->range[0];

  } else if(planck->nbands == 0) {
    return sample_continue(planck, r0, planck->range, pdf);

  } else { /* Speed up wavelength sampling by presampling a band */
    return sample_discrete(planck, r0, r1, pdf);
  }
}
