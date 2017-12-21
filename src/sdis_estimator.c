/* Copyright (C) |Meso|Star> 2016-2017 (contact@meso-star.com)
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

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
estimator_release(ref_T* ref)
{
  struct sdis_estimator* estimator = NULL;
  struct sdis_device* dev = NULL;
  ASSERT(ref);
  estimator = CONTAINER_OF(ref, struct sdis_estimator, ref);
  dev = estimator->dev;
  MEM_RM(dev->allocator, estimator);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_estimator_ref_get(struct sdis_estimator* estimator)
{
  if(!estimator) return RES_BAD_ARG;
  ref_get(&estimator->ref);
  return RES_OK;
}

res_T
sdis_estimator_ref_put(struct sdis_estimator* estimator)
{
  if(!estimator) return RES_BAD_ARG;
  ref_put(&estimator->ref, estimator_release);
  return RES_OK;
}

res_T
sdis_estimator_get_realisation_count
  (const struct sdis_estimator* estimator, size_t* nrealisations)
{
  if(!estimator || !nrealisations) return RES_BAD_ARG;
  *nrealisations = estimator->nrealisations;
  return RES_OK;
}

res_T
sdis_estimator_get_failure_count
  (const struct sdis_estimator* estimator, size_t* nfailures)
{
  if(!estimator || !nfailures) return RES_BAD_ARG;
  *nfailures = estimator->nfailures;
  return RES_OK;
}

res_T
sdis_estimator_get_temperature
  (const struct sdis_estimator* estimator, struct sdis_mc* mc)
{
  if(!estimator || !mc) return RES_BAD_ARG;
  *mc = estimator->temperature;
  return RES_OK;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
estimator_create(struct sdis_device* dev, struct sdis_estimator** out_estimator)
{
  struct sdis_estimator* estimator = NULL;
  res_T res = RES_OK;

  if(!dev || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

  estimator = MEM_CALLOC(dev->allocator, 1, sizeof(struct sdis_estimator));
  if(!estimator) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&estimator->ref);
  SDIS(device_ref_get(dev));
  estimator->dev = dev;

exit:
  if(out_estimator) *out_estimator = estimator;
  return res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

