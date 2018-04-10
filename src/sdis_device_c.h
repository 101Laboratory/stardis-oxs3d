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

#ifndef SDIS_DEVICE_C_H
#define SDIS_DEVICE_C_H

#include <rsys/dynamic_array.h>
#include <rsys/free_list.h>
#include <rsys/ref_count.h>

struct name { FITEM; };
#define FITEM_TYPE name
#include <rsys/free_list.h>

#define DARRAY_NAME accum
#define DARRAY_DATA struct sdis_accum
#include <rsys/dynamic_array.h>

#define DARRAY_NAME tile
#define DARRAY_DATA struct darray_accum
#define DARRAY_FUNCTOR_INIT darray_accum_init
#define DARRAY_FUNCTOR_RELEASE darray_accum_release
#define DARRAY_FUNCTOR_COPY darray_accum_copy
#define DARRAY_FUNCTOR_COPY_AND_RELEASE darray_accum_copy_and_release
#include <rsys/dynamic_array.h>

struct sdis_device {
  struct logger* logger;
  struct mem_allocator* allocator;
  unsigned nthreads;
  int verbose;

  struct flist_name interfaces_names;
  struct flist_name media_names;
  struct darray_tile tiles;

  struct s2d_device* s2d;
  struct s3d_device* s3d;

  ref_T ref;
};

/* Conditionally log a message on the LOG_ERROR stream of the device logger,
 * with respect to the device verbose flag */
extern LOCAL_SYM void
log_err
  (struct sdis_device* dev,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

/* Conditionally log a message on the LOG_WARNING stream of the device logger,
 * with respect to the device verbose flag */
extern LOCAL_SYM void
log_warn
  (struct sdis_device* dev,
   const char* msg,
   ...)
#ifdef COMPILER_GCC
    __attribute((format(printf, 2, 3)))
#endif
;

#endif /* SDIS_DEVICE_C_H */

