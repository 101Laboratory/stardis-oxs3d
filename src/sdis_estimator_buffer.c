/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#include "sdis.h"
#include "sdis_device_c.h"
#include "sdis_estimator_c.h"
#include "sdis_estimator_buffer_c.h"
#include "sdis_log.h"

#include <star/ssp.h>

struct sdis_estimator_buffer {
  struct sdis_estimator** estimators; /* Row major per pixel estimators */
  size_t width;
  size_t height;

  struct accum temperature;
  struct accum realisation_time;
  size_t nrealisations; /* #successes */
  size_t nfailures;

  /* State of the RNG after the simulation */
  struct ssp_rng* rng;

  ref_T ref;
  struct sdis_device* dev;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
estimator_buffer_release(ref_T* ref)
{
  struct sdis_estimator_buffer* buf;
  struct sdis_device* dev;
  size_t i;
  ASSERT(ref);

  buf = CONTAINER_OF(ref, struct sdis_estimator_buffer, ref);
  dev = buf->dev;

  if(buf->estimators) {
    FOR_EACH(i, 0, buf->width*buf->height) {
      if(buf->estimators[i]) SDIS(estimator_ref_put(buf->estimators[i]));
    }
    MEM_RM(dev->allocator, buf->estimators);
  }

  if(buf->rng) SSP(rng_ref_put(buf->rng));
  MEM_RM(dev->allocator, buf);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_estimator_buffer_ref_get(struct sdis_estimator_buffer* buf)
{
  if(!buf) return RES_BAD_ARG;
  ref_get(&buf->ref);
  return RES_OK;
}

res_T
sdis_estimator_buffer_ref_put(struct sdis_estimator_buffer* buf)
{
  if(!buf) return RES_BAD_ARG;
  ref_put(&buf->ref, estimator_buffer_release);
  return RES_OK;
}

res_T
sdis_estimator_buffer_get_definition
  (const struct sdis_estimator_buffer* buf, size_t definition[2])
{
  if(!buf || !definition) return RES_BAD_ARG;
  definition[0] = buf->width;
  definition[1] = buf->height;
  return RES_OK;
}

res_T
sdis_estimator_buffer_at
  (const struct sdis_estimator_buffer* buf,
   const size_t x,
   const size_t y,
   const struct sdis_estimator** estimator)
{
  if(!buf || x >= buf->width || y >= buf->height || !estimator)
    return RES_BAD_ARG;
  *estimator = estimator_buffer_grab(buf, x, y);
  return RES_OK;
}

res_T
sdis_estimator_buffer_get_realisation_count
  (const struct sdis_estimator_buffer* buf, size_t* nrealisations)
{
  if(!buf || !nrealisations) return RES_BAD_ARG;
  *nrealisations = buf->nrealisations;
  return RES_OK;
}

res_T
sdis_estimator_buffer_get_failure_count
  (const struct sdis_estimator_buffer* buf, size_t* nfailures)
{
  if(!buf || !nfailures) return RES_BAD_ARG;
  *nfailures = buf->nfailures;
  return RES_OK;
}

#define SETUP_MC(Mc, Acc) {                                                    \
  (Mc)->E = (Acc)->sum / (double)(Acc)->count;                                 \
  (Mc)->V = (Acc)->sum2 / (double)(Acc)->count - (Mc)->E*(Mc)->E;              \
  (Mc)->V = MMAX((Mc)->V, 0);                                                  \
  (Mc)->SE = sqrt((Mc)->V / (double)(Acc)->count);                             \
} (void)0

res_T
sdis_estimator_buffer_get_temperature
  (const struct sdis_estimator_buffer* buf, struct sdis_mc* mc)
{
  if(!buf || !mc) return RES_BAD_ARG;
  SETUP_MC(mc, &buf->temperature);
  return RES_OK;
}

res_T
sdis_estimator_buffer_get_realisation_time
  (const struct sdis_estimator_buffer* buf, struct sdis_mc* mc)
{
  if(!buf || !mc) return RES_BAD_ARG;
  SETUP_MC(mc, &buf->realisation_time);
  return RES_OK;
}

res_T
sdis_estimator_buffer_get_rng_state
  (const struct sdis_estimator_buffer* buf, struct ssp_rng** rng_state)
{
  if(!buf || !rng_state) return RES_BAD_ARG;
  *rng_state = buf->rng;
  return RES_OK;
}

#undef SETUP_MC

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
estimator_buffer_create
  (struct sdis_device* dev,
   const size_t width,
   const size_t height,
   struct sdis_estimator_buffer** out_buf)
{
  struct sdis_estimator_buffer* buf = NULL;
  size_t i;
  res_T res = RES_OK;

  if(!dev || !width || !height || !out_buf) {
    res = RES_BAD_ARG;
    goto error;
  }

  buf = MEM_CALLOC(dev->allocator, 1, sizeof(*buf));
  if(!buf) { res = RES_MEM_ERR; goto error; }

  ref_init(&buf->ref);
  SDIS(device_ref_get(dev));
  buf->dev = dev;
  buf->width = width;
  buf->height = height;

  buf->estimators = MEM_CALLOC
    (dev->allocator, width*height, sizeof(*buf->estimators));
  if(!buf->estimators) {
    log_err(dev, "%s: could not allocate a buffer of estimators of %lux%lu.\n",
      FUNC_NAME, (unsigned long)width, (unsigned long)height);
    res = RES_MEM_ERR;
    goto error;
  }

  FOR_EACH(i, 0, width*height) {
    res = estimator_create(dev, SDIS_ESTIMATOR_TEMPERATURE, buf->estimators+i);
    if(res != RES_OK) goto error;
  }

exit:
  if(out_buf) *out_buf = buf;
  return res;
error:
  if(buf) {
    SDIS(estimator_buffer_ref_put(buf));
    buf = NULL;
  }
  goto exit;
}

struct sdis_estimator*
estimator_buffer_grab
  (const struct sdis_estimator_buffer* buf,
   const size_t x,
   const size_t y)
{
  ASSERT(x < buf->width && y < buf->height);
  return buf->estimators[y*buf->width + x];
}

void
estimator_buffer_setup_realisations_count
  (struct sdis_estimator_buffer* buf,
   const size_t nrealisations,
   const size_t nsuccesses)
{
  ASSERT(buf && nrealisations && nsuccesses && nsuccesses<=nrealisations);
  buf->nrealisations = nsuccesses;
  buf->nfailures = nrealisations - nsuccesses;
}

void
estimator_buffer_setup_temperature
  (struct sdis_estimator_buffer* buf,
   const double sum,
   const double sum2)
{
  ASSERT(buf && buf->nrealisations);
  buf->temperature.sum = sum;
  buf->temperature.sum2 = sum2;
  buf->temperature.count = buf->nrealisations;
}

void
estimator_buffer_setup_realisation_time
  (struct sdis_estimator_buffer* buf,
   const double sum,
   const double sum2)
{
  ASSERT(buf && buf->nrealisations);
  buf->realisation_time.sum = sum;
  buf->realisation_time.sum2 = sum2;
  buf->realisation_time.count = buf->nrealisations;
}

res_T
estimator_buffer_save_rng_state
  (struct sdis_estimator_buffer* buf,
   const struct ssp_rng_proxy* proxy)
{
  ASSERT(buf && proxy);

  /* Release the previous RNG state if any */
  if(buf->rng) {
    SSP(rng_ref_put(buf->rng));
    buf->rng = NULL;
  }
  return create_rng_from_rng_proxy(buf->dev, proxy, &buf->rng);
}

/* Define the functions generic to the observable type */
#define SDIS_X_OBS probe
#include "sdis_estimator_buffer_X_obs.h"
#define SDIS_X_OBS probe_boundary
#include "sdis_estimator_buffer_X_obs.h"
