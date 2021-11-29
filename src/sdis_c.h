/* Copyright (C) 2016-2021 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_C_H
#define SDIS_C_H

#include <rsys/rsys.h>

/* Id of the messages sent between processes */
enum mpi_sdis_message {
  MPI_SDIS_MSG_PROGRESS, /* Progress status */
  MPI_SDIS_MSG_ACCUM_TEMP, /* Temperature accumulator */
  MPI_SDIS_MSG_ACCUM_TIME, /* Time accumulator */
  MPI_SDIS_MSG_COUNT__
};

/* Forward declarations */
struct accum;
struct sdis_device;
struct sdis_estimator;
struct sdis_green_function;
struct sdis_scene;
struct ssp_rng;
struct ssp_rng_proxy;

extern LOCAL_SYM res_T
create_per_thread_rng
  (struct sdis_device* dev,
   struct ssp_rng* rng_state,
   struct ssp_rng_proxy** rng_proxy,
   struct ssp_rng** rngs[]);

extern LOCAL_SYM void
destroy_per_thread_rng
  (struct sdis_device* dev,
   struct ssp_rng* rngs[]);

extern LOCAL_SYM res_T
create_per_thread_green_function
  (struct sdis_scene* scene,
   struct sdis_green_function** greens[]);

extern LOCAL_SYM void
destroy_per_thread_green_function
  (struct sdis_scene* scn,
   struct sdis_green_function* greens[]);

/* Allocate the progress status list for the current process. Without MPI, the
 * length of the progress list is 1. With MPI, the length is also 1 except for
 * the master process for which the length of the list is equal to the number
 * of MPI processes. For this process the list will be used to gather the
 * progress status of the other processes. */
extern LOCAL_SYM res_T
alloc_process_progress
  (struct sdis_device* dev,
   int32_t* progress[]);

extern LOCAL_SYM void
free_process_progress
  (struct sdis_device* dev,
   int32_t progress[]);

/* Compute the number of realisations for the current process */
extern LOCAL_SYM size_t
compute_process_realisations_count
  (const struct sdis_device* dev,
   const size_t overall_realisations_count);

/* Gather the accumulators and sum them in acc. With MPI, non master processes
 * store in acc the gathering of their per thread accumulators that are sent to
 * the master process. The master process gathers the per thread accumulators
 * and the per process ones and save the result in acc */
extern LOCAL_SYM res_T
gather_accumulators
  (struct sdis_device* dev,
   const enum mpi_sdis_message msg,
   const struct accum* per_thread_acc,
   struct accum* acc);

extern LOCAL_SYM res_T
setup_estimator
  (struct sdis_estimator* estimator,
   const struct ssp_rng_proxy* proxy,
   const struct accum* acc_temp,
   const struct accum* acc_time,
   const size_t overall_realisations_count);

extern LOCAL_SYM res_T
setup_green_function
  (struct sdis_green_function* per_thread_green[],
   const struct ssp_rng_proxy* proxy,
   const struct accum* per_thread_acc_time);

/* Print the progress status. With MPI, the master process print the progress
 * of all processes stored in the progress list. Non master processes do not
 * print anything */
extern LOCAL_SYM void
print_progress
  (struct sdis_device* dev,
   int32_t progress[],
   const char* label); /* Text preceding the progress status */

/* Update the printed progress status, i.e. rewind the printing and print the
 * new status */
extern LOCAL_SYM void
print_progress_update
  (struct sdis_device* dev,
   int32_t progress[],
   const char* label); /* Text preceding the progress status */

/* Waiting for the completion of concurrent processes. Without MPI this
 * function does nothing. With MPI it waits for MPI process synchronisation */
extern LOCAL_SYM void
waiting_for_process_completion
  (struct sdis_device* dev);

#endif /* SDIS_C_H */
