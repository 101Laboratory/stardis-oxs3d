/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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

#include "sdis.h"

#include <rsys/dynamic_array.h>
#include <rsys/free_list.h>
#include <rsys/logger.h>
#include <rsys/ref_count.h>

/* Forward declarations */
struct ssp_rng;
struct ssp_rng_proxy;

struct name { FITEM; void* mem; };
#define FITEM_TYPE name
#include <rsys/free_list.h>

struct sdis_device {
  struct logger* logger;
  struct logger logger__; /* Default logger */
  struct mem_allocator* allocator;
  unsigned nthreads;
  int verbose;

  struct flist_name interfaces_names;
  struct flist_name media_names;

  struct s2d_device* s2d_dev;
  struct s3d_device* s3d_dev;

  ref_T ref;
};

extern LOCAL_SYM res_T
create_rng_from_rng_proxy
  (struct sdis_device* dev,
   const struct ssp_rng_proxy* proxy,
   struct ssp_rng** out_rng);

#endif /* SDIS_DEVICE_C_H */

