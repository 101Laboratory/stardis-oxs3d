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

#ifndef SDIS_ESTIMATOR_BUFFER_C_H
#define SDIS_ESTIMATOR_BUFFER_C_H

#include <rsys/rsys.h>

/* Forward declaration */
struct sdis_device;
struct sdis_estimator;
struct sdis_estimator_buffer;

extern LOCAL_SYM res_T
estimator_buffer_create
  (struct sdis_device* dev,
   const size_t width,
   const size_t height,
   struct sdis_estimator_buffer** buf);

extern LOCAL_SYM struct sdis_estimator*
estimator_buffer_grab
  (const struct sdis_estimator_buffer* buf,
   const size_t x,
   const size_t y);

extern LOCAL_SYM void
estimator_buffer_setup_realisations_count
  (struct sdis_estimator_buffer* buf,
   const size_t nrealisations,
   const size_t nsuccesses);

extern LOCAL_SYM void
estimator_buffer_setup_temperature
  (struct sdis_estimator_buffer* buf,
   const double sum,
   const double sum2);

extern LOCAL_SYM void
estimator_buffer_setup_realisation_time
  (struct sdis_estimator_buffer* buf,
   const double sum,
   const double sum2);

extern LOCAL_SYM res_T
estimator_buffer_save_rng_state
  (struct sdis_estimator_buffer* buf,
   const struct ssp_rng_proxy* proxy);

extern LOCAL_SYM res_T
estimator_buffer_create_from_observable_list_probe
  (struct sdis_device* dev,
   struct ssp_rng_proxy* rng_proxy,
   const struct sdis_solve_probe_args obs_list_args[],
   const struct accum* per_obs_acc_temp,
   const struct accum* per_obs_acc_time,
   const size_t nobs, /* #observables */
   struct sdis_estimator_buffer** out_estim_buffer);

extern LOCAL_SYM res_T
estimator_buffer_create_from_observable_list_probe_boundary
  (struct sdis_device* dev,
   struct ssp_rng_proxy* rng_proxy,
   const struct sdis_solve_probe_boundary_args obs_list_args[],
   const struct accum* per_obs_acc_temp,
   const struct accum* per_obs_acc_time,
   const size_t nobs, /* #observables */
   struct sdis_estimator_buffer** out_estim_buffer);

#endif /* SDIS_ESTIMATOR_BUFFER_C_H */

