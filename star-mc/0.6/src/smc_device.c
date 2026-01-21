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

#include <star/ssp.h>

#include <rsys/logger.h>
#include<rsys/mem_allocator.h>

#include <omp.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE res_T
check_device_create_args(const struct smc_device_create_args* args)
{
  if(!args || args->nthreads_hint == 0) return RES_BAD_ARG;
  return RES_OK;
}

static INLINE void
log_msg
  (struct smc_device* smc,
   const enum log_type stream,
   const char* msg,
   va_list vargs)
{
  ASSERT(smc && msg);
  if(smc->verbose) {
    CHK(logger_vprint(smc->logger, stream, msg, vargs) == RES_OK);
  }
}

static void
release_rngs(struct smc_device* dev)
{
  size_t i;
  ASSERT(dev);
  if(dev->rng_proxy) {
    ssp_rng_proxy_ref_put(dev->rng_proxy);
    dev->rng_proxy = NULL;
  }
  if(dev->rngs) {
    ASSERT(dev->nthreads == sa_size(dev->rngs));
    FOR_EACH(i, 0, dev->nthreads) {
      if(dev->rngs[i]) {
        ssp_rng_ref_put(dev->rngs[i]);
        dev->rngs[i] = NULL;
      }
    }
    sa_release(dev->rngs);
    dev->rngs = NULL;
  }
}

static void
device_release(ref_T* ref)
{
  struct smc_device* dev;
  ASSERT(ref);
  dev = CONTAINER_OF(ref, struct smc_device, ref);
  release_rngs(dev);
  MEM_RM(dev->allocator, dev);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
smc_device_create
  (const struct smc_device_create_args* args,
   struct smc_device** out_dev)
{
  struct smc_device* dev = NULL;
  struct mem_allocator* allocator = &mem_default_allocator;
  struct logger* logger = LOGGER_DEFAULT;
  enum ssp_rng_type rng_type = SSP_RNG_THREEFRY;
  res_T res = RES_OK;

  if(!out_dev) { res = RES_BAD_ARG; goto exit; }

  res = check_device_create_args(args);
  if(res != RES_OK) goto error;

  if(args->allocator) allocator = args->allocator;
  if(args->logger) logger = args->logger;
  if(args->rng_type != SSP_RNG_TYPE_NULL) rng_type = args->rng_type;

  dev = MEM_CALLOC(allocator, 1, sizeof(struct smc_device));
  if(!dev) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&dev->ref);
  dev->allocator = allocator;
  dev->logger = logger;
  dev->verbose = args->verbose;

  dev->nthreads = MMIN(args->nthreads_hint, (unsigned)omp_get_num_procs());
  omp_set_num_threads((int)dev->nthreads);

  res = smc_device_set_rng_type(dev, rng_type);
  if(res != RES_OK) goto error;

exit:
  if(out_dev) *out_dev = dev;
  return res;
error:
  if(dev) {
    SMC(device_ref_put(dev));
    dev = NULL;
  }
  goto exit;
}

res_T
smc_device_ref_get(struct smc_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_get(&dev->ref);
  return RES_OK;
}

res_T
smc_device_ref_put(struct smc_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_put(&dev->ref, device_release);
  return RES_OK;
}

res_T
smc_device_set_rng_type(struct smc_device* dev, const enum ssp_rng_type type)
{
  size_t i;
  res_T res = RES_OK;
  struct ssp_rng_proxy* proxy = NULL;
  struct ssp_rng** rngs = NULL;

  if(!dev || type == SSP_RNG_TYPE_NULL) {
    /* Skip the error block */
    return RES_BAD_ARG;
  }

  proxy = dev->rng_proxy;
  rngs = dev->rngs;
  dev->rng_proxy = NULL;
  dev->rngs = NULL;

  /* Create the new rng_proxy */
  res = ssp_rng_proxy_create
    (dev->allocator, type, dev->nthreads, &dev->rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the new per thread rng */
  dev->rngs = sa_add(dev->rngs, dev->nthreads);
  memset(dev->rngs, 0, dev->nthreads*sizeof(struct ssp_rng*));
  FOR_EACH(i, 0, dev->nthreads) {
    res = ssp_rng_proxy_create_rng(dev->rng_proxy, i, dev->rngs + i);
    if(res != RES_OK) goto error;
  }

  /* Release the previous RNG data structure */
  if(proxy) SSP(rng_proxy_ref_put(proxy));
  if(rngs) {
    FOR_EACH(i, 0, dev->nthreads) {
      if(rngs[i]) SSP(rng_ref_put(rngs[i]));
    }
    sa_release(rngs);
  }

exit:
  return res;
error:
  /* Restore the previous RNG type */
  release_rngs(dev);
  dev->rng_proxy = proxy;
  dev->rngs = rngs;
  goto exit;
}

res_T
smc_device_get_rng_type(struct smc_device* dev, enum ssp_rng_type* type)
{
  if(!dev || !type) return RES_BAD_ARG;
  return ssp_rng_proxy_get_type(dev->rng_proxy, type);
}

res_T
smc_device_get_threads_count(const struct smc_device* dev, unsigned* nthreads)
{
  if(!dev || !nthreads) return RES_BAD_ARG;
  ASSERT(dev->nthreads <= UINT_MAX);
  *nthreads = (unsigned)dev->nthreads;
  return RES_OK;
}


/*******************************************************************************
 * Local functions
 ******************************************************************************/
void
log_info(struct smc_device* smc, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(smc && msg);
  va_start(vargs_list, msg);
  log_msg(smc, LOG_OUTPUT, msg, vargs_list);
  va_end(vargs_list);
}

void
log_err(struct smc_device* smc, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(smc && msg);
  va_start(vargs_list, msg);
  log_msg(smc, LOG_ERROR, msg, vargs_list);
  va_end(vargs_list);
}

void
log_warn(struct smc_device* smc, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(smc && msg);
  va_start(vargs_list, msg);
  log_msg(smc, LOG_WARNING, msg, vargs_list);
  va_end(vargs_list);
}
