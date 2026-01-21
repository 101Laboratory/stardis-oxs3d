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

#ifndef SDIS_MPI_H
#define SDIS_MPI_H

#ifndef SDIS_ENABLE_MPI
  #error "Invalid inclusion. Stardis-Solver is compiled without MPI support"
#endif

#include "sdis_c.h"

#include <rsys/rsys.h>
#include <mpi.h>

/* Forward declarations */
struct sdis_device;

/* Send the progress status `progress' to the master process */
extern LOCAL_SYM void
mpi_send_progress
  (struct sdis_device* dev,
   const int32_t progress);

/* Fetch the progress status of non master processes into `progress'. The
 * progress of the i-th process is stored in progress[i], meaning that the
 * length of progress must be at least equal to the number of MPI processes */
extern LOCAL_SYM void
mpi_fetch_progress
  (struct sdis_device* dev, 
   int32_t progress[]);

/* Actively wait for the completion of the MPI request `req' */
extern LOCAL_SYM void
mpi_waiting_for_request
  (struct sdis_device* dev,
   MPI_Request* req);

/* Actively wait for a message from the process iproc */
extern LOCAL_SYM void
mpi_waiting_for_message
  (struct sdis_device* dev,
   const int iproc,
   const enum mpi_sdis_message msg,
   MPI_Status* status);

/* Waiting for all processes */
extern LOCAL_SYM void
mpi_barrier
  (struct sdis_device* dev);

#endif /* SDIS_MPI_H */
