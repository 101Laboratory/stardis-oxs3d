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

#ifndef SDIS_ESTIMATOR_C_H
#define SDIS_ESTIMATOR_C_H

#include "sdis_heat_path.h"
#include "sdis_misc.h"

#include <rsys_math.h>
#include <rsys/ref_count.h>

/* Forward declarations */
struct ssp_rng;
struct sdis_device;
struct sdis_estimator;

enum flux_name {
  FLUX_CONVECTIVE,
  FLUX_RADIATIVE,
  FLUX_IMPOSED,
  FLUX_TOTAL,
  FLUX_NAMES_COUNT__
};

struct sdis_estimator {
  struct accum temperature;
  struct accum realisation_time;
  struct accum fluxes[FLUX_NAMES_COUNT__];

  struct {
    struct accum power;
    double spread;
    double time_range[2];
  } power;

  size_t nrealisations; /* #successes */
  size_t nfailures;

  struct mutex* mutex;
  struct darray_heat_path paths; /* Tracked paths */

  /* State of the RNG after the simulation */
  struct ssp_rng* rng;

  enum sdis_estimator_type type;
  ref_T ref;
  struct sdis_device* dev;
};

/*******************************************************************************
 * Estimator local API
 ******************************************************************************/
extern LOCAL_SYM res_T
estimator_create
  (struct sdis_device* dev,
   const enum sdis_estimator_type type,
   struct sdis_estimator** estimator);

/* Thread safe */
extern LOCAL_SYM res_T
estimator_add_and_release_heat_path
  (struct sdis_estimator* estimator,
   struct sdis_heat_path* path);

extern LOCAL_SYM res_T
estimator_save_rng_state
  (struct sdis_estimator* estimator,
   const struct ssp_rng_proxy* proxy);

/* Must be invoked before any others "estimator_setup" functions */
static INLINE void
estimator_setup_realisations_count
  (struct sdis_estimator* estimator,
   const size_t nrealisations,
   const size_t nsuccesses)
{
  ASSERT(estimator && nrealisations && nsuccesses<=nrealisations);
  estimator->nrealisations = nsuccesses;
  estimator->nfailures = nrealisations - nsuccesses;
}

static INLINE void
estimator_setup_temperature
  (struct sdis_estimator* estim,
   const double sum,
   const double sum2)
{
  ASSERT(estim);
  estim->temperature.sum = sum;
  estim->temperature.sum2 = sum2;
  estim->temperature.count = estim->nrealisations;
}

static INLINE void
estimator_setup_power
  (struct sdis_estimator* estim,
   const double sum,
   const double sum2,
   const double spread,
   const double time_range[2])
{
  ASSERT(estim && time_range);
  estim->power.power.sum = sum;
  estim->power.power.sum2 = sum2;
  estim->power.power.count = estim->nrealisations;
  estim->power.spread = spread;
  estim->power.time_range[0] = time_range[0];
  estim->power.time_range[1] = time_range[1];
}

static INLINE void
estimator_setup_realisation_time
  (struct sdis_estimator* estim,
   const double sum,
   const double sum2)
{
  ASSERT(estim);
  estim->realisation_time.sum = sum;
  estim->realisation_time.sum2 = sum2;
  estim->realisation_time.count = estim->nrealisations;
}

static INLINE void
estimator_setup_flux
  (struct sdis_estimator* estim,
   const enum flux_name name,
   const double sum,
   const double sum2)
{
  ASSERT(estim && (unsigned)name < FLUX_NAMES_COUNT__);
  estim->fluxes[name].sum = sum;
  estim->fluxes[name].sum2 = sum2;
  estim->fluxes[name].count = estim->nrealisations;
}

#endif /* SDIS_PROBE_ESTIMATOR_C_H */

