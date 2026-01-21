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
#include "sdis_log.h"

#include <star/ssp.h>

#include <rsys/cstr.h>
#include <rsys/mutex.h>

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
  darray_heat_path_release(&estimator->paths);
  if(estimator->mutex) mutex_destroy(estimator->mutex);
  if(estimator->rng) SSP(rng_ref_put(estimator->rng));
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
sdis_estimator_get_type
  (const struct sdis_estimator* estimator, enum sdis_estimator_type* type)
{
  if(!estimator || !type) return RES_BAD_ARG;
  *type = estimator->type;
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

#define SETUP_MC(Mc, Acc) {                                                    \
  (Mc)->E = (Acc)->sum / (double)(Acc)->count;                                 \
  (Mc)->V = (Acc)->sum2 / (double)(Acc)->count - (Mc)->E*(Mc)->E;              \
  (Mc)->V = MMAX((Mc)->V, 0);                                                  \
  (Mc)->SE = sqrt((Mc)->V / (double)(Acc)->count);                             \
} (void)0

res_T
sdis_estimator_get_temperature
  (const struct sdis_estimator* estimator, struct sdis_mc* mc)
{
  if(!estimator || !mc 
  || (  estimator->type != SDIS_ESTIMATOR_TEMPERATURE
     && estimator->type != SDIS_ESTIMATOR_FLUX))
    return RES_BAD_ARG;
  SETUP_MC(mc, &estimator->temperature);
  return RES_OK;
}

res_T
sdis_estimator_get_realisation_time
  (const struct sdis_estimator* estimator, struct sdis_mc* mc)
{
  if(!estimator || !mc) return RES_BAD_ARG;
  SETUP_MC(mc, &estimator->realisation_time);
  return RES_OK;
}

res_T
sdis_estimator_get_convective_flux
  (const struct sdis_estimator* estimator, struct sdis_mc* flux)
{
  if(!estimator || !flux || estimator->type != SDIS_ESTIMATOR_FLUX)
    return RES_BAD_ARG;
  SETUP_MC(flux, &estimator->fluxes[FLUX_CONVECTIVE]);
  return RES_OK;
}

res_T
sdis_estimator_get_radiative_flux
  (const struct sdis_estimator* estimator, struct sdis_mc* flux)
{
  if(!estimator || !flux || estimator->type != SDIS_ESTIMATOR_FLUX)
    return RES_BAD_ARG;
  SETUP_MC(flux, &estimator->fluxes[FLUX_RADIATIVE]);
  return RES_OK;
}

res_T
sdis_estimator_get_imposed_flux
  (const struct sdis_estimator* estimator, struct sdis_mc* flux)
{
  if(!estimator || !flux || estimator->type != SDIS_ESTIMATOR_FLUX)
    return RES_BAD_ARG;
  SETUP_MC(flux, &estimator->fluxes[FLUX_IMPOSED]);
  return RES_OK;
}

res_T
sdis_estimator_get_total_flux
  (const struct sdis_estimator* estimator, struct sdis_mc* flux)
{
  if(!estimator || !flux || estimator->type != SDIS_ESTIMATOR_FLUX)
    return RES_BAD_ARG;
  SETUP_MC(flux, &estimator->fluxes[FLUX_TOTAL]);
  return RES_OK;
}

res_T
sdis_estimator_get_power
  (const struct sdis_estimator* estimator, struct sdis_mc* power)
{
  if(!estimator || !power || estimator->type != SDIS_ESTIMATOR_POWER)
    return RES_BAD_ARG;
  SETUP_MC(power, &estimator->power.power);
  power->E *= estimator->power.spread;
  return RES_OK;
}

#undef SETUP_MC

res_T
sdis_estimator_get_paths_count
  (const struct sdis_estimator* estimator, size_t* npaths)
{
  if(!estimator || !npaths) return RES_BAD_ARG;
  *npaths = darray_heat_path_size_get(&estimator->paths);
  return RES_OK;
}

SDIS_API res_T
sdis_estimator_get_path
  (const struct sdis_estimator* estimator,
   const size_t ipath,
   const struct sdis_heat_path** path)
{
  if(!estimator || !path
  || ipath >= darray_heat_path_size_get(&estimator->paths))
    return RES_BAD_ARG;
  *path = darray_heat_path_cdata_get(&estimator->paths) + ipath;
  return RES_OK;
}

res_T
sdis_estimator_for_each_path
  (const struct sdis_estimator* estimator,
   sdis_process_heat_path_T func,
   void* context)
{
  const struct sdis_heat_path* paths = NULL;
  size_t i, n;
  res_T res = RES_OK;

  if(!estimator || !func) {
    res = RES_BAD_ARG;
    goto error;
  }

  SDIS(estimator_get_paths_count(estimator, &n));
  paths = darray_heat_path_cdata_get(&estimator->paths);
  FOR_EACH(i, 0, n) {
    res = func(paths+i, context);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_estimator_get_rng_state
  (const struct sdis_estimator* estimator,
   struct ssp_rng** rng_state)
{
  if(!estimator || !rng_state) return RES_BAD_ARG;
  *rng_state = estimator->rng;
  return RES_OK;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
estimator_create
  (struct sdis_device* dev,
   const enum sdis_estimator_type type,
   struct sdis_estimator** out_estimator)
{
  struct sdis_estimator* estimator = NULL;
  res_T res = RES_OK;

  if(!dev
  || (unsigned)type >= SDIS_ESTIMATOR_TYPES_COUNT__
  || !out_estimator) {
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
  estimator->nrealisations = 0;
  estimator->nfailures = 0;
  estimator->dev = dev;
  estimator->type = type;
  darray_heat_path_init(dev->allocator, &estimator->paths);

  estimator->mutex = mutex_create();
  if(!estimator->mutex) {
    res = RES_MEM_ERR;
    goto error;
  }

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

res_T
estimator_add_and_release_heat_path
  (struct sdis_estimator* estimator, struct sdis_heat_path* path)
{
  struct sdis_heat_path* dst = NULL;
  size_t i;
  res_T res = RES_OK;
  ASSERT(estimator && path);

  mutex_lock(estimator->mutex);

  i = darray_heat_path_size_get(&estimator->paths);

  res = darray_heat_path_resize(&estimator->paths, i+1);
  if(res != RES_OK) goto error;

  dst = darray_heat_path_data_get(&estimator->paths) + i;
  res = heat_path_copy_and_release(dst, path);
  if(res != RES_OK) goto error;

exit:
  mutex_unlock(estimator->mutex);
  return res;
error:
  goto exit;
}

res_T
estimator_save_rng_state
  (struct sdis_estimator* estimator,
   const struct ssp_rng_proxy* proxy)
{
  ASSERT(estimator && proxy);
  /* Release the previous RNG state if any */
  if(estimator->rng) {
    SSP(rng_ref_put(estimator->rng));
    estimator->rng = NULL;
  }
  return create_rng_from_rng_proxy(estimator->dev, proxy, &estimator->rng);
}

