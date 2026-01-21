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

#if !defined(SIMD_WIDTH__)
  #error "Undefined SIMD_WIDTH__ macro"
#endif
#if SIMD_WIDTH__ != 4 && SIMD_WIDTH__ != 8
  #error "Unexpected SIMD_WIDTH__ value of "STR(RSIMD_WIDTH__)
#endif

/* Check that internal macros are not already defined */
#if defined(vXf__)                                                             \
 || defined(vXf_T__)                                                           \
 || defined(SIMD_FUNC__)
 #error "Unexpected macro definition"
#endif

/* Macros generic to the SIMD_WIDTH__ */
#define vXf_T CONCAT(CONCAT(v, SIMD_WIDTH__), f_T)
#define vXf(Func) CONCAT(CONCAT(CONCAT(v, SIMD_WIDTH__), f_), Func)
#define SIMD_FUNC(Func) CONCAT(CONCAT(Func, _simd), SIMD_WIDTH__)

static INLINE vXf_T
SIMD_FUNC(eval_f)(struct rdgfa* rdgfa, const vXf_T theta)
{
  /* Input arguments */
  const vXf_T Df = vXf(set1)((float)rdgfa->fractal_dimension);

  /* Precompute constants */
  const vXf_T Rg2 = vXf(set1)((float)rdgfa->Rg2);
  const vXf_T half_theta = vXf(mul)(theta, vXf(set1)(0.5f));

  /* Precompute values */
  const vXf_T sin_half_theta = vXf(sin)(half_theta);
  const vXf_T q = vXf(mul)(vXf(set1)((float)rdgfa->cst_4pi_div_lambda), sin_half_theta);
  const vXf_T q2Rg2 = vXf(mul)(vXf(mul)(q, q), Rg2);

  /* Evaluate f(theta) when q2Rg2 < 1.5*Df */
  const vXf_T val0 = vXf(exp)(vXf(mul)(vXf(set1)(-1.f/3.f), q2Rg2));

  /* Evaluate f(theta) when q2Rg2 >= 1.5*Df */
  const vXf_T tmp0 = vXf(div)(vXf(set1)((float)rdgfa->cst_3Df_div_2E), q2Rg2);
  const vXf_T half_Df = vXf(mul)(Df, vXf(set1)(0.5f));
  const vXf_T val1 = vXf(pow)(tmp0, half_Df);

  /* Setup f */
  const vXf_T mask = vXf(lt)(q2Rg2, vXf(mul)(Df, vXf(set1)(1.5f)));
  const vXf_T f = vXf(sel)(val1, val0, mask);
  return f;
}

static INLINE vXf_T
SIMD_FUNC(eval2)
  (struct rdgfa* rdgfa,
   const vXf_T theta,
   const vXf_T cos_theta)
{
  const vXf_T f = SIMD_FUNC(eval_f)(rdgfa, theta);
  const vXf_T g = vXf(set1)((float)rdgfa->g);
  const vXf_T cos2_theta = vXf(mul)(cos_theta, cos_theta);
  const vXf_T cst0 = vXf(set1)(3.f/(16.f*(float)PI));
  const vXf_T tmp0 = vXf(div)(f, g);
  const vXf_T tmp1 = vXf(add)(vXf(set1)(1), cos2_theta);
  const vXf_T phase = vXf(mul)(vXf(mul)(cst0, tmp0), tmp1);
  return phase;
}

static INLINE vXf_T
SIMD_FUNC(eval)(struct rdgfa* rdgfa, const vXf_T theta)
{
  return SIMD_FUNC(eval2)(rdgfa, theta, vXf(cos)(theta));
}

static INLINE res_T
SIMD_FUNC(compute_cumulative)(struct rdgfa* rdgfa)
{
  vXf_T dtheta;
  vXf_T theta1;
  vXf_T step;
  vXf_T two_PI;
  float* f_list = NULL;
  float* d_omega_list = NULL;
  double* cdf = NULL;
  size_t nangles;
  size_t i;
  res_T res = RES_OK;
  ASSERT(rdgfa);

  /* Force the number of angles to be a multiple of the SIMD width */
  nangles = rdgfa->nintervals + 1;
  nangles = (nangles + SIMD_WIDTH__-1)/ SIMD_WIDTH__ * SIMD_WIDTH__;

  /* Allocate the cumulative array */
  res = darray_double_resize(&rdgfa->cdf, rdgfa->nintervals);
  if(res != RES_OK) goto error;

  /* Allocate temporaries arrays */
  res = darray_simdf_resize(&rdgfa->f_list, nangles);
  if(res != RES_OK) goto error;
  res = darray_simdf_resize(&rdgfa->d_omega_list, nangles);
  if(res != RES_OK) goto error;

  /* Fetch data */
  cdf = darray_double_data_get(&rdgfa->cdf);
  f_list = darray_simdf_data_get(&rdgfa->f_list);
  d_omega_list = darray_simdf_data_get(&rdgfa->d_omega_list);

  /* Compute the angular step for the angular domain */
  rdgfa->dtheta = PI / (double)rdgfa->nintervals;

  step = vXf(set1)((float)rdgfa->dtheta*(float)SIMD_WIDTH__);
  dtheta = vXf(set1)((float)rdgfa->dtheta);
  two_PI = vXf(set1)((float)(2*PI));
#if SIMD_WIDTH__ == 4
  theta1 = vXf(mul)(dtheta, vXf(set)(0, 1, 2, 3));
#elif SIMD_WIDTH__ == 8
  theta1 = vXf(mul)(dtheta, vXf(set)(0, 1, 2, 3, 4, 5, 6, 7));
#endif

  /* Compute f and d_omaga */
  FOR_EACH(i, 0, nangles/SIMD_WIDTH__) {
    /* Compute f */
    const vXf_T f = SIMD_FUNC(eval)(rdgfa, theta1);

    /* Compute d_omega */
    const vXf_T theta2 = vXf(add)(theta1, dtheta);
    const vXf_T tmp0 = vXf(mul)(vXf(add)(theta1, theta2), vXf(set1)(0.5f));
    const vXf_T d_omega = vXf(mul)(vXf(mul)(two_PI, vXf(sin)(tmp0)), dtheta);

    /* Store the result */
    vXf(store)(&f_list[i*SIMD_WIDTH__], f);
    vXf(store)(&d_omega_list[i*SIMD_WIDTH__], d_omega);

    /* Go to the next angles */
    theta1 = vXf(add)(theta1, step);
  }

  /* Compute the (unormalized) cumulative */
  FOR_EACH(i, 0, rdgfa->nintervals) {
    const double f1 = f_list[i+0];
    const double f2 = f_list[i+1];
    const double d_omega = d_omega_list[i];
    const double tmp = (f1 + f2) * 0.5 * d_omega;
    cdf[i] = (i == 0 ? tmp : tmp + cdf[i-1]);
  }

  /* Save the normalization factor */
  rdgfa->rcp_normalize_factor = 1.0 / cdf[rdgfa->nintervals-1];

  /* Finally normalize the CDF */
  FOR_EACH(i, 0, rdgfa->nintervals) {
    cdf[i] *= rdgfa->rcp_normalize_factor;
  }

exit:
  return res;
error:
  darray_double_clear(&rdgfa->cdf);
  darray_simdf_clear(&rdgfa->f_list);
  darray_simdf_clear(&rdgfa->d_omega_list);
  goto exit;
}

/* Undef generic macros */
#undef vXf_T
#undef vXf
#undef SIMD_FUNC

/* Undef parameter */
#undef SIMD_WIDTH__

