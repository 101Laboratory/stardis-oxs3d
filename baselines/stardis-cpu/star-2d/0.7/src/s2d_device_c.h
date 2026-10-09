/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef S2D_DEVICE_C_H
#define S2D_DEVICE_C_H

#include "s2d_backend.h"

#include <rsys/ref_count.h>
#include <rsys/free_list.h>

struct name { FITEM; };
#define FITEM_TYPE name
#include <rsys/free_list.h>

struct s2d_device {
  int verbose;
  struct logger* logger;
  struct mem_allocator* allocator;

  RTCDevice rtc; /* Embree device */

  struct flist_name names; /* List of shape id */

  ref_T ref;
};

/* Conditionally log a message on the LOG_ERROR stream of the device logger,
 * with respect to the device verbose flag */
extern LOCAL_SYM void
log_error
  (struct s2d_device* dev,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

/* Conditionally log a message on the LOG_WARNING stream of the device logger,
 * with respect to the device verbose flag */
extern LOCAL_SYM void
log_warning
  (struct s2d_device* dev,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
    __attribute((format(printf, 2, 3)))
#endif
;

#endif /* S2D_DEVICE_C_H */

