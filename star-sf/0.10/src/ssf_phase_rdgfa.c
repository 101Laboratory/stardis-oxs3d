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
#include "ssf_phase_c.h"

#include <star/ssp.h>

#include <rsys/algorithm.h>
#include <rsys/dynamic_array_double.h>

#if defined(SSF_USE_SIMD_128) || defined(SSF_USE_SIMD_256)
  /* Helper macro */
  #define USE_SIMD
#endif

#ifdef USE_SIMD
  #include <rsimd/math.h>
  #include <rsimd/rsimd.h>

  /* Generate the struct darray_simdf dynamic array */
  #define DARRAY_NAME simdf
  #define DARRAY_DATA float
  #ifdef SSF_USE_SIMD_256
    #define DARRAY_ALIGNMENT 64
  #else
    #define DARRAY_ALIGNMENT 16
  #endif
  dynamic_array.h>
#endif

#define EXP1 2.7182818284590452354

struct rdgfa {
  double wavelength; /* In nm */
  double fractal_dimension; /* No  unit */
  double gyration_radius; /* In nm */

  double rcp_normalize_factor; /* Reciprocal normalization factor of the CDF */

  /* Discretized cumulative (#entries = nintervals) */
  struct darray_double cdf;

#ifdef USE_SIMD
  struct darray_simdf f_list;
  struct darray_simdf d_omega_list;
#endif

  /* Precomputed values */
  double Rg2; /* gyration_radius^2 */
  double cst_4pi_div_lambda; /* (4*PI)/wavelength */
  double cst_3Df_div_2E; /* 3*Df/(2*exp(1)) */
  double g; /* g function */

  /* #intervals used to discretize the [0,pi[ angular domain */
  size_t nintervals;

  /* Length of an angular interval */
  double dtheta; /* In rad */
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
check_phase_rdgfa_setup_args(const struct ssf_phase_rdgfa_setup_args* args)
{
  return args
      && args->wavelength > 0
      && args->fractal_dimension > 0
      && args->gyration_radius > 0
      && args->nintervals != 0;
}

static INLINE int
cmp_dbl(const void* a, const void* b)
{
  const double key = *((double*)a);
  const double val = *((double*)b);
  if(key < val) {
    return -1;
  } else if(key > val) {
    return 1;
  } else {
    return 0;
  }
}

static INLINE double
eval_f(struct rdgfa* rdgfa, const double theta)
{
  double Df, Rg2, q, q2Rg2, f;
  ASSERT(rdgfa);

  /* Input arguments */
  Df = rdgfa->fractal_dimension;

  /* Fech precomputed constants */
  Rg2 = rdgfa->Rg2;

  /* Precompute values */
  q = rdgfa->cst_4pi_div_lambda * sin(theta*0.5);
  q2Rg2 = q*q*Rg2;

  /* Evaluate f(theta) */
  f = q2Rg2 < 1.5*Df
    ? exp(-1.0/3.0 * q2Rg2)
    : pow(rdgfa->cst_3Df_div_2E * 1/q2Rg2, Df*0.5);
  return f;
}

/* Not normalized */
static INLINE double
eval2
  (struct rdgfa* rdgfa,
   const double theta,
   const double cos_theta)
{
  double f, cos2_theta, phase;
  ASSERT(rdgfa && eq_eps(cos_theta, cos(theta), 1.e-6));

  /* Precompute values */
  cos2_theta = cos_theta * cos_theta;

  /* Evaluate phase(theta) */
  f = eval_f(rdgfa, theta);
  phase = 3.0/(16*PI) * f / rdgfa->g * (1 + cos2_theta);
  return phase;
}

/* Not normalized */
static FINLINE double
eval(struct rdgfa* rdgfa, const double theta)
{
  return eval2(rdgfa, theta, cos(theta));
}

static INLINE res_T
compute_cumulative(struct rdgfa* rdgfa)
{
  double* cdf = NULL;
  double f1;
  double theta1;
  size_t i;
  res_T res = RES_OK;
  ASSERT(rdgfa);

  /* Allocate the cumulative array */
  res = darray_double_resize(&rdgfa->cdf, rdgfa->nintervals);
  if(res != RES_OK) goto error;
  cdf = darray_double_data_get(&rdgfa->cdf);

  /* Compute the angular step for the angular domain */
  rdgfa->dtheta = PI / (double)rdgfa->nintervals;

  theta1 = 0;
  f1 = eval(rdgfa, 0);
  FOR_EACH(i, 0, rdgfa->nintervals) {
    /* Compute the upper bound of the current angular range */
    const double theta2 = theta1 + rdgfa->dtheta;

    /* Compute the (unormalized) cumulative for current interval */
    const double delta_omega = 2*PI*sin((theta1+theta2)*0.5)*rdgfa->dtheta;
    const double f2 = eval(rdgfa, theta2);
    const double tmp = (f1 + f2) * 0.5 * delta_omega;
    cdf[i] = (i == 0 ? tmp : tmp + cdf[i-1]);

    /* Go to the next interval */
    f1 = f2;
    theta1 = theta2;
  }

  /* Save the normamlization factor */
  rdgfa->rcp_normalize_factor = 1.0 / cdf[rdgfa->nintervals-1];

  /* Finally normalize the CDF */
  FOR_EACH(i, 0, rdgfa->nintervals) {
    cdf[i] *= rdgfa->rcp_normalize_factor;
  }

exit:
  return res;
error:
  darray_double_clear(&rdgfa->cdf);
  goto exit;
}

/* Generate the simd functions if required */
#ifdef SSF_USE_SIMD_128
  #define SIMD_WIDTH__ 4
  #include "ssf_phase_rdgfa_simdX.h"
#endif
#ifdef SSF_USE_SIMD_256
  #define SIMD_WIDTH__ 8
  #include "ssf_phase_rdgfa_simdX.h"
#endif

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static res_T
rdgfa_init(struct mem_allocator* allocator, void* phase)
{
  struct rdgfa* rdgfa = phase;
  ASSERT(phase);
  memset(rdgfa, 0, sizeof(*rdgfa));
  darray_double_init(allocator, &rdgfa->cdf);
#ifdef USE_SIMD
  darray_simdf_init(allocator, &rdgfa->f_list);
  darray_simdf_init(allocator, &rdgfa->d_omega_list);
#endif /* USE_SIMD */
  return RES_OK;
}

static void
rdgfa_release(void* phase)
{
  struct rdgfa* rdgfa = phase;
  ASSERT(phase);
  darray_double_release(&rdgfa->cdf);
#ifdef USE_SIMD
  darray_simdf_release(&rdgfa->f_list);
  darray_simdf_release(&rdgfa->d_omega_list);
#endif /* USE_SIMD */
}

static double
rdgfa_eval(void* data, const double wo[3], const double wi[3])
{
  const struct rdgfa* rdgfa = data;
  double cos_theta, theta;
  double v[3];
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(wi));

  d3_minus(v, wo);
  cos_theta = d3_dot(v, wi);
  theta = acos(cos_theta);
  return eval2(data, theta, cos_theta) * rdgfa->rcp_normalize_factor;
}

static void
rdgfa_sample
  (void* data,
   struct ssp_rng* rng,
   const double wo[3],
   double wi[3],
   double* pdf)
{
  struct rdgfa* rdgfa = data;
  const double* cdf = NULL;
  double* find = NULL;
  double frame[9];
  double w[3];
  double thetas[2];
  double theta;
  double sin_theta;
  double cos_theta;
  double phi;
  double r;
  double u;
  size_t n;
  size_t i;
  ASSERT(data && rng && wo && wi);

  /* Fetch the CDF array and its number of entries */
  cdf = darray_double_cdata_get(&rdgfa->cdf);
  n = darray_double_size_get(&rdgfa->cdf);

  /* Sample the CDF */
  r = ssp_rng_canonical(rng);
  find = search_lower_bound(&r, cdf, n, sizeof(double), cmp_dbl);
  ASSERT(find && (*find) >= r);
  i = (size_t)(find - cdf);

  /* Compute the angular range into which the sample lies */
  thetas[0] = rdgfa->dtheta * (double)(i + 0);
  thetas[1] = rdgfa->dtheta * (double)(i + 1);

  /* Compute the sampled theta angle by linearly interpolate it from the
   * boundaries of its interval */
  if(i == 0) {
    u = r / cdf[0];
  } else {
    u = (r - cdf[i-1]) /  (cdf[i] - cdf[i-1]);
  }
  ASSERT(0 <= u && u < 1);
  theta = u * (thetas[1] - thetas[0]) + thetas[0];

  /* Uniformly sample a phi angle in [0, 2PI[ */
  phi = ssp_rng_uniform_double(rng, 0, 2*PI);
  sin_theta = sin(theta);
  cos_theta = cos(theta);

  /* Compute the cartesian coordinates of the sampled direction in the _local_
   * phase function space */
  wi[0] = cos(phi) * sin_theta;
  wi[1] = sin(phi) * sin_theta;
  wi[2] = cos_theta;

  /* Compute the transformation matrix from local phase function to world
   * space. Note that by convention, in Star-SF the directions point outward
   * the scattering position. Revert 'wo' to match the convention used by the
   * previous computations */
  d33_basis(frame, d3_minus(w, wo));
  d33_muld3(wi, frame, wi);

  ASSERT(eq_eps(d3_dot(wi, w), cos_theta, fabs(cos_theta*1.e-6)));

  if(pdf) *pdf = rdgfa_eval(rdgfa, wo, wi);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
const struct ssf_phase_type ssf_phase_rdgfa = {
  rdgfa_init,
  rdgfa_release,
  rdgfa_sample,
  rdgfa_eval,
  rdgfa_eval,
  sizeof(struct rdgfa),
  ALIGNOF(struct rdgfa)
};

res_T
ssf_phase_rdgfa_setup
  (struct ssf_phase* phase,
   const struct ssf_phase_rdgfa_setup_args* args)
{
  struct rdgfa* rdgfa = NULL;
  double lambda, Df, Rg, k, k2;
  res_T res = RES_OK;

  if(!phase
  || !PHASE_TYPE_EQ(&phase->type, &ssf_phase_rdgfa)
  || !check_phase_rdgfa_setup_args(args)) {
    res = RES_BAD_ARG;
    goto error;
  }

  rdgfa = phase->data;
  rdgfa->wavelength = args->wavelength;
  rdgfa->fractal_dimension = args->fractal_dimension;
  rdgfa->gyration_radius = args->gyration_radius;
  rdgfa->nintervals = args->nintervals;

  /* Fetch input data */
  lambda = rdgfa->wavelength;
  Df = rdgfa->fractal_dimension;
  Rg = rdgfa->gyration_radius;

  /* Precompute constants */
  rdgfa->Rg2 = Rg*Rg; /* gyration_radius^2 */
  rdgfa->cst_4pi_div_lambda = 4*PI / lambda;
  rdgfa->cst_3Df_div_2E = 3*Df/(2.0*EXP1);

  /* Precompute the function g */
  k = (2.0*PI) / lambda;
  k2 = k*k;
  rdgfa->g = pow(1 + 4*k2*rdgfa->Rg2/(3*Df), -Df*0.5);

  /* Precompute the phase function cumulative */
  switch(args->simd) {
    case SSF_SIMD_NONE:
      res = compute_cumulative(rdgfa);
      if(res != RES_OK) goto error;
      break;
    case SSF_SIMD_128:
    #ifdef SSF_USE_SIMD_128
      res = compute_cumulative_simd4(rdgfa);
      if(res != RES_OK) goto error;
    #else
      res = RES_BAD_OP;
      goto error;
    #endif
      break;
    case SSF_SIMD_256:
    #ifdef SSF_USE_SIMD_256
      res = compute_cumulative_simd8(rdgfa);
      if(res != RES_OK) goto error;
    #else
      res = RES_BAD_OP;
      goto error;
    #endif
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
ssf_phase_rdgfa_get_desc
  (const struct ssf_phase* phase,
   struct ssf_phase_rdgfa_desc* desc)
{
  struct rdgfa* rdgfa = NULL;
  res_T res = RES_OK;

  if(!phase || !PHASE_TYPE_EQ(&phase->type, &ssf_phase_rdgfa) || !desc) {
    res = RES_BAD_ARG;
    goto error;
  }

  rdgfa = phase->data;
  desc->wavelength = rdgfa->wavelength;
  desc->gyration_radius = rdgfa->gyration_radius;
  desc->fractal_dimension = rdgfa->fractal_dimension;
  desc->normalization_factor = 1.0/rdgfa->rcp_normalize_factor;
  desc->nintervals = rdgfa->nintervals;

exit:
  return res;
error:
  goto exit;
}

res_T
ssf_phase_rdgfa_get_interval
  (const struct ssf_phase* phase,
   const size_t interval_id, /* In [0, #intervals[ */
   struct ssf_phase_rdgfa_interval* interval)
{
  struct rdgfa* rdgfa = NULL;
  res_T res = RES_OK;

  if(!phase || !PHASE_TYPE_EQ(&phase->type, &ssf_phase_rdgfa) || !interval) {
    res = RES_BAD_ARG;
    goto error;
  }

  rdgfa = phase->data;
  if(interval_id >= darray_double_size_get(&rdgfa->cdf)) {
    res = RES_BAD_ARG;
    goto error;
  }

  interval->range[0] = rdgfa->dtheta * (double)(interval_id + 0);
  interval->range[1] = rdgfa->dtheta * (double)(interval_id + 1);
  interval->cumulative = darray_double_cdata_get(&rdgfa->cdf)[interval_id];

exit:
  return res;
error:
  goto exit;
}

