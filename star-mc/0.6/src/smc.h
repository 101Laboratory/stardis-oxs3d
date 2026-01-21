/* Copyright (C) 2015-2018, 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef SMC_H
#define SMC_H

#include <star/ssp.h>
#include <rsys/rsys.h>
#include <limits.h>

/* Library symbol management */
#if defined(SMC_SHARED_BUILD) /* Build shared library */
  #define SMC_API extern EXPORT_SYM
#elif defined(SMC_STATIC) /* Use/build static library */
  #define SMC_API extern LOCAL_SYM
#else /* Use shared library */
  #define SMC_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the smc function `Func'
 * returns an error. One should use this macro on smc function calls for which
 * no explicit error checking is performed */
#ifndef NDEBUG
  #define SMC(Func) ASSERT(smc_ ## Func == RES_OK)
#else
  #define SMC(Func) smc_ ## Func
#endif

#define SMC_NTHREADS_DEFAULT (~0u)

/* Forward declaration of external types */
struct logger;
struct mem_allocator;
struct ssp_rng;

/* Generic type descriptor */
struct smc_type {
  void* (*create)(struct mem_allocator* allocator, void* ctx);
  void (*destroy)(struct mem_allocator* allocator, void* data);

  void (*set)(void* result, const void* value);
  void (*zero)(void* result);
  void (*add)(void* result, const void* op0, const void* op1);
  void (*sub)(void* result, const void* op0, const void* op1);
  void (*mul)(void* result, const void* op0, const void* op1);
  void (*divi)(void* result, const void* op0, const size_t op1);
  void (*sqrt)(void* result, const void* value);
};
#define SMC_TYPE_NULL__ {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
static const struct smc_type SMC_TYPE_NULL = SMC_TYPE_NULL__;

struct smc_estimator_status {
  void* E; /* Expected value */
  void* V; /* Variance */
  void* SE; /* Standard error, i.e. sqrt(V / N) */
  size_t N; /* OK samples count */
  size_t NF; /* Failed samples count */
};
#define SMC_ESTIMATOR_STATUS_NULL__ {NULL, NULL, NULL, 0, 0}
static const struct smc_estimator_status SMC_ESTIMATOR_STATUS_NULL =
  SMC_ESTIMATOR_STATUS_NULL__;

struct smc_integrator {
  res_T (*integrand)
    (void* value,
     struct ssp_rng* rng,
     const unsigned ithread,
     const uint64_t irealisation,
     void* ctx);
  const struct smc_type* type;
  size_t max_realisations; /* Maximum # of realisations */
  size_t max_failures; /* Cancel integration past # failed samples */
};
#define SMC_INTEGRATOR_NULL__ {NULL, NULL, 0, 0}
static const struct smc_integrator SMC_INTEGRATOR_NULL = SMC_INTEGRATOR_NULL__;

struct smc_device_create_args {
  struct mem_allocator* allocator; /* May NULL <=> Default allocator */
  struct logger* logger; /* May NULL <=> default logger */
  unsigned nthreads_hint; /* Hint on the number of threads to use */
  enum ssp_rng_type rng_type; /* SSP_RNG_TYPE_NULL <=> use default RNG type */
  int verbose; /* Verbosity level */
};
#define SMC_DEVICE_CREATE_ARGS_DEFAULT__ {                                     \
  NULL, NULL, SMC_NTHREADS_DEFAULT, SSP_RNG_TYPE_NULL, 0                       \
}
static const struct smc_device_create_args SMC_DEVICE_CREATE_ARGS_DEFAULT =
  SMC_DEVICE_CREATE_ARGS_DEFAULT__;

/* Forward declaration of opaque types */
struct smc_device; /* Entry point of the library */
struct smc_estimator; /* Estimator of an integrator */

BEGIN_DECLS

/* Pre-declared SMC types */
SMC_API const struct smc_type smc_float;
SMC_API const struct smc_type smc_double;
SMC_API const struct smc_type smc_doubleN;

/* Context of a list of double precision floating point data. Must be set as
 * the context parameter of the smc_solve function when the integrator type is
 * smc_doubleN. */
struct smc_doubleN_context {
  size_t count; /* Number of floating point values */
  void* integrand_data; /* User defined data used by the integrand */
};
#define SMC_DOUBLEN_CONTEXT_NULL__ {0, NULL}
static const struct smc_doubleN_context SMC_DOUBLEN_CONTEXT_NULL =
  SMC_DOUBLEN_CONTEXT_NULL__;

/* Syntactic sugar macros and functions */
#define SMC_FLOAT(Val) (*(float*)(Val))
#define SMC_DOUBLE(Val) (*(double*)(Val))
SMC_API double* SMC_DOUBLEN(void* val);

/*******************************************************************************
 * Device API
 ******************************************************************************/
SMC_API res_T
smc_device_create
  (const struct smc_device_create_args* args,
   struct smc_device** dev);

SMC_API res_T
smc_device_ref_get
  (struct smc_device* dev);

SMC_API res_T
smc_device_ref_put
  (struct smc_device* dev);

SMC_API res_T
smc_device_set_rng_type
  (struct smc_device* dev,
   const enum ssp_rng_type type);

SMC_API res_T
smc_device_get_rng_type
  (struct smc_device* dev,
   enum ssp_rng_type* type);

/* Return the maximum number of threads internally used by the device */
SMC_API res_T
smc_device_get_threads_count
  (const struct smc_device* dev,
   unsigned* nthreads);

/*******************************************************************************
 * Integration API
 ******************************************************************************/
SMC_API res_T
smc_solve
  (struct smc_device* dev,
   struct smc_integrator* integrator,
   void* ctx,
   struct smc_estimator** estimator);

SMC_API res_T
smc_solve_N
  (struct smc_device* dev,
   struct smc_integrator* integrator,
   const size_t count,
   void* contexts,
   const size_t sizeof_context,
   struct smc_estimator* estimators[]);

SMC_API res_T
smc_estimator_ref_get
  (struct smc_estimator* estimator);

SMC_API res_T
smc_estimator_ref_put
  (struct smc_estimator* estimator);

SMC_API res_T
smc_estimator_get_status
  (struct smc_estimator* estimator,
   struct smc_estimator_status* status);

END_DECLS

#endif /* SMC_H */

