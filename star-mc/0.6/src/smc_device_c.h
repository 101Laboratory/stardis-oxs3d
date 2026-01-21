/* Copyright (C) 2015-2018, 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef SMC_DEVICE_C_H
#define SMC_DEVICE_C_H

#include <rsys/ref_count.h>
#include <rsys/stretchy_array.h>

struct logger;
struct mem_allocator;
struct ssp_rng_proxy;
struct ssp_rng;

struct smc_device {
  struct ssp_rng_proxy* rng_proxy;
  struct ssp_rng** rngs;
  struct logger* logger;
  struct mem_allocator* allocator;
  size_t nthreads;
  int verbose;
  ref_T ref;
};

extern LOCAL_SYM void
log_info
  (struct smc_device* smc,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
  ;

extern LOCAL_SYM void
log_err
  (struct smc_device* smc,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
  ;

extern LOCAL_SYM void
log_warn
  (struct smc_device* smc,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
  ;

#endif /* SMC_DEVICE_C_H */
