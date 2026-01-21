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

#include "sdis_c.h"
#include "sdis_device_c.h"
#include "sdis_estimator_c.h"
#include "sdis_interface_c.h"
#include "sdis_log.h"
#include "sdis_green.h"
#include "sdis_realisation.h"
#include "sdis_scene_c.h"

#include <rsys/algorithm.h>
#include <rsys/clock_time.h>
#include <rsys/dynamic_array.h>

#include <omp.h>

#include "sdis_Xd_begin.h"

#ifndef SDIS_SOLVE_MEDIUM_XD_H
#define SDIS_SOLVE_MEDIUM_XD_H

/*
 * Define the data structures and functions that are not generic to the
 * SDIS_XD_DIMENSION parameter
 */

struct enclosure_cumul {
  unsigned enc_id;
  double cumul;
};

/* Define the darray_enclosure_cumul dynamic array */
#define DARRAY_NAME enclosure_cumul
#define DARRAY_DATA struct enclosure_cumul
#include <rsys/dynamic_array.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
cmp_double_to_enc_cumuls(const void* a, const void* b)
{
  const double key = *(const double*)a;
  const struct enclosure_cumul* enc_cumul = (const struct enclosure_cumul*)b;
  if(key < enc_cumul->cumul) return -1;
  if(key > enc_cumul->cumul) return +1;
  return 0;
}

static res_T
compute_medium_enclosure_cumulative
  (struct sdis_scene* scn,
   const struct sdis_medium* mdm,
   struct darray_enclosure_cumul* cumul)
{
  struct htable_enclosure_iterator it, end;
  double accum = 0;
  res_T res = RES_OK;
  ASSERT(scn && mdm && cumul);

  darray_enclosure_cumul_clear(cumul);

  htable_enclosure_begin(&scn->enclosures, &it);
  htable_enclosure_end(&scn->enclosures, &end);
  while(!htable_enclosure_iterator_eq(&it, &end)) {
    struct enclosure_cumul enc_cumul;
    const struct enclosure* enc = htable_enclosure_iterator_data_get(&it);
    const unsigned* enc_id = htable_enclosure_iterator_key_get(&it);
    htable_enclosure_iterator_next(&it);

    if(sdis_medium_get_id(mdm) != enc->medium_id) continue;

    accum += enc->V;
    enc_cumul.enc_id = *enc_id;
    enc_cumul.cumul = accum;
    res = darray_enclosure_cumul_push_back(cumul, &enc_cumul);
    if(res != RES_OK) goto error;
  }

  if(darray_enclosure_cumul_size_get(cumul) == 0) {
    log_err(scn->dev,
      "%s: there is no enclosure that encompasses the submitted medium.\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  darray_enclosure_cumul_clear(cumul);
  goto exit;
}

static unsigned
sample_medium_enclosure
  (const struct darray_enclosure_cumul* cumul, struct ssp_rng* rng)
{
  const struct enclosure_cumul* enc_cumuls = NULL;
  const struct enclosure_cumul* enc_cumul_found = NULL;
  double r;
  size_t i;
  size_t sz;
  ASSERT(cumul && rng && darray_enclosure_cumul_size_get(cumul));

  sz = darray_enclosure_cumul_size_get(cumul);
  enc_cumuls = darray_enclosure_cumul_cdata_get(cumul);
  if(sz == 1) {
    enc_cumul_found = enc_cumuls;
  } else {
    /* Generate an uniform random number in [0, cumul[ */
    r = ssp_rng_canonical(rng);
    r = r * enc_cumuls[sz-1].cumul;

    enc_cumul_found = search_lower_bound
      (&r, enc_cumuls, sz, sizeof(*enc_cumuls), cmp_double_to_enc_cumuls);
    ASSERT(enc_cumul_found);

    /* search_lower_bound returns the first entry that is not less than `r'.
     * The following code discards entries that are also equal to `r'. */
    i = (size_t)(enc_cumul_found - enc_cumuls);
    while(enc_cumuls[i].cumul == r && i < sz) ++i;
    ASSERT(i < sz);

    enc_cumul_found = enc_cumuls + i;
  }
  return enc_cumul_found->enc_id;
}

static INLINE res_T
check_solve_medium_args(const struct sdis_solve_medium_args* args)
{
  if(!args) return RES_BAD_ARG;

  /* Check the medium */
  if(!args->medium) return RES_BAD_ARG;

  /* Check #realisations */
  if(!args->nrealisations || args->nrealisations > INT64_MAX) {
    return RES_BAD_ARG;
  }

  /* Check time range */
  if(args->time_range[0] < 0 || args->time_range[1] < args->time_range[0]) {
    return RES_BAD_ARG;
  }
  if(args->time_range[1] > DBL_MAX
  && args->time_range[0] != args->time_range[1]) {
    return RES_BAD_ARG;
  }

  /* Check picard order */
  if(args->picard_order < 1) {
    return RES_BAD_ARG;
  }

  /* Check the RNG type */
  if(!args->rng_state && args->rng_type >= SSP_RNG_TYPES_COUNT__) {
    return RES_BAD_ARG;
  }

  /* Check the diffusion algorithm */
  if((unsigned)args->diff_algo >= SDIS_DIFFUSION_ALGORITHMS_COUNT__) {
    return RES_BAD_ARG;
  }

  return RES_OK;
}

static INLINE res_T
check_compute_power_args(const struct sdis_compute_power_args* args)
{
  if(!args) return RES_BAD_ARG;

  /* Check the medium */
  if(!args->medium) return RES_BAD_ARG;

  /* Check #realisations */
  if(!args->nrealisations || args->nrealisations > INT64_MAX) {
    return RES_BAD_ARG;
  }

  /* Check time range */
  if(args->time_range[0] < 0 || args->time_range[1] < args->time_range[0]) {
    return RES_BAD_ARG;
  }
  if(args->time_range[1] > DBL_MAX
  && args->time_range[0] != args->time_range[1]) {
    return RES_BAD_ARG;
  }

  /* Check the RNG type */
  if(!args->rng_state && args->rng_type >= SSP_RNG_TYPES_COUNT__) {
    return RES_BAD_ARG;
  }

  return RES_OK;
}

#endif /* !SDIS_SOLVE_MEDIUM_XD_H */

/*******************************************************************************
 * Helper functions generic to the SDIS_XD_DIMENSION parameter
 ******************************************************************************/
static res_T
XD(sample_enclosure_position)
  (struct sdis_scene* scn,
   const unsigned enc_id,
   struct ssp_rng* rng,
   double pos[DIM])
{
  const size_t MAX_NCHALLENGES = 1000;

  const struct enclosure* enc = NULL;
  float lower[DIM], upper[DIM];
  size_t ichallenge;
  size_t i;
  res_T res = RES_OK;
  ASSERT(scn && rng && pos);
  ASSERT(enc_id != ENCLOSURE_ID_NULL);

  enc = scene_get_enclosure(scn, enc_id);
  SXD(scene_view_get_aabb(enc->sXd(view), lower, upper));

  FOR_EACH(i, 0, DIM) {
    if(lower[i] > upper[i] || eq_epsf(lower[i], upper[i], 1.e-6f)) {
      res = RES_BAD_ARG; /* Degenerated enclosure */
      goto error;
    }
  }

  FOR_EACH(ichallenge, 0, MAX_NCHALLENGES) {
    struct sXd(hit) hit = SXD_HIT_NULL;
    const float dir[3] = {1,0,0};
    const float range[2] = {FLT_MIN, FLT_MAX};
    float org[DIM];

    /* Generate an uniform position into the enclosure AABB */
    FOR_EACH(i, 0, DIM) {
      org[i] = ssp_rng_uniform_float(rng, lower[i], upper[i]);
    }

    /* Check that pos lies into the enclosure; trace a ray and check that it
     * hits something and that the normal points towards the traced ray
     * direction (enclosure normals point inword the enclosure) */
    SXD(scene_view_trace_ray(enc->sXd(view), org, dir, range, NULL, &hit));
    if(!SXD_HIT_NONE(&hit) && fX(dot)(dir, hit.normal) < 0) {
      dX_set_fX(pos, org);
      break;
    }
  }

  if(ichallenge >= MAX_NCHALLENGES) {
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
static res_T
XD(solve_medium)
  (struct sdis_scene* scn,
   const struct sdis_solve_medium_args* args,
   struct sdis_green_function** out_green, /* May be NULL <=> No green func */
   struct sdis_estimator** out_estimator) /* May be NULL <=> No estimator */
{
  /* Time registration */
  struct time time0, time1;
  char buf[128]; /* Temporary buffer used to store formated time */

  /* Device variables */
  struct mem_allocator* allocator = NULL;
  size_t nthreads = 0;

  /* Stardis variables */
  struct sdis_estimator* estimator = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_green_function** per_thread_green = NULL;

  /* Random number generator */
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** per_thread_rng = NULL;

  /* Miscellaneous */
  struct darray_enclosure_cumul cumul;
  struct accum* per_thread_acc_temp = NULL;
  struct accum* per_thread_acc_time = NULL;
  size_t nrealisations = 0;
  int64_t irealisation;
  int32_t* progress = NULL; /* Per process progress bar */
  int pcent_progress = 1; /* Percentage requiring progress update */
  int is_master_process = 1;
  int cumul_is_init = 0;
  int register_paths = SDIS_HEAT_PATH_NONE;
  ATOMIC nsolved_realisations = 0;
  ATOMIC res = RES_OK;

  if(!scn) { res = RES_BAD_ARG; goto error; }
  if(!out_estimator && !out_green) { res = RES_BAD_ARG; goto error; }
  res = check_solve_medium_args(args);
  if(res != RES_OK) goto error;
  res = XD(scene_check_dimensionality)(scn);
  if(res != RES_OK) goto error;

  if(out_green && args->picard_order != 1) {
    log_err(scn->dev, "%s: the evaluation of the green function does not make "
      "sense when dealing with the non-linearities of the system; i.e. picard "
      "order must be set to 1 while it is currently set to %lu.\n",
      FUNC_NAME, (unsigned long)args->picard_order);
    res = RES_BAD_ARG;
    goto error;
  }

#ifdef SDIS_ENABLE_MPI
  is_master_process = !scn->dev->use_mpi || scn->dev->mpi_rank == 0;
#endif

  nthreads = scn->dev->nthreads;
  allocator = scn->dev->allocator;

  /* Update the progress bar every percent if escape sequences are allowed in
   * log messages or only every 10 percent when only plain text is allowed.
   * This reduces the number of lines of plain text printed */
  pcent_progress = scn->dev->no_escape_sequence ? 10 : 1;

  /* Create the per thread RNGs */
  res = create_per_thread_rng
    (scn->dev, args->rng_state, args->rng_type, &rng_proxy, &per_thread_rng);
  if(res != RES_OK) goto error;

  /* Allocate the per process progress status */
  res = alloc_process_progress(scn->dev, &progress);
  if(res != RES_OK) goto error;

  /* Create the per thread accumulators */
  per_thread_acc_temp = MEM_CALLOC(allocator, nthreads, sizeof(struct accum));
  per_thread_acc_time = MEM_CALLOC(allocator, nthreads, sizeof(struct accum));
  if(!per_thread_acc_temp) { res = RES_MEM_ERR; goto error; }
  if(!per_thread_acc_time) { res = RES_MEM_ERR; goto error; }

  /* Compute the enclosure cumulative */
  darray_enclosure_cumul_init(scn->dev->allocator, &cumul);
  cumul_is_init = 1;
  res = compute_medium_enclosure_cumulative(scn, args->medium, &cumul);
  if(res != RES_OK) goto error;

  if(out_green) {
    res = create_per_thread_green_function
      (scn, args->signature, &per_thread_green);
    if(res != RES_OK) goto error;
  }

  /* Create the estimator on the master process only. No estimator is needed
   * for non master process */
  if(out_estimator && is_master_process) {
    res = estimator_create(scn->dev, SDIS_ESTIMATOR_TEMPERATURE, &estimator);
    if(res != RES_OK) goto error;
  }

  /* Synchronise the processes */
  process_barrier(scn->dev);

  #define PROGRESS_MSG "Solving medium temperature: "
  print_progress(scn->dev, progress, PROGRESS_MSG);

  /* Begin time registration of the computation */
  time_current(&time0);

  /* Here we go! Launch the Monte Carlo estimation */
  nrealisations = compute_process_realisations_count(scn->dev, args->nrealisations);
  register_paths = out_estimator && is_master_process
    ? args->register_paths : SDIS_HEAT_PATH_NONE;
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    struct probe_realisation_args realis_args = PROBE_REALISATION_ARGS_NULL;
    struct time t0, t1;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = per_thread_rng[ithread];
    struct accum* acc_temp = &per_thread_acc_temp[ithread];
    struct accum* acc_time = &per_thread_acc_time[ithread];
    struct green_path_handle* pgreen_path = NULL;
    struct green_path_handle green_path = GREEN_PATH_HANDLE_NULL;
    struct sdis_heat_path* pheat_path = NULL;
    struct sdis_heat_path heat_path;
    double weight;
    double time;
    double pos[DIM];
    size_t n;
    unsigned enc_id = ENCLOSURE_ID_NULL;
    int pcent;
    res_T res_local = RES_OK;
    res_T res_simul = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    time_current(&t0);

    time = sample_time(rng, args->time_range);
    if(out_green) {
      res_local = green_function_create_path(per_thread_green[ithread], &green_path);
      if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); goto error_it; }
      pgreen_path = &green_path;
    }

    if(register_paths) {
      heat_path_init(scn->dev->allocator, &heat_path);
      pheat_path = &heat_path;
    }

    /* Uniformly Sample an enclosure that surround the submitted medium and
     * uniformly sample a position into it */
    enc_id = sample_medium_enclosure(&cumul, rng);
    res_local = XD(sample_enclosure_position)(scn, enc_id, rng, pos);
    if(res_local != RES_OK) {
      log_err(scn->dev, "%s: could not sample a medium position.\n", FUNC_NAME);
      ATOMIC_SET(&res, res_local);
      goto error_it;
    }

    /* Run a probe realisation */
    realis_args.rng = rng;
    realis_args.enc_id = enc_id;
    realis_args.time = time;
    realis_args.picard_order = args->picard_order;
    realis_args.green_path = pgreen_path;
    realis_args.heat_path = pheat_path;
    realis_args.irealisation = (size_t)irealisation;
    realis_args.diff_algo = args->diff_algo;
    dX(set)(realis_args.position, pos);
    res_simul = XD(probe_realisation)(scn, &realis_args, &weight);
    if(res_simul != RES_OK && res_simul != RES_BAD_OP) {
      ATOMIC_SET(&res, res_simul);
      goto error_it;
    }

    /* Finalize the registered path */
    if(pheat_path) {
      pheat_path->status = res_simul == RES_OK
        ? SDIS_HEAT_PATH_SUCCESS
        : SDIS_HEAT_PATH_FAILURE;

      /* Check if the path must be saved regarding the register_paths mask */
      if(!(register_paths & (int)pheat_path->status)) {
        heat_path_release(pheat_path);
        pheat_path = NULL;
      } else { /* Register the sampled path */
        res_local = estimator_add_and_release_heat_path(estimator, pheat_path);
        if(res_local != RES_OK) {
          ATOMIC_SET(&res, res_local);
          goto error_it;
        }
      }
      pheat_path = NULL;
    }

    /* Stop time registration */
    time_sub(&t0, time_current(&t1), &t0);

    /* Update accumulators */
    if(res_simul == RES_OK) {
      const double usec = (double)time_val(&t0, TIME_NSEC) * 0.001;
      acc_temp->sum += weight; acc_temp->sum2 += weight*weight; ++acc_temp->count;
      acc_time->sum += usec;   acc_time->sum2 += usec*usec;     ++acc_time->count;
    }

    /* Update progress */
    n = (size_t)ATOMIC_INCR(&nsolved_realisations);
    pcent = (int)((double)n * 100.0 / (double)nrealisations + 0.5/*round*/);
    #pragma omp critical
    if(pcent/pcent_progress > progress[0]/pcent_progress) {
      progress[0] = pcent;
      print_progress_update(scn->dev, progress, PROGRESS_MSG);
    }
  exit_it:
    if(pheat_path) heat_path_release(pheat_path);
    continue;
  error_it:
    goto exit_it;
  }
  /* Synchronise processes */
  process_barrier(scn->dev);

  res = gather_res_T(scn->dev, (res_T)res);
  if(res != RES_OK) goto error;

  print_progress_completion(scn->dev, progress, PROGRESS_MSG);
  #undef PROGRESS_MSG

  /* Report computation time */
  time_sub(&time0, time_current(&time1), &time0);
  time_dump(&time0, TIME_ALL, NULL, buf, sizeof(buf));
  log_info(scn->dev, "Medium temperature solved in %s.\n", buf);

  /* Gather the RNG proxy sequence IDs and ensure that the RNG proxy state of
   * the master process is greater than the RNG proxy state of all other
   * processes */
  res = gather_rng_proxy_sequence_id(scn->dev, rng_proxy);
  if(res != RES_OK) goto error;

  /* Setup the estimated temperature */
  if(out_estimator) {
    struct accum acc_temp;
    struct accum acc_time;

    time_current(&time0);

    #define GATHER_ACCUMS(Msg, Acc) {                                          \
      res = gather_accumulators(scn->dev, Msg, per_thread_##Acc, &Acc);        \
      if(res != RES_OK) goto error;                                            \
    } (void)0
    GATHER_ACCUMS(MPI_SDIS_MSG_ACCUM_TEMP, acc_temp);
    GATHER_ACCUMS(MPI_SDIS_MSG_ACCUM_TIME, acc_time);
    #undef GATHER_ACCUMS

    time_sub(&time0, time_current(&time1), &time0);
    time_dump(&time0, TIME_ALL, NULL, buf, sizeof(buf));
    log_info(scn->dev, "Accumulators gathered in %s.\n",  buf);

    /* Return an estimator only on master process */
    if(is_master_process) {
      ASSERT(acc_temp.count == acc_time.count);
      estimator_setup_realisations_count(estimator, args->nrealisations, acc_temp.count);
      estimator_setup_temperature(estimator, acc_temp.sum, acc_temp.sum2);
      estimator_setup_realisation_time(estimator, acc_time.sum, acc_time.sum2);
      res = estimator_save_rng_state(estimator, rng_proxy);
      if(res != RES_OK) goto error;
    }
  }

  if(out_green) {
    time_current(&time0);

    res = gather_green_functions
      (scn, rng_proxy, per_thread_green, per_thread_acc_time, &green);
    if(res != RES_OK) goto error;

    time_sub(&time0, time_current(&time1), &time0);
    time_dump(&time0, TIME_ALL, NULL, buf, sizeof(buf));
    log_info(scn->dev, "Green functions gathered in %s.\n", buf);

    /* Return a green function only on master process */
    if(!is_master_process) {
      SDIS(green_function_ref_put(green));
      green = NULL;
    }
  }

exit:
  if(per_thread_rng) release_per_thread_rng(scn->dev, per_thread_rng);
  if(per_thread_green) release_per_thread_green_function(scn, per_thread_green);
  if(progress) free_process_progress(scn->dev, progress);
  if(per_thread_acc_temp) MEM_RM(scn->dev->allocator, per_thread_acc_temp);
  if(per_thread_acc_time) MEM_RM(scn->dev->allocator, per_thread_acc_time);
  if(cumul_is_init) darray_enclosure_cumul_release(&cumul);
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_estimator) *out_estimator = estimator;
  if(out_green) *out_green = green;
  return (res_T)res;
error:
  if(green) { SDIS(green_function_ref_put(green)); green = NULL; }
  if(estimator) { SDIS(estimator_ref_put(estimator)); estimator = NULL; }
  goto exit;
}

static res_T
XD(compute_power)
  (struct sdis_scene* scn,
   const struct sdis_compute_power_args* args,
   struct sdis_estimator** out_estimator)
{
  /* Time registration */
  struct time time0, time1;
  char buf[128]; /* Temporary buffer used to store formated time */

  /* Device variables */
  struct mem_allocator* allocator = NULL;
  size_t nthreads = 0;

  /* Stardis variables */
  struct sdis_estimator* estimator = NULL;

  /* Random number generator */
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** per_thread_rng = NULL;

  /* Miscellaneous */
  struct darray_enclosure_cumul cumul;
  struct accum* per_thread_acc_mpow = NULL;
  struct accum* per_thread_acc_time = NULL;
  double spread = 0;
  size_t nrealisations = 0;
  int64_t irealisation = 0;
  int32_t* progress = NULL; /* Per process progress bar */
  int cumul_is_init = 0;
  int is_master_process = 1;
  ATOMIC nsolved_realisations = 0;
  ATOMIC res = RES_OK;

  if(!scn) { res = RES_BAD_ARG; goto error; }
  if(!out_estimator) { res = RES_BAD_ARG; goto error; }
  res = check_compute_power_args(args);
  if(res != RES_OK) goto error;
  res = XD(scene_check_dimensionality)(scn);
  if(res != RES_OK) goto error;

  if(sdis_medium_get_type(args->medium) != SDIS_SOLID) {
    log_err(scn->dev, "Could not compute mean power on a non solid medium.\n");
    res = RES_BAD_ARG;
    goto error;
  }

#ifdef SDIS_ENABLE_MPI
  is_master_process = !scn->dev->use_mpi || scn->dev->mpi_rank == 0;
#endif

  nthreads = scn->dev->nthreads;
  allocator = scn->dev->allocator;

  /* Create the per thread RNGs */
  res = create_per_thread_rng
    (scn->dev, args->rng_state, args->rng_type, &rng_proxy, &per_thread_rng);
  if(res != RES_OK) goto error;

  /* Allocate the per process progress status */
  res = alloc_process_progress(scn->dev, &progress);
  if(res != RES_OK) goto error;

  /* Create the per thread accumulators */
  per_thread_acc_mpow = MEM_CALLOC(allocator, nthreads, sizeof(struct accum));
  per_thread_acc_time = MEM_CALLOC(allocator, nthreads, sizeof(struct accum));
  if(!per_thread_acc_mpow) { res = RES_MEM_ERR; goto error; }
  if(!per_thread_acc_time) { res = RES_MEM_ERR; goto error; }

  /* Compute the cumulative of the spreads of the enclosures surrounding the
   * submitted medium */
  darray_enclosure_cumul_init(scn->dev->allocator, &cumul);
  cumul_is_init = 1;
  res = compute_medium_enclosure_cumulative(scn, args->medium, &cumul);
  if(res != RES_OK) goto error;

  /* Fetch the overall medium spread from the unormalized enclosure cumulative */
  spread = darray_enclosure_cumul_cdata_get(&cumul)
    [darray_enclosure_cumul_size_get(&cumul)-1].cumul;

  /* Create the estimator on the master process only. No estimator is needed
   * for non master process */
  if(is_master_process) {
    res = estimator_create(scn->dev, SDIS_ESTIMATOR_POWER, &estimator);
    if(res != RES_OK) goto error;
  }

  /* Synchronise the processes */
  process_barrier(scn->dev);

  #define PROGRESS_MSG "Computing mean power: "
  print_progress(scn->dev, progress, PROGRESS_MSG);

  /* Begin time registration of the computation */
  time_current(&time0);

  /* Here we go! Launch the Monte Carlo estimation */
  nrealisations = compute_process_realisations_count(scn->dev, args->nrealisations);
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    struct time t0, t1;
    struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = per_thread_rng[ithread];
    struct accum* acc_mpow = &per_thread_acc_mpow[ithread];
    struct accum* acc_time = &per_thread_acc_time[ithread];
    double power = 0;
    double usec = 0;
    size_t n = 0;
    unsigned enc_id = ENCLOSURE_ID_NULL;
    int pcent = 0;
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    /* Begin time registration */
    time_current(&t0);

    /* Sample the time */
    vtx.time = sample_time(rng, args->time_range);

    /* Uniformly Sample an enclosure that surround the submitted medium and
     * uniformly sample a position into it */
    enc_id = sample_medium_enclosure(&cumul, rng);
    res_local = XD(sample_enclosure_position)(scn, enc_id, rng, vtx.P);
    if(res_local != RES_OK) {
      log_err(scn->dev, "%s: could not sample a medium position.\n", FUNC_NAME);
      ATOMIC_SET(&res, res_local);
      goto error_it;
    }

    /* Fetch the volumic power */
    power = solid_get_volumic_power(args->medium, &vtx);

    /* Stop time registration */
    time_sub(&t0, time_current(&t1), &t0);
    usec = (double)time_val(&t0, TIME_NSEC) * 0.001;

    /* Update accumulators */
    acc_mpow->sum += power; acc_mpow->sum2 += power*power; ++acc_mpow->count;
    acc_time->sum += usec;  acc_time->sum2 += usec*usec;   ++acc_time->count;

    /* Update progress */
    n = (size_t)ATOMIC_INCR(&nsolved_realisations);
    pcent = (int)((double)n * 100.0 / (double)nrealisations + 0.5/*round*/);
    #pragma omp critical
    if(pcent > progress[0]) {
      progress[0] = pcent;
      print_progress_update(scn->dev, progress, PROGRESS_MSG);
    }
  exit_it:
    continue;
  error_it:
    goto exit_it;
  }
  /* Synchronise the processes */
  process_barrier(scn->dev);

  res = gather_res_T(scn->dev, (res_T)res);
  if(res != RES_OK) goto error;

  print_progress_update(scn->dev, progress, PROGRESS_MSG);
  log_info(scn->dev, "\n");
  #undef PROGRESS_MSG

  /* Report computation time */
  time_sub(&time0, time_current(&time1), &time0);
  time_dump(&time0, TIME_ALL, NULL, buf, sizeof(buf));
  log_info(scn->dev, "Mean power computed in in %s.\n", buf);

  /* Gather the RNG proxy sequence IDs and ensure that the RNG proxy state of
   * the master process is greater than the RNG proxy state of all other
   * processes */
  res = gather_rng_proxy_sequence_id(scn->dev, rng_proxy);
  if(res != RES_OK) goto error;

  /* Setup the estimated mean power */
  {
    struct accum acc_mpow;
    struct accum acc_time;

    time_current(&time0);

    #define GATHER_ACCUMS(Msg, Acc) {                                          \
      res = gather_accumulators(scn->dev, Msg, per_thread_##Acc, &Acc);        \
      if(res != RES_OK) goto error;                                            \
    } (void)0
    GATHER_ACCUMS(MPI_SDIS_MSG_ACCUM_MEAN_POWER, acc_mpow);
    GATHER_ACCUMS(MPI_SDIS_MSG_ACCUM_TIME, acc_time);
    #undef GATHER_ACCUMS

    time_sub(&time0, time_current(&time1), &time0);
    time_dump(&time0, TIME_ALL, NULL, buf, sizeof(buf));
    log_info(scn->dev, "Accumulators gathered in %s.\n",  buf);

    /* Return an estimator only on master process */
    if(is_master_process) {
      ASSERT(acc_mpow.count == acc_time.count);
      estimator_setup_realisations_count(estimator, args->nrealisations, acc_mpow.count);
      estimator_setup_realisation_time(estimator, acc_time.sum, acc_time.sum2);
      estimator_setup_power
        (estimator, acc_mpow.sum, acc_mpow.sum2, spread, args->time_range);
      res = estimator_save_rng_state(estimator, rng_proxy);
      if(res != RES_OK) goto error;
    }
  }

exit:
  if(per_thread_rng) release_per_thread_rng(scn->dev, per_thread_rng);
  if(progress) free_process_progress(scn->dev, progress);
  if(per_thread_acc_mpow) MEM_RM(scn->dev->allocator, per_thread_acc_mpow);
  if(per_thread_acc_time) MEM_RM(scn->dev->allocator, per_thread_acc_time);
  if(cumul_is_init) darray_enclosure_cumul_release(&cumul);
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_estimator) *out_estimator = estimator;
  return (res_T)res;
error:
  if(estimator) { SDIS(estimator_ref_put(estimator)); estimator = NULL; }
  goto exit;
}

#include "sdis_Xd_end.h"
