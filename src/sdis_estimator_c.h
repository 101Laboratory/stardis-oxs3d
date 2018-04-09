/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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

#include <rsys/ref_count.h>

/* Forward declarations */
struct sdis_device;
struct sdis_estimator;

struct sdis_estimator {
  struct sdis_mc temperature;
  size_t nrealisations;
  size_t nfailures;

  ref_T ref;
  struct sdis_device* dev;
};

/*******************************************************************************
 * Estmator data structure
 ******************************************************************************/
extern LOCAL_SYM res_T
estimator_create
  (struct sdis_device* dev,
   struct sdis_estimator** estimator);

#endif /* SDIS_PROBE_ESTIMATOR_C_H */

