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

#ifndef SDIS_C_H
#define SDIS_C_H

#include <star/ssp.h>
#include <rsys/hash.h>
#include <rsys/rsys.h>

/* Id of the messages sent between processes */
enum mpi_sdis_message {
  MPI_SDIS_MSG_ACCUM_TEMP, /* Temperature accumulator */
  MPI_SDIS_MSG_ACCUM_TIME, /* Time accumulator */
  MPI_SDIS_MSG_ACCUM_FLUX_CONVECTIVE, /* Convective flux accumulator */
  MPI_SDIS_MSG_ACCUM_FLUX_IMPOSED, /* Imposed flux accumulator */
  MPI_SDIS_MSG_ACCUM_FLUX_RADIATIVE, /* Radiative flux accumulator */
  MPI_SDIS_MSG_ACCUM_FLUX_TOTAL, /* Total flux accumulator */
  MPI_SDIS_MSG_ACCUM_MEAN_POWER, /* Mean power accumulator */
  MPI_SDIS_MSG_GREEN_FUNCTION, /* Serialized green function */
  MPI_SDIS_MSG_PROGRESS, /* Progress status */
  MPI_SDIS_MSG_RES_T, /* Result status */
  MPI_SDIS_MSG_RNG_PROXY_SEQUENCE_ID, /* Index of the current RNG sequence */
  MPI_SDIS_MSG_TILE, /* 2D Tile of row ordered accumulators */
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
   struct ssp_rng* rng_state, /* May be NULL */
   const enum ssp_rng_type rng_type, /* RNG type when `rng_state' is NULL */
   struct ssp_rng_proxy** rng_proxy,
   struct ssp_rng** rngs[]);

extern LOCAL_SYM void
release_per_thread_rng
  (struct sdis_device* dev,
   struct ssp_rng* rngs[]);

extern LOCAL_SYM res_T
create_per_thread_green_function
  (struct sdis_scene* scene,
   const hash256_T signature,
   struct sdis_green_function** greens[]);

extern LOCAL_SYM void
release_per_thread_green_function
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

/* Calculate the index range of the current process. It returns the size of the
 * range. The overall_count is the number of calculations to parallelize between
 * processes. For example, it may be the number of realisations of one
 * calculation, or the total number of probe calculations. */
extern LOCAL_SYM size_t
compute_process_index_range
  (const struct sdis_device* dev,
   const size_t overall_count,
   size_t range[2]); /* [lower, upper[ */

/* Return the number of realisations for the current process */
static INLINE size_t
compute_process_realisations_count
  (const struct sdis_device* dev,
   const size_t overall_realisations_count)
{
  size_t range[2];
  return compute_process_index_range(dev, overall_realisations_count, range);
}

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

/* Collect accumulators evaluated over multiple processes, with each accumulator
 * storing a complete Monte Carlo calculation. Without MPI, nothing happens
 * since the per_probe_acc variable already stores the entire list of
 * accumulators. With MPI, non-master processes send their list of accumulators
 * to the master process which saves them in the per_probe_acc, after its
 * accumulators that it has managed, sorted against the identifiers of the
 * probes listed in process_probes. */
extern LOCAL_SYM res_T
gather_accumulators_list
  (struct sdis_device* dev,
   const enum mpi_sdis_message msg,
   const size_t nprobes, /* Total number of probes */
   const size_t process_probes[2], /* Ids of the probes managed by the process */
   struct accum* per_probe_acc); /* List of per probe accumulators */

/* Gather the green functions. With MPI, non master processes store in green
 * the gathering of their per thread green functions and sent the result to the
 * master process. The master process gathers both per thread green functions
 * and per process ones and finally save the result in green */
extern LOCAL_SYM res_T
gather_green_functions
  (struct sdis_scene* scn,
   struct ssp_rng_proxy* proxy,
   struct sdis_green_function* per_thread_green[],
   const struct accum* acc_time,
   struct sdis_green_function** green);

/* Gather the sequence IDs of the proxy RNGs. Without MPI, nothing happens.
 * With MPI, non-master processes send the sequence ID of their proxy RNG to
 * the master process. The master process updates its proxy RNG to ensure that
 * its state is greater than the state of all other proxies, that is, its
 * sequence ID is greater than the sequence IDs received. */
extern LOCAL_SYM res_T
gather_rng_proxy_sequence_id
  (struct sdis_device* dev,
   struct ssp_rng_proxy* proxy);

/* Gather `res' from all other processes. Without MPI, the function simply
 * returns `res'. With MPI, each process sends `res' to the other processes and
 * retrieves the `res' sent by the other processes. The function then returns
 * RES_OK if all the results collected are RES_OK. Otherwise, it returns the
 * first error received. */
extern LOCAL_SYM res_T
gather_res_T
  (struct sdis_device* dev,
   const res_T res);

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

/* Print progress completion, i.e. rewind the printing and print 100% */
extern LOCAL_SYM void
print_progress_completion
  (struct sdis_device* dev,
   int32_t progress[],
   const char* label); /* Text preceding the progress status */

/* Waiting for all processes. Without MPI this function does nothing. With MPI
 * it waits for MPI process synchronisation */
extern LOCAL_SYM void
process_barrier
  (struct sdis_device* dev);

#endif /* SDIS_C_H */
