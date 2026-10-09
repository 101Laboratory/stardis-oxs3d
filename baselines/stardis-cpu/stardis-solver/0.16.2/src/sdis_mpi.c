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

#define _POSIX_C_SOURCE 199309L /* nanosleep */

#include "sdis_c.h"
#include "sdis_device_c.h"
#include "sdis_mpi.h"

#include <rsys/mutex.h>

#include <sys/time.h>
#include <sys/time.h> /* struct timespec */

/*******************************************************************************
 * Local functions
 ******************************************************************************/
void
mpi_send_progress(struct sdis_device* dev, const int32_t progress)
{
  ASSERT(dev && dev->use_mpi);
  (void)dev;

  mutex_lock(dev->mpi_mutex);
  MPI(Send(&progress, 1/*#data*/, MPI_INT32_T, 0/*dst rank*/,
    MPI_SDIS_MSG_PROGRESS, MPI_COMM_WORLD));
  mutex_unlock(dev->mpi_mutex);
}

void
mpi_fetch_progress(struct sdis_device* dev, int32_t progress[])
{
  int iproc;
  ASSERT(dev && dev->use_mpi && dev->mpi_rank == 0);

  FOR_EACH(iproc, 1, dev->mpi_nprocs) {
    /* Flush all progress messages sent by the process `iproc' */
    for(;;) {
      MPI_Request req;
      int flag;

      /* Query for a progress message */
      mutex_lock(dev->mpi_mutex);                                   
      MPI(Iprobe(iproc, MPI_SDIS_MSG_PROGRESS, MPI_COMM_WORLD, &flag,
        MPI_STATUS_IGNORE));
      mutex_unlock(dev->mpi_mutex);

      if(flag == 0) break; /* No more progress status to receive */

      /* Asynchronously receive the progress status */
      mutex_lock(dev->mpi_mutex);                                   
      MPI(Irecv(&progress[iproc], 1/*count*/, MPI_INT32_T, iproc,
        MPI_SDIS_MSG_PROGRESS, MPI_COMM_WORLD, &req));
      mutex_unlock(dev->mpi_mutex);

      /* Actively wait for the completion of the receive procedure */
      mpi_waiting_for_request(dev, &req);
    }
  }
}

void
mpi_waiting_for_request(struct sdis_device* dev, MPI_Request* req)
{
  struct timespec t;
  ASSERT(dev && dev->use_mpi && req);

  /* Setup the suspend time of the process while waiting for a request */
  t.tv_sec = 0;
  t.tv_nsec = 10000000; /* 10ms */

  /* Wait for process synchronisation */
  for(;;) {
    int complete;
    
    mutex_lock(dev->mpi_mutex);
    MPI(Test(req, &complete, MPI_STATUS_IGNORE));
    mutex_unlock(dev->mpi_mutex);

    if(complete) break;
    nanosleep(&t, NULL);
  }
}

void
mpi_waiting_for_message
  (struct sdis_device* dev,
   const int iproc,
   const enum mpi_sdis_message msg,
   MPI_Status* status)
{
  struct timespec t;
  ASSERT(dev && dev->use_mpi && status);

  /* Setup the suspend time of the process while waiting for a message */
  t.tv_sec = 0;
  t.tv_nsec = 10000000; /* 10ms */

  /* Wait for process synchronisation */
  for(;;) {
    int flag;

    /* Asynchronously probe for green function data */
    mutex_lock(dev->mpi_mutex);
    MPI(Iprobe(iproc, msg, MPI_COMM_WORLD, &flag, status));
    mutex_unlock(dev->mpi_mutex);

    if(flag) break;
    nanosleep(&t, NULL);
  }
}

void
mpi_barrier(struct sdis_device* dev)
{
  MPI_Request req;
  ASSERT(dev && dev->use_mpi);

  /* Asynchronously wait for process completion. Use an asynchronous barrier to
   * avoid a dead lock if another thread on the same process queries the
   * mpi_mutex */

  mutex_lock(dev->mpi_mutex);
  MPI(Ibarrier(MPI_COMM_WORLD, &req));
  mutex_unlock(dev->mpi_mutex);

  mpi_waiting_for_request(dev, &req);
}
