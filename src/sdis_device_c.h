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

#ifndef SDIS_DEVICE_C_H
#define SDIS_DEVICE_C_H

#include "sdis.h"

#include <rsys/dynamic_array.h>
#include <rsys/free_list.h>
#include <rsys/logger.h>
#include <rsys/ref_count.h>
#include <rsys/str.h>

#ifdef SDIS_ENABLE_MPI
  #ifndef NDEBUG
    #define MPI(Func) ASSERT(MPI_##Func == MPI_SUCCESS)
  #else
    #define MPI(Func) MPI_##Func
  #endif
#endif

/* Forward declarations */
struct mutex;
struct ssp_rng;
struct ssp_rng_proxy;
struct swf_tabulation;

struct name { FITEM; void* mem; };
#define FITEM_TYPE name
#include <rsys/free_list.h>

struct sdis_device {
  struct logger* logger;
  struct logger logger__; /* Default logger */
  struct mem_allocator* allocator;
  unsigned nthreads;
  int no_escape_sequence;
  int verbose;

#ifdef SDIS_ENABLE_MPI
  int mpi_rank; /* Rank of the process in the MPI group */
  int mpi_nprocs; /* Overall #processes in the MPI group */
  struct str mpi_err_str; /* String used to store the MPI error string */

  struct mutex* mpi_mutex; /* Protect MPI calls from concurrent threads */
  int use_mpi;
#endif

  struct flist_name interfaces_names;
  struct flist_name media_names;
  struct flist_name source_names;

  struct s2d_device* s2d_dev;
  struct s3d_device* s3d_dev;

  struct swf_tabulation* H_2d;
  struct swf_tabulation* H_3d;

  ref_T ref;
};

extern LOCAL_SYM res_T
create_rng_from_rng_proxy
  (struct sdis_device* dev,
   const struct ssp_rng_proxy* proxy,
   struct ssp_rng** out_rng);

#endif /* SDIS_DEVICE_C_H */

