/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

#include <rsys/math.h>
#include <rsys/ref_count.h>

/* Forward declarations */
struct sdis_device;
struct sdis_estimator;
enum sdis_estimator_type;

enum flux_name {
  FLUX_CONVECTIVE,
  FLUX_RADIATIVE,
  FLUX_TOTAL,
  FLUX_NAMES_COUNT__
};

struct sdis_estimator {
  struct sdis_mc temperature;
  struct sdis_mc realisation_time;
  struct sdis_mc fluxes[FLUX_NAMES_COUNT__];
  size_t nrealisations;
  size_t nfailures;

  struct mutex* mutex;
  struct darray_heat_path paths; /* Tracked paths */

  enum sdis_estimator_type type;
  ref_T ref;
  struct sdis_device* dev;
};

struct sdis_estimator_handle;

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

/* Must be invoked before any others "estimator_setup" functions */
static INLINE void
estimator_setup_realisations_count
  (struct sdis_estimator* estimator,
   const size_t nrealisations,
   const size_t nsuccesses)
{
  ASSERT(estimator && nrealisations && nsuccesses && nsuccesses<=nrealisations);
  estimator->nrealisations = nsuccesses;
  estimator->nfailures = nrealisations - nsuccesses;
}

#define SETUP_MC(McName, Sum, Sum2, Count) {                                   \
  (McName).E = (Sum) / (double)(Count);                                        \
  (McName).V = (Sum2) / (double)(Count) - (McName).E*(McName).E;               \
  (McName).V = MMAX((McName).V, 0);                                            \
  (McName).SE = sqrt((McName).V / (double)(Count));                            \
} (void)0

static INLINE void
estimator_setup_temperature
  (struct sdis_estimator* estim,
   const double sum,
   const double sum2)
{
  ASSERT(estim && estim->nrealisations);
  SETUP_MC(estim->temperature, sum, sum2, estim->nrealisations);
}

static INLINE void
estimator_setup_realisation_time
  (struct sdis_estimator* estim,
   const double sum,
   const double sum2)
{
  ASSERT(estim && estim->nrealisations);
  SETUP_MC(estim->realisation_time, sum, sum2, estim->nrealisations);
}

static INLINE void
estimator_setup_flux
  (struct sdis_estimator* estim,
   const enum flux_name name,
   const double sum,
   const double sum2)
{
  ASSERT(estim && (unsigned)name < FLUX_NAMES_COUNT__ && estim->nrealisations);
  SETUP_MC(estim->fluxes[name], sum, sum2, estim->nrealisations);
}

#undef SETUP_MC

#endif /* SDIS_PROBE_ESTIMATOR_C_H */

