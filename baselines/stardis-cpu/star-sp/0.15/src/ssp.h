/* Copyright (C) 2015-2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef SSP_H
#define SSP_H

#include <rsys/double33.h>
#include <rsys/float33.h>
#include <rsys/rsys_math.h>
#include <rsys/rsys.h>

#include <stdint.h> /* SIZE_MAX */

/* Library symbol management */
#if defined(SSP_SHARED_BUILD) /* Build shared library */
  #define SSP_API extern EXPORT_SYM
#elif defined(SSP_STATIC) /* Use/build static library */
  #define SSP_API extern LOCAL_SYM
#else /* Use shared library */
  #define SSP_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the ssp function `Func'
 * returns an error. One should use this macro on smc function calls for which
 * no explicit error checking is performed */
#ifndef NDEBUG
  #define SSP(Func) ASSERT(ssp_ ## Func == RES_OK)
#else
  #define SSP(Func) ssp_ ## Func
#endif

#define SSP_SEQUENCE_ID_NONE SIZE_MAX

/* Forward declaration of opaque types */
struct ssp_rng;
struct ssp_rng_proxy;
struct ssp_ranst_discrete;
struct ssp_ranst_gaussian;
struct ssp_ranst_gaussian_float;
struct ssp_ranst_piecewise_linear;
struct ssp_ranst_piecewise_linear_float;

/* Forward declaration of external types */
struct mem_allocator;

enum ssp_rng_type {
  /* David Jones's Keep It Simple Stupid builtin PRNG type. Suitable for fast
   * basic randomness */
  SSP_RNG_KISS,
  /* 64-bits Mersenne Twister builtin PRNG type of Matsumoto and Nishimura */
  SSP_RNG_MT19937_64,
  /* 48-bits RANLUX builtin PRNG type of Lusher and James, 1994 */
  SSP_RNG_RANLUX48,
  /* A random_device RNG is a uniformly-distributed integer random number
   * generator that produces non-deterministic random numbers. It may be
   * implemented in terms of an implementation-defined pseudo-random number
   * engine if a non-deterministic source (e.g. a hardware device) is not
   * available to the implementation. In this case each random_device object
   * may generate the same number sequence. */
  SSP_RNG_RANDOM_DEVICE,
  /* Counter Based RNG threefry of Salmon, Moraes, Dror & Shaw */
  SSP_RNG_THREEFRY,
  /* Counter Based RNG aes of Salmon, Moraes, Dror & Shaw */
  SSP_RNG_AES,
  SSP_RNG_TYPES_COUNT__,
  SSP_RNG_TYPE_NULL = SSP_RNG_TYPES_COUNT__
};

/* Arguments of the ssp_rng_proxy_create2 function */
struct ssp_rng_proxy_create2_args {
  const struct ssp_rng* rng; /* Original RNG state. May be NULL*/
  enum ssp_rng_type type; /* Type of the RN if 'rng' is NULL */
  size_t sequence_offset; /* #RNs before the 1st valid sequence */
  size_t sequence_size; /* #RNs in a sequence */
  size_t sequence_pitch; /* #RNs between sequences. Must be >= sequence_size */
  size_t nbuckets; /* #buckets of continuous RNs in a sequence */
};
#define SSP_RNG_PROXY_CREATE2_ARGS_NULL__ {NULL, SSP_RNG_TYPE_NULL, 0, 0, 0, 0}
static const struct ssp_rng_proxy_create2_args SSP_RNG_PROXY_CREATE2_ARGS_NULL =
  SSP_RNG_PROXY_CREATE2_ARGS_NULL__;

BEGIN_DECLS

/*******************************************************************************
 * Random Number Generator API
 ******************************************************************************/
SSP_API res_T
ssp_rng_create
  (struct mem_allocator* allocator, /* May be NULL <=> Use default allocator */
   const enum ssp_rng_type type,
   struct ssp_rng** rng);

SSP_API res_T
ssp_rng_ref_put
  (struct ssp_rng* rng);

SSP_API res_T
ssp_rng_ref_get
  (struct ssp_rng* rng);

SSP_API res_T
ssp_rng_get_type
  (const struct ssp_rng* rng,
   enum ssp_rng_type* type);

SSP_API res_T
ssp_rng_set
  (struct ssp_rng* rng,
   const uint64_t seed);

/* Uniform random distribution in [ssp_rng_min(rng), ssp_rng_max(rng)] */
SSP_API uint64_t
ssp_rng_get
  (struct ssp_rng* rng);

/* Advances the internal state by n times.
   Equivalent to calling ssp_rng_get() n times and discarding the result */
SSP_API res_T
ssp_rng_discard
  (struct ssp_rng* rng, uint64_t n);

/* Uniform random integer distribution in [lower, upper] */
SSP_API uint64_t
ssp_rng_uniform_uint64
  (struct ssp_rng* rng,
   const uint64_t lower,
   const uint64_t upper);

/* Uniform random floating point distribution in [lower, upper) */
SSP_API double
ssp_rng_uniform_double
  (struct ssp_rng* rng,
   const double lower,
   const double upper);

SSP_API float
ssp_rng_uniform_float
  (struct ssp_rng* rng,
   const float lower,
   const float upper);

/* Uniform random floating point distribution in [0, 1) */
SSP_API double
ssp_rng_canonical
  (struct ssp_rng* rng);

/* Uniform random single precision floating point distribution in [0, 1) */
SSP_API float
ssp_rng_canonical_float
  (struct ssp_rng* rng);

SSP_API uint64_t
ssp_rng_min
  (struct ssp_rng* rng);

SSP_API uint64_t
ssp_rng_max
  (struct ssp_rng* rng);

SSP_API res_T
ssp_rng_read
  (struct ssp_rng* rng,
   FILE* stream);

SSP_API res_T
ssp_rng_read_cstr
  (struct ssp_rng* rng,
   const char* cstr); /* Null terminated string */

SSP_API res_T
ssp_rng_write
  (const struct ssp_rng* rng,
   FILE* stream);

SSP_API res_T
ssp_rng_write_cstr
  (const struct ssp_rng* rng,
   char* buf, /* May be NULL */
   const size_t bufsz, /* buf capacity */
   size_t* len); /* May be NULL. #chars to write into buf without null char */

SSP_API double
ssp_rng_entropy
  (const struct ssp_rng* rng);

/*******************************************************************************
 * Proxy of Random Number Generators - It manages a list of `nbuckets' RNGs
 * whose each have independant `infinite' random sequences
 ******************************************************************************/
SSP_API res_T
ssp_rng_proxy_create
  (struct mem_allocator* allocator, /* May be NULL <=> Use default allocator */
   const enum ssp_rng_type type,
   const size_t nbuckets,
   struct ssp_rng_proxy** proxy);

/* Build the proxy RNG from the state of `rng'. Note that `rng' is read only
 * argument, i.e. it is not used internally. */
SSP_API res_T
ssp_rng_proxy_create_from_rng
  (struct mem_allocator* allocator,
   const struct ssp_rng* rng,
   const size_t nbuckets,
   struct ssp_rng_proxy** proxy);

SSP_API res_T
ssp_rng_proxy_create2
  (struct mem_allocator* mem_allocator,
   const struct ssp_rng_proxy_create2_args* args,
   struct ssp_rng_proxy** out_proxy);

SSP_API res_T
ssp_rng_proxy_read
  (struct ssp_rng_proxy* proxy,
   FILE* stream);

SSP_API res_T
ssp_rng_proxy_write
  (const struct ssp_rng_proxy* proxy,
   FILE* stream);

SSP_API res_T
ssp_rng_proxy_ref_get
  (struct ssp_rng_proxy* proxy);

SSP_API res_T
ssp_rng_proxy_ref_put
  (struct ssp_rng_proxy* proxy);

/* Create the RNG of `ibucket'. Return a RES_BAD_ARG error if RNG was already
 * created for `ibucket'. Call ssp_rng_ref_put to release the returned RNG */
SSP_API res_T
ssp_rng_proxy_create_rng
  (struct ssp_rng_proxy* proxy,
   const size_t ibucket,
   struct ssp_rng** rng);

SSP_API res_T
ssp_rng_proxy_get_type
  (const struct ssp_rng_proxy* proxy,
   enum ssp_rng_type* type);

/* Returns the index of the current sequence. This index is independent of the
 * original seed used by the proxy. When creating the proxy, the sequence index
 * is 0. It is then incremented by one each time a new sequence is required.
 * This index is used to identify the state of the proxy relative to the
 * original seed. */
SSP_API res_T
ssp_rng_proxy_get_sequence_id
  (const struct ssp_rng_proxy* proxy,
   size_t* sequence_id);

/* Discard the random numbers of `n' sequences. If `n' is 0, nothing happens.
 * If `n' is greater than or equal to one, the random numbers of the current
 * sequence are ignored as well as the random numbers of the following
 * sequences until the sequence identifier has been incremented by `n-1'. */
SSP_API res_T
ssp_rng_proxy_flush_sequences
  (struct ssp_rng_proxy* proxy,
   const size_t n);

/*******************************************************************************
 * Miscellaneous distributions
 ******************************************************************************/
/* Random variate from the exponential distribution with mean `1/mu':
 * P(x) dx = mu * exp(-x * mu) dx */
SSP_API double
ssp_ran_exp
  (struct ssp_rng* rng,
   const double mu);

SSP_API float
ssp_ran_exp_float
  (struct ssp_rng* rng,
   const float mu);

SSP_API double
ssp_ran_exp_pdf
  (const double x,
   const double mu);

SSP_API float
ssp_ran_exp_float_pdf
  (const float x,
   const float mu);

/* Truncated exponential distribution */
SSP_API double
ssp_ran_exp_truncated
  (struct ssp_rng* rng,
   const double mu,
   const double max);

SSP_API float
ssp_ran_exp_truncated_float
  (struct ssp_rng* rng,
   const float mu,
   const float max);

SSP_API double
ssp_ran_exp_truncated_pdf
  (const double x,
   const double mu,
   const double max);

SSP_API float
ssp_ran_exp_truncated_float_pdf
  (const float x,
   const float mu,
   const float max);

/* Stateless random variate from the gaussian (or normal) distribution
 * with mean `mu':
 * P(x) dx = 1 / (sigma*sqrt(2*PI)) * exp(-1/2*((x-mu)/sigma)^2) dx */
SSP_API double
ssp_ran_gaussian
  (struct ssp_rng* rng,
   const double mu,
   const double sigma);

SSP_API float
ssp_ran_gaussian_float
  (struct ssp_rng* rng,
   const float mu,
   const float sigma);

/* Return the probability distribution for a gaussian random variate */
SSP_API double
ssp_ran_gaussian_pdf
  (const double x,
   const double mu,
   const double sigma);

SSP_API float
ssp_ran_gaussian_float_pdf
  (const float x,
   const float mu,
   const float sigma);

/* Random variate from the lognormal distribution:
 * P(x) dx = 1/(sigma*x*sqrt(2*PI)) * exp(-(ln(x)-zeta)^2 / (2*sigma^2)) dx */
SSP_API double
ssp_ran_lognormal
  (struct ssp_rng* rng,
   const double zeta,
   const double sigma);

SSP_API float
ssp_ran_lognormal_float
  (struct ssp_rng* rng,
   const float zeta,
   const float sigma);

/* Return the probability distribution for a lognormal random variate */
SSP_API double
ssp_ran_lognormal_pdf
  (const double x,
   const double zeta,
   const double sigma);

SSP_API float
ssp_ran_lognormal_float_pdf
  (const float x,
   const float zeta,
   const float sigma);

/*******************************************************************************
 * Sphere distribution
 ******************************************************************************/
/* Uniform sampling of an unit sphere. The PDF of the generated sample is
 * stored in sample[3] */
SSP_API double*
ssp_ran_sphere_uniform
  (struct ssp_rng* rng,
   double sample[3],
   double* pdf); /* Can be NULL => pdf not computed */

SSP_API float*
ssp_ran_sphere_uniform_float
  (struct ssp_rng* rng,
   float sample[3],
   float* pdf); /* Can be NULL => pdf not computed */

/* Return the probability distribution for a sphere uniform random variate */
static INLINE double
ssp_ran_sphere_uniform_pdf(void)
{
  return 1 / (4*PI);
}

static INLINE float
ssp_ran_sphere_uniform_float_pdf(void)
{
  return 1 / (4*(float)PI);
}

/*******************************************************************************
 * Circle distribution
 ******************************************************************************/
SSP_API double*
ssp_ran_circle_uniform
  (struct ssp_rng* rng,
   double sample[2],
   double* pdf); /* Can be NULL => pdf not computed */

SSP_API float*
ssp_ran_circle_uniform_float
  (struct ssp_rng* rng,
   float sample[2],
   float* pdf); /* Can be NULL => pdf not computed */

static INLINE double
ssp_ran_circle_uniform_pdf(void)
{
  return 1/(2*PI);
}

static INLINE float
ssp_ran_circle_uniform_float_pdf(void)
{
  return 1/(2*(float)PI);
}

/*******************************************************************************
 * Triangle distribution
 ******************************************************************************/
/* Uniform sampling of a triangle */
SSP_API double*
ssp_ran_triangle_uniform
  (struct ssp_rng* rng,
   const double v0[3], /* Position of the 1st vertex */
   const double v1[3], /* Position of the 2nd vertex */
   const double v2[3], /* Position of the 3rd vertex */
   double sample[3],   /* Sampled position */
   double* pdf);       /* Can be NULL => pdf not computed */

SSP_API float*
ssp_ran_triangle_uniform_float
  (struct ssp_rng* rng,
   const float v0[3], /* Position of the 1st vertex */
   const float v1[3], /* Position of the 2nd vertex */
   const float v2[3], /* Position of the 3rd vertex */
   float sample[3],   /* Sampled position */
   float* pdf);       /* Can be NULL => pdf not computed */

/* Return the probability distribution for a uniform point sampling on a triangle */
static INLINE double
ssp_ran_triangle_uniform_pdf
  (const double v0[3],
   const double v1[3],
   const double v2[3])
{
  double vec0[3], vec1[3], tmp[3];
  ASSERT(v0 && v1 && v2);

  d3_sub(vec0, v0, v2);
  d3_sub(vec1, v1, v2);
  return 2 / d3_len(d3_cross(tmp, vec0, vec1));
}

static INLINE float
ssp_ran_triangle_uniform_float_pdf
  (const float v0[3],
   const float v1[3],
   const float v2[3])
{
  float vec0[3], vec1[3], tmp[3];
  ASSERT(v0 && v1 && v2);

  f3_sub(vec0, v0, v2);
  f3_sub(vec1, v1, v2);
  return 2 / f3_len(f3_cross(tmp, vec0, vec1));
}

/*******************************************************************************
 * Tetrahedron distribution
 ******************************************************************************/
/* Uniform sampling of a tetrahedron */
SSP_API double*
ssp_ran_tetrahedron_uniform
  (struct ssp_rng* rng,
   const double v0[3], /* Position of the 1st vertex */
   const double v1[3], /* Position of the 2nd vertex */
   const double v2[3], /* Position of the 3rd vertex */
   const double v3[3], /* Position of the 4th vertex */
   double sample[3],   /* Sampled position */
   double* pdf);       /* Can be NULL => pdf not computed */

SSP_API float*
ssp_ran_tetrahedron_uniform_float
  (struct ssp_rng* rng,
   const float v0[3], /* Position of the 1st vertex */
   const float v1[3], /* Position of the 2nd vertex */
   const float v2[3], /* Position of the 3rd vertex */
   const float v3[3], /* Position of the 4th vertex */
   float sample[3],   /* Sampled position */
   float* pdf);       /* Can be NULL => pdf not computed */

/* Return the probability distribution for a uniform point sampling in a tetrahedron */
static INLINE double
ssp_ran_tetrahedron_uniform_pdf
  (const double v0[3],
   const double v1[3],
   const double v2[3],
   const double v3[3])
{
  double vec0[3], vec1[3], vec2[3], tmp[3];
  ASSERT(v0 && v1 && v2 && v3);

  d3_sub(vec0, v1, v2);
  d3_sub(vec1, v3, v2);
  d3_sub(vec2, v0, v2);
  return 6 / fabs(d3_dot(d3_cross(tmp, vec0, vec1), vec2));
}

static INLINE float
ssp_ran_tetrahedron_uniform_float_pdf
  (const float v0[3],
   const float v1[3],
   const float v2[3],
   const float v3[3])
{
  float vec0[3], vec1[3], vec2[3], tmp[3];
  ASSERT(v0 && v1 && v2 && v3);

  f3_sub(vec0, v1, v2);
  f3_sub(vec1, v3, v2);
  f3_sub(vec2, v0, v2);
  return  6.f / absf(f3_dot(f3_cross(tmp, vec0, vec1), vec2));
}

/*******************************************************************************
 * Hemisphere distribution
 ******************************************************************************/
/* Uniform sampling of an unit hemisphere whose up direction is implicitly the
 * Z axis. */
SSP_API double*
ssp_ran_hemisphere_uniform_local
  (struct ssp_rng* rng,
   double sample[3],
   double* pdf); /* Can be NULL => pdf not computed */

SSP_API float*
ssp_ran_hemisphere_uniform_float_local
  (struct ssp_rng* rng,
   float sample[3],
   float* pdf); /* Can be NULL => pdf not computed */

/* Return the probability distribution for an hemispheric uniform random
 * variate with an implicit up direction in Z */
static INLINE double
ssp_ran_hemisphere_uniform_local_pdf(const double sample[3])
{
  ASSERT(sample);
  return sample[2] < 0 ? 0 : 1/(2*PI);
}

static INLINE float
ssp_ran_hemisphere_uniform_float_local_pdf(const float sample[3])
{
  ASSERT(sample);
  return sample[2] < 0 ? 0 : 1/(2*(float)PI);
}

/* Uniform sampling of an unit hemisphere whose up direction is `up'. */
static INLINE double*
ssp_ran_hemisphere_uniform
  (struct ssp_rng* rng,
   const double up[3],
   double sample[3],
   double* pdf) /* Can be NULL => pdf not computed */
{
  double basis[9];
  double sample_local[3];
  ASSERT(rng && up && sample && d3_is_normalized(up));
  ssp_ran_hemisphere_uniform_local(rng, sample_local, pdf);
  return d33_muld3(sample, d33_basis(basis, up), sample_local);
}

static INLINE float*
ssp_ran_hemisphere_uniform_float
  (struct ssp_rng* rng,
   const float up[3],
   float sample[3],
   float* pdf) /* Can be NULL => pdf not computed */
{
  float basis[9];
  float sample_local[3];
  ASSERT(rng && up && sample && f3_is_normalized(up));
  ssp_ran_hemisphere_uniform_float_local(rng, sample_local, pdf);
  return f33_mulf3(sample, f33_basis(basis, up), sample_local);
}

/* Return the probability distribution for an hemispheric uniform random
 * variate with an explicit `up' direction */
static INLINE double
ssp_ran_hemisphere_uniform_pdf
  (const double up[3],
   const double sample[3])
{
  ASSERT(up && sample && d3_is_normalized(up));
  return d3_dot(sample, up) < 0 ? 0 : 1/(2*PI);
}

static INLINE float
ssp_ran_hemisphere_uniform_float_pdf
  (const float up[3],
   const float sample[3])
{
  ASSERT(up && sample && f3_is_normalized(up));
  return f3_dot(sample, up) < 0 ? 0 : 1/(2*(float)PI);
}

/* Cosine weighted sampling of an unit hemisphere whose up direction is
 * implicitly the Z axis. The PDF of the generated sample is stored in
 * sample[3] */
SSP_API double*
ssp_ran_hemisphere_cos_local
  (struct ssp_rng* rng,
   double sample[3],
   double* pdf); /* Can be NULL => pdf not computed */

SSP_API float*
ssp_ran_hemisphere_cos_float_local
  (struct ssp_rng* rng,
   float sample[3],
   float* pdf); /* Can be NULL => pdf not computed */

/* Return the probability distribution for an hemispheric cosine weighted
 * random variate with an implicit up direction in Z */
static INLINE double
ssp_ran_hemisphere_cos_local_pdf(const double sample[3])
{
  ASSERT(sample);
  return sample[2] < 0 ? 0 : sample[2]/PI;
}

static INLINE float
ssp_ran_hemisphere_cos_float_local_pdf(const float sample[3])
{
  ASSERT(sample);
  return sample[2] < 0 ? 0 : sample[2]/(float)PI;
}

/* Cosine weighted sampling of an unit hemisphere whose up direction is `up'. */
static INLINE double*
ssp_ran_hemisphere_cos
  (struct ssp_rng* rng,
   const double up[3],
   double sample[3],
   double* pdf) /* Can be NULL => pdf not computed */
{
  double sample_local[3];
  double basis[9];
  ASSERT(rng && up && sample && d3_is_normalized(up));
  ssp_ran_hemisphere_cos_local(rng, sample_local, pdf);
  return d33_muld3(sample, d33_basis(basis, up), sample_local);
}

static INLINE float*
ssp_ran_hemisphere_cos_float
  (struct ssp_rng* rng,
   const float up[3],
   float sample[3],
   float* pdf) /* Can be NULL => pdf not computed */
{
  float sample_local[3];
  float basis[9];
  ASSERT(rng && up && sample && f3_is_normalized(up));
  ssp_ran_hemisphere_cos_float_local(rng, sample_local, pdf);
  return f33_mulf3(sample, f33_basis(basis, up), sample_local);
}

/* Return the probability distribution for an hemispheric cosine weighted
 * random variate with an explicit `up' direction */
static INLINE double
ssp_ran_hemisphere_cos_pdf
  (const double up[3],
   const double sample[3])
{
  double dot;
  ASSERT(up && sample);
  dot = d3_dot(sample, up);
  return dot < 0 ? 0 : dot/PI;
}

static INLINE float
ssp_ran_hemisphere_cos_float_pdf
  (const float up[3],
   const float sample[3])
{
  float dot;
  ASSERT(up && sample);
  dot = f3_dot(sample, up);
  return dot < 0 ? 0 : dot/(float)PI;
}

/*******************************************************************************
 * Henyey Greenstein distribution
 ******************************************************************************/
/* Henyey-Greenstein sampling of an unit sphere for an incident direction
 * that is implicitly the Z axis. */
SSP_API double*
ssp_ran_sphere_hg_local
  (struct ssp_rng* rng,
   const double g,
   double sample[3],
   double* pdf); /* Can be NULL => pdf not computed */

SSP_API float*
ssp_ran_sphere_hg_float_local
  (struct ssp_rng* rng,
   const float g,
   float sample[3],
   float* pdf); /* Can be NULL => pdf not computed */

/* Return the probability distribution for a Henyey-Greenstein random
* variate with an implicit incident direction in Z */
SSP_API double
ssp_ran_sphere_hg_local_pdf
  (const double g,
   const double sample[3]);

SSP_API float
ssp_ran_sphere_hg_float_local_pdf
  (const float g,
   const float sample[3]);

/* Henyey-Greenstein sampling of an unit sphere for an incident direction
 * that is `up'. */
static INLINE double*
ssp_ran_sphere_hg
  (struct ssp_rng* rng,
   const double up[3],
   const double g,
   double sample[3],
   double* pdf) /* Can be NULL => pdf not computed */
{
  double sample_local[3];
  double basis[9];
  ASSERT(-1 <= g && g <= +1 && rng && up && sample && d3_is_normalized(up));
  if (fabs(g) == 1) {
    d3_muld(sample, up, g);
    if(pdf) *pdf=INF;
  } else {
    ssp_ran_sphere_hg_local(rng, g, sample_local, pdf);
    d33_muld3(sample, d33_basis(basis, up), sample_local);
  }
  return sample;
}

static INLINE float*
ssp_ran_sphere_hg_float
  (struct ssp_rng* rng,
   const float up[3],
   const float g,
   float sample[3],
   float* pdf) /* Can be NULL => pdf not computed */
{
  float sample_local[3];
  float basis[9];
  ASSERT(-1 <= g && g <= +1 && rng && up && sample && f3_is_normalized(up));
  if(absf(g) == 1) {
    f3_mulf(sample, up, g);
    if(pdf) *pdf = (float)INF;
  } else {
    ssp_ran_sphere_hg_float_local(rng, g, sample_local, pdf);
    f33_mulf3(sample, f33_basis(basis, up), sample_local);
  }
  return sample;
}

/* Return the probability distribution for a Henyey-Greenstein random
* variate with an explicit incident direction that is `up' */
SSP_API double
ssp_ran_sphere_hg_pdf
  (const double up[3],
   const double g,
   const double sample[3]);

SSP_API float
ssp_ran_sphere_hg_float_pdf
  (const float up[3],
   const float g,
   const float sample[3]);

/*******************************************************************************
 * General discrete distribution with state
 ******************************************************************************/
/* Create a discrete random variate data structure from a list of weights.
 * `weights' contain the weights of `nweights' discrete events. Its elements
 * must be positive but they needn't add up to one. */
SSP_API res_T
ssp_ranst_discrete_create
  (struct mem_allocator* allocator, /* May be NULL <=> Use default allocator */
   struct ssp_ranst_discrete** ran);

SSP_API res_T
ssp_ranst_discrete_setup
  (struct ssp_ranst_discrete* discrete,
   const double* weights,
   const size_t nweights);

SSP_API res_T
ssp_ranst_discrete_ref_get
  (struct ssp_ranst_discrete* ran);

SSP_API res_T
ssp_ranst_discrete_ref_put
  (struct ssp_ranst_discrete* ran);

/* Return the index of the sampled discret event. */
SSP_API size_t
ssp_ranst_discrete_get
  (struct ssp_rng* rng,
   const struct ssp_ranst_discrete* ran);

/* Return the PDF of the discret event `i'. */
SSP_API double
ssp_ranst_discrete_pdf
  (const size_t i,
   const struct ssp_ranst_discrete* ran);

/*******************************************************************************
 * Gaussian distribution with state
 ******************************************************************************/
SSP_API res_T
ssp_ranst_gaussian_create
  (struct mem_allocator* allocator, /* May be NULL <=> Use default allocator */
   struct ssp_ranst_gaussian** ran);

SSP_API res_T
ssp_ranst_gaussian_float_create
  (struct mem_allocator* allocator, /* May be NULL <=> Use default allocator */
   struct ssp_ranst_gaussian_float** ran);

SSP_API res_T
ssp_ranst_gaussian_setup
  (struct ssp_ranst_gaussian* ran,
   const double mu,
   const double sigma);

SSP_API res_T
ssp_ranst_gaussian_float_setup
  (struct ssp_ranst_gaussian_float* ran,
   const float mu,
   const float sigma);

SSP_API res_T
ssp_ranst_gaussian_float_ref_get
  (struct ssp_ranst_gaussian_float* ran);

SSP_API res_T
ssp_ranst_gaussian_ref_get
  (struct ssp_ranst_gaussian* ran);

SSP_API res_T
ssp_ranst_gaussian_ref_put
  (struct ssp_ranst_gaussian* ran);

SSP_API res_T
ssp_ranst_gaussian_float_ref_put
  (struct ssp_ranst_gaussian_float* ran);

SSP_API double
ssp_ranst_gaussian_get
  (const struct ssp_ranst_gaussian* ran,
   struct ssp_rng* rng);

SSP_API float
ssp_ranst_gaussian_float_get
  (const struct ssp_ranst_gaussian_float* ran,
   struct ssp_rng* rng);

SSP_API double
ssp_ranst_gaussian_pdf
  (const struct ssp_ranst_gaussian* ran,
   const double x);

SSP_API float
ssp_ranst_gaussian_float_pdf
  (const struct ssp_ranst_gaussian_float* ran,
   const float x);

/*******************************************************************************
 * Piecewise linear distribution with state
 ******************************************************************************/
SSP_API res_T
ssp_ranst_piecewise_linear_create
  (struct mem_allocator* allocator, /* May be NULL <=> Use default allocator */
   struct ssp_ranst_piecewise_linear **ran);

SSP_API res_T
ssp_ranst_piecewise_linear_float_create
  (struct mem_allocator* allocator, /* May be NULL <=> Use default allocator */
   struct ssp_ranst_piecewise_linear_float **ran);

SSP_API res_T
ssp_ranst_piecewise_linear_setup
  (struct ssp_ranst_piecewise_linear *ran,
   const double* intervals,
   const double* weights,
   const size_t size);

SSP_API res_T
ssp_ranst_piecewise_linear_float_setup
  (struct ssp_ranst_piecewise_linear_float *ran,
   const float* intervals,
   const float* weights,
   const size_t size);

SSP_API res_T
ssp_ranst_piecewise_linear_ref_get
  (struct ssp_ranst_piecewise_linear* ran);

SSP_API res_T
ssp_ranst_piecewise_linear_float_ref_get
  (struct ssp_ranst_piecewise_linear_float* ran);

SSP_API res_T
ssp_ranst_piecewise_linear_ref_put
  (struct ssp_ranst_piecewise_linear* ran);

SSP_API res_T
ssp_ranst_piecewise_linear_float_ref_put
  (struct ssp_ranst_piecewise_linear_float* ran);

SSP_API double
ssp_ranst_piecewise_linear_get
  (const struct ssp_ranst_piecewise_linear *ran,
   struct ssp_rng* rng);

SSP_API float
ssp_ranst_piecewise_linear_float_get
  (const struct ssp_ranst_piecewise_linear_float *ran,
   struct ssp_rng* rng);

SSP_API double
ssp_ranst_piecewise_linear_pdf
  (const struct ssp_ranst_piecewise_linear *ran,
   double x);

SSP_API float
ssp_ranst_piecewise_linear_float_pdf
  (const struct ssp_ranst_piecewise_linear_float *ran,
   float x);

/*******************************************************************************
 * Uniform distribution of a point into a disk.
 ******************************************************************************/
SSP_API double*
ssp_ran_uniform_disk_local
  (struct ssp_rng* rng,
   const double radius,
   double pt[3], /* pt[2] <= 0 */
   double* pdf); /* Can be NULL => pdf not computed */

SSP_API float*
ssp_ran_uniform_disk_float_local
  (struct ssp_rng* rng,
   const float radius,
   float pt[3], /* pt[2] <= 0 */
   float* pdf); /* Can be NULL => pdf not computed */

static INLINE double
ssp_ran_uniform_disk_local_pdf(const double radius)
{
  return 1 / (radius * radius);
}

static INLINE float
ssp_ran_uniform_disk_float_local_pdf(const float radius)
{
  return 1 / (radius * radius);
}

static INLINE double*
ssp_ran_uniform_disk
  (struct ssp_rng* rng,
   const double radius,
   const double up[3],
   double pt[3],
   double* pdf) /* Can be NULL => pdf not computed */
{
  double sample_local[3];
  double basis[9];
  ASSERT(rng && up && pt && radius > 0);
  ssp_ran_uniform_disk_local(rng, radius, sample_local, pdf);
  return d33_muld3(pt, d33_basis(basis, up), sample_local);
}

static INLINE float*
ssp_ran_uniform_disk_float
  (struct ssp_rng* rng,
   const float radius,
   const float up[3],
   float pt[3],
   float* pdf) /* Can be NULL => pdf not computed */
{
  float sample_local[3];
  float basis[9];
  ASSERT(rng && up && pt && radius > 0);
  ssp_ran_uniform_disk_float_local(rng, radius, sample_local, pdf);
  return f33_mulf3(pt, f33_basis(basis, up), sample_local);
}

/*******************************************************************************
 * Uniform distribution of a point onto a sphere cap
 * pdf = 1/(2*PI*(1-cos(aperture))
 ******************************************************************************/
/* Uniform sampling of unit sphere cap centered in zero whose up direction
 * is implicity the Z axis. */
SSP_API double*
ssp_ran_sphere_cap_uniform_local
  (struct ssp_rng* rng,
   /* In [-1, 1]. Height where the sphere cap begins. It equal cos(theta_max)
    * where theta_max is the aperture angle from the up axis */
   const double height,
   double sample[3],
   double* pdf); /* Can be NULL => pdf not computed */

SSP_API float*
ssp_ran_sphere_cap_uniform_float_local
  (struct ssp_rng* rng,
   const float height, /* In [-1, 1]. Height of the sphere cap. */
   float sample[3],
   float* pdf); /* Can be NULL => pdf not computed */

static INLINE double
ssp_ran_sphere_cap_uniform_pdf(const double height)
{
  ASSERT(height <= 1.0 && height >= -1.0);
  return height == 1.0 ? INF : 1.0/(2.0*PI*(1-height));
}

static INLINE float
ssp_ran_sphere_cap_uniform_float_pdf(const float height)
{
  ASSERT(height <= 1.f && height >= -1.f);
  return height == 1.f ? (float)INF : 1.f/(2.f*(float)PI*(1.f-height));
}

/* Uniform sampling of unit sphere cap centered in zero whose up direction
 * is explicitly defined */
static INLINE double*
ssp_ran_sphere_cap_uniform
  (struct ssp_rng* rng,
   const double up[3],
   const double height,
   double sample[3],
   double* pdf) /* Can be NULL */
{
  double sample_local[3];
  double basis[9];
  ASSERT(up);
  ssp_ran_sphere_cap_uniform_local(rng, height, sample_local, pdf);
  return d33_muld3(sample, d33_basis(basis, up), sample_local);
}

static  INLINE float*
ssp_ran_sphere_cap_uniform_float
  (struct ssp_rng* rng,
   const float up[3],
   const float height,
   float sample[3],
   float* pdf)
{
  float sample_local[3];
  float basis[9];
  ASSERT(up);
  ssp_ran_sphere_cap_uniform_float_local(rng, height, sample_local, pdf);
  return f33_mulf3(sample, f33_basis(basis, up), sample_local);
}

/*******************************************************************************
 * Uniform distribution of a point onto a truncated sphere cap
 * pdf = 1/(2*PI*(cos(aperture_max)-cos(aperture_min))
 ******************************************************************************/
/* Uniform sampling of unit sphere cap centered in zero whose up direction
 * is implicity the Z axis. */
SSP_API double*
ssp_ran_spherical_zone_uniform_local
  (struct ssp_rng* rng,
   const double height_range[2],
   double pt[3],
   double* pdf);

SSP_API float*
ssp_ran_spherical_zone_uniform_float_local
  (struct ssp_rng* rng,
   const float height_range[2],
   float pt[3],
   float* pdf);

/* Uniform sampling of unit truncated sphere cap centered in zero whose
 * up direction is explicitly defined */
static INLINE double*
ssp_ran_spherical_zone_uniform
  (struct ssp_rng* rng,
   const double up[3],
   const double height_range[2],
   double sample[3],
   double* pdf) /* Can be NULL */
{
  double sample_local[3];
  double basis[9];
  ASSERT(up);
  ssp_ran_spherical_zone_uniform_local(rng, height_range, sample_local, pdf);
  return d33_muld3(sample, d33_basis(basis, up), sample_local);
}

static INLINE float*
ssp_ran_spherical_zone_uniform_float
  (struct ssp_rng* rng,
   const float up[3],
   const float height_range[2],
   float sample[3],
   float* pdf) /* Can be NULL */
{
  float sample_local[3];
  float basis[9];
  ASSERT(up);
  ssp_ran_spherical_zone_uniform_float_local(rng, height_range, sample_local, pdf);
  return f33_mulf3(sample, f33_basis(basis, up), sample_local);
}

static INLINE double
ssp_ran_spherical_zone_uniform_pdf(const double height_range[2])
{
  ASSERT(height_range && height_range[0] <= height_range[1]
    && -1 <= height_range[0] && height_range[1] <= 1);
  if(height_range[0] == height_range[1]) {
    return height_range[0] == 1 ? INF : ssp_ran_circle_uniform_pdf();
  }
  return 1.0/(2.0*PI*(height_range[1]-height_range[0]));
}

static INLINE float
ssp_ran_spherical_zone_uniform_float_pdf(const float height_range[2])
{
  ASSERT(height_range && height_range[0] <= height_range[1]
    && -1 <= height_range[0] && height_range[1] <= 1);
  if(height_range[0] == height_range[1]) {
    return height_range[0] == 1 ?
      (float)INF : ssp_ran_circle_uniform_float_pdf();
  }
  return 1.f/(2.f*(float)PI*(height_range[1]-height_range[0]));
}

END_DECLS

#endif /* SSP_H */
