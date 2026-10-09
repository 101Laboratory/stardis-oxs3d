/* Copyright (C) 2018-2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef SDIS_COMPUTE_H
#define SDIS_COMPUTE_H

#include <rsys/rsys.h>
#include <rsys/dynamic_array.h>

struct stardis;
struct time;
struct str;

#define DARRAY_NAME estimators
#define DARRAY_DATA struct sdis_estimator*
#include <rsys/dynamic_array.h>

#define READ_RANDOM_STATE(Name) \
  if(!stardis->mpi_initialized || stardis->mpi_rank == 0) { \
    if(str_is_empty(Name)) { \
      /* Force using threefry independently of the default RNG type */ \
      args.rng_type = SSP_RNG_THREEFRY; \
    } else { \
      const char* name = str_cget(Name); \
      stream_r = fopen(name, "r"); \
      if(!stream_r) { \
        res = RES_IO_ERR; \
        logger_print(stardis->logger, LOG_ERROR, \
          "Cannot open generator's state file ('%s').\n", \
          name); \
        goto error; \
      } \
      ERR(ssp_rng_create(stardis->allocator, SSP_RNG_THREEFRY, &args.rng_state)); \
      res =  read_random_generator_state(args.rng_state, stream_r); \
      if(res != RES_OK) { \
        logger_print(stardis->logger, LOG_ERROR, \
          "Cannot read random generator's state ('%s').\n", \
          name); \
        goto error; \
      } \
      fclose(stream_r); stream_r = NULL; \
    } \
  }

#define WRITE_RANDOM_STATE(Name) \
  if(!stardis->mpi_initialized || stardis->mpi_rank == 0) { \
    if(!str_is_empty(Name)) { \
      const char* name = str_cget(Name); \
      stream_r = fopen(name, "wb"); \
      if(!stream_r) { \
        res = RES_IO_ERR; \
        logger_print(stardis->logger, LOG_ERROR, \
          "Cannot write random generator's state ('%s').\n", \
          name); \
        goto error; \
      } \
      ERR(write_random_generator_state(estimator, stream_r)); \
      fclose(stream_r); stream_r = NULL; \
    } \
  }

extern LOCAL_SYM struct sdis_medium*
find_medium_by_name
  (struct stardis* stardis,
   const char* name,
   unsigned* id); /* Can be NULL */

extern LOCAL_SYM res_T
stardis_compute
  (struct stardis* stardis,
   struct time* start);

extern LOCAL_SYM res_T
read_compute_surface
  (struct stardis* stardis);

extern LOCAL_SYM res_T
compute_probe_on_interface
  (struct stardis* stardis,
   struct time* start);

extern  LOCAL_SYM res_T
compute_flux_density_boundary
  (struct stardis* stardis,
   struct time* start);

#endif
