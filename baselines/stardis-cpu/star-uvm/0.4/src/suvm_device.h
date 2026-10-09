/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef SUVM_DEVICE_H
#define SUVM_DEVICE_H

#include "suvm_backend.h"
#include <rsys/ref_count.h>

struct logger;
struct mem_allocator;

struct suvm_device {
  int verbose;
  struct logger* logger;
  struct mem_allocator* allocator;
  RTCDevice rtc; /* Embree device */
  ref_T ref;
};

extern LOCAL_SYM void
log_info
  (struct suvm_device* dev,
const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

extern LOCAL_SYM void
log_err
  (struct suvm_device* dev,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

extern LOCAL_SYM void
log_warn
  (struct suvm_device* dev,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
    __attribute((format(printf, 2, 3)))
#endif
;
  
#endif /* SUVM_DEVICE_H */
