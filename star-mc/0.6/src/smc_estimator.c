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

#include "smc.h"
#include "smc_device_c.h"
#include "smc_type_c.h"

#include<rsys/mem_allocator.h>

#include <limits.h>
#include <omp.h>
#include <string.h>

struct smc_estimator {
  struct smc_type type;
  void* value;
  void* square_value;
  size_t nsamples;
  size_t nfailed;

  struct smc_estimator_status status;

  struct smc_device* dev;
  ref_T ref;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
estimator_create
  (struct smc_device* dev,
   const struct smc_type* type,
   void* ctx,
   struct smc_estimator** out_estimator)
{
  struct smc_estimator* estimator = NULL;
  res_T res = RES_OK;
  ASSERT(out_estimator && dev && type);

  estimator = MEM_CALLOC(dev->allocator, 1, sizeof(struct smc_estimator));
  if(!estimator) {
    res = RES_MEM_ERR;
    goto error;
  }
  SMC(device_ref_get(dev));
  estimator->dev = dev;
  ref_init(&estimator->ref);

  #define TYPE_CREATE(Dst) {                                                   \
    (Dst) = type->create(dev->allocator, ctx);                                 \
    if(!(Dst)) {                                                               \
      res = RES_MEM_ERR;                                                       \
      goto error;                                                              \
    }                                                                          \
    type->zero((Dst));                                                         \
  } (void)0
  TYPE_CREATE(estimator->value);
  TYPE_CREATE(estimator->square_value);
  TYPE_CREATE(estimator->status.E);
  TYPE_CREATE(estimator->status.V);
  TYPE_CREATE(estimator->status.SE);
  #undef TYPE_CREATE
  estimator->nsamples = 0;
  estimator->nfailed = 0;
  estimator->status.N = 0;
  estimator->status.NF = 0;
  estimator->type = *type;

exit:
  *out_estimator = estimator;
  return res;
error:
  if(estimator) {
    SMC(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

static char
check_integrator(struct smc_integrator* integrator)
{
  ASSERT(integrator);
  return integrator->integrand
      && integrator->type
      && integrator->max_realisations
      && check_type(integrator->type);
}

static void
estimator_release(ref_T* ref)
{
  struct smc_estimator* estimator;
  struct smc_device* dev;
  ASSERT(ref);

  estimator = CONTAINER_OF(ref, struct smc_estimator, ref);
  dev = estimator->dev;
  if(estimator->value)
    estimator->type.destroy(dev->allocator, estimator->value);
  if(estimator->square_value)
    estimator->type.destroy(dev->allocator, estimator->square_value);
  if(estimator->status.E)
    estimator->type.destroy(dev->allocator, estimator->status.E);
  if(estimator->status.V)
    estimator->type.destroy(dev->allocator, estimator->status.V);
  if(estimator->status.SE)
    estimator->type.destroy(dev->allocator, estimator->status.SE);
  MEM_RM(dev->allocator, estimator);
  SMC(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
smc_solve
  (struct smc_device* dev,
   struct smc_integrator* integrator,
   void* ctx,
   struct smc_estimator** out_estimator)
{
  struct smc_estimator* estimator = NULL;
  int64_t i;
  unsigned nthreads = 0;
  char* raw = NULL;
  void** vals = NULL;
  void** accums = NULL;
  void** accums_sqr = NULL;
  size_t* nsamples = NULL;
  size_t nfailed = 0;
  int progress = 0;
  ATOMIC cancel = 0;
  ATOMIC nsolved_realisations = 0;
  res_T res = RES_OK;

  if(!dev || !integrator || !out_estimator || !check_integrator(integrator)) {
    res = RES_BAD_ARG;
    goto error;
  }
  SMC(device_get_threads_count(dev, &nthreads));

  /* Create per thread temporary variables */
  raw = MEM_CALLOC(dev->allocator, nthreads, sizeof(void*)*3 + sizeof(size_t));
  if(!raw) {
    res = RES_MEM_ERR;
    goto error;
  }
  vals       = (void**) (raw + 0 * sizeof(void*) * nthreads);
  accums     = (void**) (raw + 1 * sizeof(void*) * nthreads);
  accums_sqr = (void**) (raw + 2 * sizeof(void*) * nthreads);
  nsamples   = (size_t*)(raw + 3 * sizeof(void*) * nthreads);
  #define TYPE_CREATE(Dst) {                                                   \
    (Dst) = integrator->type->create(dev->allocator, ctx);                     \
    if(!(Dst)) {                                                               \
      res = RES_MEM_ERR;                                                       \
      goto error;                                                              \
    }                                                                          \
    integrator->type->zero((Dst));                                             \
  } (void)0
  FOR_EACH(i, 0, (int64_t)nthreads) {
    TYPE_CREATE(vals[i]);
    TYPE_CREATE(accums[i]);
    TYPE_CREATE(accums_sqr[i]);
    nsamples[i] = 0;
  }
  #undef TYPE_CREATE

  /* Create the estimator */
  res = estimator_create(dev, integrator->type, ctx, &estimator);
  if(res != RES_OK) goto error;

  /* Parallel evaluation of the simulation */
  log_info(dev, "Solving: %3d%%\r", progress);
  #pragma omp parallel for schedule(static)
  for(i = 0; i < (int64_t)integrator->max_realisations; ++i) {
    const int ithread = omp_get_thread_num();
    int64_t n = 0;
    int pcent = 0;
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&cancel)) continue;

    res_local = integrator->integrand
      (vals[ithread], dev->rngs[ithread], (unsigned)ithread, (uint64_t)i, ctx);

    if(res_local != RES_OK) {
      #pragma omp critical
      {
        nfailed += 1;
        if(nfailed > integrator->max_failures) {
          ATOMIC_SET(&cancel, 1);
        }
      }
      continue;
    }

    /* call succeded */
    integrator->type->add(accums[ithread], accums[ithread], vals[ithread]);
    integrator->type->mul(vals[ithread], vals[ithread], vals[ithread]);
    integrator->type->add(accums_sqr[ithread], accums_sqr[ithread], vals[ithread]);
    ++nsamples[ithread];

    n = ATOMIC_INCR(&nsolved_realisations);
    pcent = (int)
      ((double)n * 100.0 / (double)integrator->max_realisations + 0.5/*round*/);
    #pragma omp critical
    if(pcent > progress) {
      progress = pcent;
      log_info(dev, "Solving: %3d%%\r", progress);
    }
  }
  log_info(dev, "Solving: %3d%%\n", progress);

  /* Merge the parallel estimation into the final estimator */
  FOR_EACH(i, 0, (int64_t)nthreads) {
    estimator->nsamples += nsamples[i];
    integrator->type->add(estimator->value, estimator->value, accums[i]);
    integrator->type->add
      (estimator->square_value, estimator->square_value, accums_sqr[i]);
  }
  estimator->nfailed = nfailed;

exit:
  if(raw) { /* Release temporary variables */
    FOR_EACH(i, 0, (int64_t)nthreads) {
      if(vals[i]) integrator->type->destroy(dev->allocator, vals[i]);
      if(accums[i]) integrator->type->destroy(dev->allocator, accums[i]);
      if(accums_sqr[i]) integrator->type->destroy(dev->allocator, accums_sqr[i]);
    }
    MEM_RM(dev->allocator, raw);
  }
  if(out_estimator) *out_estimator = estimator;
  return res;
error:
  if(estimator) {
    SMC(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

res_T
smc_solve_N
  (struct smc_device* dev,
   struct smc_integrator* integrator,
   const size_t count,
   void* ctx,
   const size_t sizeof_ctx,
   struct smc_estimator* estimators[])
{
  void** vals = NULL;
  int64_t i;
  unsigned nthreads = 0;
  int progress = 0;
  ATOMIC cancel = 0;
  ATOMIC nsolved = 0;
  res_T res = RES_OK;

  if(!estimators) {
    res = RES_BAD_ARG;
    goto error;
  }
  memset(estimators, 0, sizeof(struct smc_estimator*) * count);

  if(!dev || !integrator || !count || !check_integrator(integrator)) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Create the estimators */
  FOR_EACH(i, 0, (int64_t)count) {
    res = estimator_create
      (dev, integrator->type, (char*)ctx + (size_t)i*sizeof_ctx, estimators+i);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread temporary variables */
  SMC(device_get_threads_count(dev, &nthreads));
  vals = MEM_CALLOC(dev->allocator, nthreads, sizeof(void*));
  FOR_EACH(i, 0, (int64_t)nthreads) {
    vals[i] = integrator->type->create
      (dev->allocator, (char*)ctx + (size_t)i*sizeof_ctx);
    if(!vals[i]) {
      res = RES_MEM_ERR;
      goto error;
    }
  }

  /* Parallel estimation of N simulations */
  log_info(dev, "Solving: %3d%%\r", progress);
  #pragma omp parallel for schedule(static, 1)
  for(i = 0; i < (int64_t)count; ++i) {
    size_t istep;
    int64_t n = 0;
    int pcent = 0;
    const int ithread = omp_get_thread_num();
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&cancel)) continue;

    FOR_EACH(istep, 0, integrator->max_realisations) {
      if(ATOMIC_GET(&cancel)) break;

      res_local = integrator->integrand
        (vals[ithread], dev->rngs[ithread], (unsigned)ithread, (uint64_t)i,
         (char*)ctx + (size_t)i*sizeof_ctx);

      if(res_local != RES_OK) {
        ++estimators[i]->nfailed;
        if(estimators[i]->nfailed > integrator->max_failures) {
          ATOMIC_SET(&cancel, 1);
        }
        break;
      }

      /* call succeded */
      integrator->type->add
        (estimators[i]->value, estimators[i]->value, vals[ithread]);
      integrator->type->mul
        (vals[ithread], vals[ithread], vals[ithread]);
      integrator->type->add
        (estimators[i]->square_value, estimators[i]->square_value, vals[ithread]);
      ++estimators[i]->nsamples;
    }

    n = ATOMIC_INCR(&nsolved);
    pcent = (int)((double)n * 100.0 / (double)count + 0.5/*round*/);
    #pragma omp critical
    if(pcent > progress) {
      progress = pcent;
      log_info(dev, "Solving: %3d%%\r", progress);
    }
    
  }
  log_info(dev, "Solving: %3d%%\n", progress);

exit:
  if(vals) {
    FOR_EACH(i, 0, (int64_t)nthreads) {
      if(vals[i]) integrator->type->destroy(dev->allocator, vals[i]);
    }
    MEM_RM(dev->allocator, vals);
  }
  return res;
error:
  if(estimators) {
    FOR_EACH(i, 0, (int64_t)count) {
      if(estimators[i]) {
        SMC(estimator_ref_put(estimators[i]));
        estimators[i] = NULL;
      }
    }
  }
  goto exit;
}

res_T
smc_estimator_ref_get(struct smc_estimator* estimator)
{
  if(!estimator) return RES_BAD_ARG;
  ref_get(&estimator->ref);
  return RES_OK;
}

res_T
smc_estimator_ref_put(struct smc_estimator* estimator)
{
  if(!estimator) return RES_BAD_ARG;
  ref_put(&estimator->ref, estimator_release);
  return RES_OK;
}

res_T
smc_estimator_get_status
  (struct smc_estimator* estimator,
   struct smc_estimator_status* status)
{
  if(!estimator || !status)
    return RES_BAD_ARG;

  if(estimator->nsamples != estimator->status.N
    || estimator->nfailed != estimator->status.NF)
  {
    estimator->status.N = estimator->nsamples;
    estimator->status.NF = estimator->nfailed;

    if(estimator->nsamples > 0) {
      /* Variance */
      estimator->type.divi
        (estimator->status.E, estimator->square_value, estimator->nsamples);
      estimator->type.mul
        (estimator->status.V, estimator->value, estimator->value);
      estimator->type.divi
        (estimator->status.V,
         estimator->status.V,
         estimator->nsamples * estimator->nsamples);
      estimator->type.sub
        (estimator->status.V, estimator->status.E, estimator->status.V);
      /* Standard error */
      estimator->type.divi
        (estimator->status.SE, estimator->status.V, estimator->nsamples);
      estimator->type.sqrt(estimator->status.SE, estimator->status.SE);
      /* Expected value */
      estimator->type.divi
        (estimator->status.E, estimator->value,  estimator->nsamples);
    }
    else {
      estimator->type.zero(estimator->status.E);
      estimator->type.zero(estimator->status.SE);
      estimator->type.zero(estimator->status.V);
    }
  }
  *status = estimator->status;
  return RES_OK;
}
