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

/* Define functions on estimator buffer generic to the observable type. */

#ifndef SDIS_ESTIMATOR_BUFFER_X_OBS_H
#define SDIS_ESTIMATOR_BUFFER_X_OBS_H

#include "sdis.h"
#include "sdis_estimator_c.h"
#include "sdis_estimator_buffer_c.h"
#include "sdis_misc.h"

#endif /* SDIS_ESTIMATOR_BUFFER_X_OBS_H */

/* Check the generic observable parameter */
#ifndef SDIS_X_OBS
  #error "The SDIS_X_OBS macro must be defined."
#endif

#define X_OBS(Name) CONCAT(CONCAT(Name, _), SDIS_X_OBS)
#define SDIS_SOLVE_X_OBS_ARGS CONCAT(CONCAT(sdis_solve_, SDIS_X_OBS),_args)

res_T
X_OBS(estimator_buffer_create_from_observable_list)
  (struct sdis_device* dev,
   struct ssp_rng_proxy* rng_proxy,
   const struct SDIS_SOLVE_X_OBS_ARGS obs_list_args[],
   const struct accum* per_obs_acc_temp,
   const struct accum* per_obs_acc_time,
   const size_t nobs, /* #observables */
   struct sdis_estimator_buffer** out_estim_buffer)
{
  /* Accumulators throughout the buffer */
  struct accum acc_temp = ACCUM_NULL;
  struct accum acc_time = ACCUM_NULL;
  size_t nrealisations = 0;

  struct sdis_estimator_buffer* estim_buf = NULL;
  size_t iobs = 0;
  res_T res = RES_OK;

  ASSERT(dev && rng_proxy);
  ASSERT(obs_list_args || !nobs);
  ASSERT(per_obs_acc_time && per_obs_acc_time && out_estim_buffer);

  res = estimator_buffer_create(dev, nobs, 1, &estim_buf);
  if(res != RES_OK) {
    log_err(dev, "Unable to allocate the estimator buffer.\n");
    goto error;
  }

  FOR_EACH(iobs, 0, nobs) {
    const struct SDIS_SOLVE_X_OBS_ARGS* obs_args = NULL;
    const struct accum* obs_acc_temp = NULL;
    const struct accum* obs_acc_time = NULL;
    struct sdis_estimator* estim = NULL;

    /* Get observable data */
    obs_args = obs_list_args + iobs;
    obs_acc_temp = per_obs_acc_temp + iobs;
    obs_acc_time = per_obs_acc_time + iobs;
    ASSERT(obs_acc_temp->count == obs_acc_time->count);

    /* Setup the estimator of the current observable */
    estim = estimator_buffer_grab(estim_buf, iobs, 0);
    estimator_setup_realisations_count
      (estim, obs_args->nrealisations, obs_acc_temp->count);
    estimator_setup_temperature
      (estim, obs_acc_temp->sum, obs_acc_temp->sum2);
    estimator_setup_realisation_time
      (estim, obs_acc_time->sum, obs_acc_time->sum2);

    /* Update global accumulators */
    acc_temp.sum +=  obs_acc_temp->sum;
    acc_temp.sum2 += obs_acc_temp->sum2;
    acc_temp.count += obs_acc_temp->count;
    acc_time.sum +=  obs_acc_time->sum;
    acc_time.sum2 += obs_acc_time->sum2;
    acc_time.count += obs_acc_time->count;
    nrealisations += obs_args->nrealisations;
  }

  ASSERT(acc_temp.count == acc_time.count);

  /* Setup global estimator */
  estimator_buffer_setup_realisations_count
    (estim_buf, nrealisations, acc_temp.count);
  estimator_buffer_setup_temperature
    (estim_buf, acc_temp.sum, acc_temp.sum2);
  estimator_buffer_setup_realisation_time
    (estim_buf, acc_time.sum, acc_time.sum2);

  res = estimator_buffer_save_rng_state(estim_buf, rng_proxy);
  if(res != RES_OK) goto error;

exit:
  *out_estim_buffer = estim_buf;
  return res;
error:
  goto exit;
}

#undef X_OBS
#undef SDIS_SOLVE_X_OBS_ARGS
#undef SDIS_X_OBS
