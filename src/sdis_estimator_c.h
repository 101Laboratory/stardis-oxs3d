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
  struct sdis_mc fluxes[FLUX_NAMES_COUNT__];
  size_t nrealisations;
  size_t nfailures;

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
   const size_t nrealisations,
   const size_t nsuccesses,
   struct sdis_estimator** estimator);

static INLINE void
estimator_setup_temperature
  (struct sdis_estimator* estim,
   const double sum,
   const double sum2)
{
  double N;
  ASSERT(estim && estim->nrealisations);
  N = (double)estim->nrealisations;
  estim->temperature.E = sum/N;
  estim->temperature.V = sum2/N - estim->temperature.E*estim->temperature.E;
  estim->temperature.V = MMAX(estim->temperature.V, 0);
  estim->temperature.SE = sqrt(estim->temperature.V/N);
}

static INLINE void
estimator_setup_flux
  (struct sdis_estimator* estim,
   const enum flux_name name,
   const double sum,
   const double sum2)
{
  double N;
  ASSERT(estim && (unsigned)name < FLUX_NAMES_COUNT__ && estim->nrealisations);
  N = (double)estim->nrealisations;
  estim->fluxes[name].E = sum/N;
  estim->fluxes[name].V = sum2/N - estim->fluxes[name].E*estim->fluxes[name].E;
  estim->fluxes[name].V = MMAX(estim->fluxes[name].V, 0);
  estim->fluxes[name].SE = sqrt(estim->fluxes[name].V/N);
}

#endif /* SDIS_PROBE_ESTIMATOR_C_H */

