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

#include "sdis_device_c.h"
#include "sdis_estimator_c.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_realisation.h"
#include "sdis_scene_c.h"

#include <rsys/clock_time.h>
#include <star/ssp.h>
#include <omp.h>

#include "sdis_Xd_begin.h"

/*******************************************************************************
 * Helper function
 ******************************************************************************/
#ifndef SDIS_SOLVE_PROBE_BOUNDARY_XD_H
#define SDIS_SOLVE_PROBE_BOUNDARY_XD_H

static INLINE res_T
check_solve_probe_boundary_args
  (const struct sdis_solve_probe_boundary_args* args)
{
  if(!args) return RES_BAD_ARG;

  /* Check #realisations */
  if(!args->nrealisations || args->nrealisations > INT64_MAX) {
    return RES_BAD_ARG;
  }

  /* Check side */
  if((unsigned)args->side >= SDIS_SIDE_NULL__) {
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

  return RES_OK;
}

#endif /* SDIS_SOLVE_PROBE_BOUNDARY_XD_H */

/*******************************************************************************
 * Local functions
 ******************************************************************************/
static res_T
XD(solve_probe_boundary)
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_args* args,
   struct sdis_green_function** out_green,
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
  struct sdis_green_function* green = NULL;
  struct sdis_green_function** per_thread_green = NULL;

  /* Random number generator */
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** per_thread_rng = NULL;

  /* Miscellaneous */
  struct accum* per_thread_acc_temp = NULL;
  struct accum* per_thread_acc_time = NULL;
  size_t nrealisations = 0;
  int64_t irealisation = 0;
  int32_t* progress = NULL; /* Per process progress bar */
  int register_paths = SDIS_HEAT_PATH_NONE;
  int is_master_process = 1;
  ATOMIC nsolved_realisations = 0;
  ATOMIC res = RES_OK;

  if(!scn) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = check_solve_probe_boundary_args(args);
  if(res != RES_OK) goto error;

  if(!out_estimator && !out_green) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(out_green && args->picard_order != 1) {
    log_err(scn->dev, "%s: the evaluation of the green function does not make "
      "sense when dealing with the non-linearities of the system; i.e. picard "
      "order must be set to 1 while it is currently set to %lu.\n",
      FUNC_NAME, (unsigned long)args->picard_order);
    res = RES_BAD_ARG;
    goto error;
  }

#if SDIS_XD_DIMENSION == 2
  if(scene_is_2d(scn) == 0) { res = RES_BAD_ARG; goto error; }
#else
  if(scene_is_2d(scn) != 0) { res = RES_BAD_ARG; goto error; }
#endif

  /* Check the primitive identifier */
  if(args->iprim >= scene_get_primitives_count(scn)) {
    log_err(scn->dev,
      "%s: invalid primitive identifier `%lu'. "
      "It must be in the [0 %lu] range.\n",
      FUNC_NAME,
      (unsigned long)args->iprim,
      (unsigned long)scene_get_primitives_count(scn)-1);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check parametric coordinates */
#if SDIS_XD_DIMENSION  == 2
  {
    const double v = CLAMP(1.0 - args->uv[0], 0, 1);
    if(args->uv[0] < 0 || args->uv[0] > 1 || !eq_eps(args->uv[0]+v, 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates %g."
        "u + (1-u) must be equal to 1 with u [0, 1].\n",
        FUNC_NAME, args->uv[0]);
      res = RES_BAD_ARG;
      goto error;
    }
  }
#else /* SDIS_XD_DIMENSION == 3 */
  {
    const double w = CLAMP(1 - args->uv[0] - args->uv[1], 0, 1);
    if(args->uv[0] < 0 || args->uv[1] < 0 || args->uv[0] > 1 || args->uv[1] > 1
    || !eq_eps(w + args->uv[0] + args->uv[1], 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates [%g, %g]. "
        "u + v + (1-u-v) must be equal to 1 with u and v in [0, 1].\n",
        FUNC_NAME, args->uv[0], args->uv[1]);
      res = RES_BAD_ARG;
      goto error;
    }
  }
#endif

#ifdef SDIS_ENABLE_MPI
  is_master_process = !scn->dev->use_mpi || scn->dev->mpi_rank == 0;
#endif

  nthreads = scn->dev->nthreads;
  allocator = scn->dev->allocator;

  /* Create the per thread RNGs */
  res = create_per_thread_rng
    (scn->dev, args->rng_state, &rng_proxy, &per_thread_rng);
  if(res != RES_OK) goto error;

  /* Allocate the per process progress status */
  res = alloc_process_progress(scn->dev, &progress);
  if(res != RES_OK) goto error;

  /* Create the per thread accumulators */
  per_thread_acc_temp = MEM_CALLOC(allocator, nthreads, sizeof(struct accum));
  per_thread_acc_time = MEM_CALLOC(allocator, nthreads, sizeof(struct accum));
  if(!per_thread_acc_temp) { res = RES_MEM_ERR; goto error; }
  if(!per_thread_acc_time) { res = RES_MEM_ERR; goto error; }

  /* Create the per thread green function */
  if(out_green) {
    res = create_per_thread_green_function(scn, &per_thread_green);
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

  #define PROGRESS_MSG "Solving surface probe temperature: "
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
    struct boundary_realisation_args realis_args = BOUNDARY_REALISATION_ARGS_NULL;
    struct time t0, t1;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = per_thread_rng[ithread];
    struct accum* acc_temp = &per_thread_acc_temp[ithread];
    struct accum* acc_time = &per_thread_acc_time[ithread];
    struct green_path_handle* pgreen_path = NULL;
    struct green_path_handle green_path = GREEN_PATH_HANDLE_NULL;
    struct sdis_heat_path* pheat_path = NULL;
    struct sdis_heat_path heat_path;
    double w = NaN;
    double time;
    size_t n;
    int pcent;
    res_T res_local = RES_OK;
    res_T res_simul = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    /* Begin time registration */
    time_current(&t0);

    time = sample_time(rng, args->time_range);
    if(out_green) {
      res_local = green_function_create_path
        (per_thread_green[ithread], &green_path);
      if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); goto error_it; }
      pgreen_path = &green_path;
    }

    if(register_paths) {
      heat_path_init(scn->dev->allocator, &heat_path);
      pheat_path = &heat_path;
    }

    /* Invoke the boundary realisation */
    realis_args.rng = rng;
    realis_args.iprim = args->iprim;
    realis_args.time = time;
    realis_args.picard_order = args->picard_order;
    realis_args.side = args->side;
    realis_args.green_path = pgreen_path;
    realis_args.heat_path = pheat_path;
    realis_args.uv[0] = args->uv[0];
#if SDIS_XD_DIMENSION == 3
    realis_args.uv[1] = args->uv[1];
#endif
    res_simul = XD(boundary_realisation)(scn, &realis_args, &w);

    /* Handle fatal error */
    if(res_simul != RES_OK && res_simul != RES_BAD_OP) {
      ATOMIC_SET(&res, res_simul);
      goto error_it;
    }

    if(pheat_path) {
      pheat_path->status = res_simul == RES_OK
        ? SDIS_HEAT_PATH_SUCCESS
        : SDIS_HEAT_PATH_FAILURE;

      /* Check if the path must be saved regarding the register_paths mask */
      if(!(register_paths & (int)pheat_path->status)) {
        heat_path_release(pheat_path);
        pheat_path = NULL;
      } else {
        /* Register the sampled path */
        res_local = estimator_add_and_release_heat_path(estimator, pheat_path);
        if(res_local != RES_OK) {
          ATOMIC_SET(&res, res_local);
          goto error_it;
        }
        pheat_path = NULL;
      }
    }

    /* Stop time registration */
    time_sub(&t0, time_current(&t1), &t0);

    /* Update accumulators */
    if(res_simul == RES_OK) {
      const double usec = (double)time_val(&t0, TIME_NSEC) * 0.001;
      acc_temp->sum += w;    acc_temp->sum2 += w*w;       ++acc_temp->count;
      acc_time->sum += usec; acc_time->sum2 += usec*usec; ++acc_time->count;
    }

    /* Update progress */
    n = (size_t)ATOMIC_INCR(&nsolved_realisations);
    pcent = (int)((double)n * 100.0 / (double)nrealisations + 0.5/*round*/);
    #pragma omp critical
    if(pcent > progress[0]) {
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

  print_progress_update(scn->dev, progress, PROGRESS_MSG);
  log_info(scn->dev, "\n");
  #undef PROGRESS_MSG

  /* Report computation time */
  time_sub(&time0, time_current(&time1), &time0);
  time_dump(&time0, TIME_ALL, NULL, buf, sizeof(buf));
  log_info(scn->dev, "Surface probe temperature solved in %s.\n", buf);

  /* Gather the RNG proxy sequence IDs and ensure that the RNG proxy state of
   * the master process is greater than the RNG proxy state of all other
   * processes */
  res = gather_rng_proxy_sequence_id(scn->dev, rng_proxy);
  if(res != RES_OK) goto error;

  /* Setup the estimated temperature and per realisation time */
  if(out_estimator) {
    struct accum acc_temp, acc_time;

    time_current(&time0);

    res = gather_accumulators
      (scn->dev, MPI_SDIS_MSG_ACCUM_TEMP, per_thread_acc_temp, &acc_temp);
    if(res != RES_OK) goto error;
    res = gather_accumulators
      (scn->dev, MPI_SDIS_MSG_ACCUM_TIME, per_thread_acc_time, &acc_time);
    if(res != RES_OK) goto error;

    time_sub(&time0, time_current(&time1), &time0);
    time_dump(&time0, TIME_ALL, NULL, buf, sizeof(buf));
    log_info(scn->dev, "Accumulators gathered in %s.\n",  buf);

    /* Return an estimator only on master process */
    if(is_master_process) {
      res = setup_estimator
        (estimator, rng_proxy, &acc_temp, &acc_time, args->nrealisations);
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
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_green) *out_green = green;
  if(out_estimator) *out_estimator = estimator;
  return (res_T)res;
error:
  if(estimator) { SDIS(estimator_ref_put(estimator)); estimator = NULL; }
  if(green) { SDIS(green_function_ref_put(green)); green = NULL; }
  goto exit;
}

static res_T
XD(solve_probe_boundary_flux)
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_flux_args* args,
   struct sdis_estimator** out_estimator)
{
  struct sdis_estimator* estimator = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  const struct sdis_interface* interf;
  const struct sdis_medium *fmd, *bmd;
  enum sdis_side solid_side, fluid_side;
  struct sdis_interface_fragment frag;
  struct accum* acc_tp = NULL; /* Per thread temperature accumulator */
  struct accum* acc_ti = NULL; /* Per thread realisation time */
  struct accum* acc_fl = NULL; /* Per thread flux accumulator */
  struct accum* acc_fc = NULL; /* Per thread convective flux accumulator */
  struct accum* acc_fr = NULL; /* Per thread radiative flux accumulator */
  struct accum* acc_fi = NULL; /* Per thread imposed flux accumulator */
  size_t nrealisations = 0;
  int64_t irealisation = 0;
  size_t i;
  int progress = 0;
  ATOMIC nsolved_realisations = 0;
  ATOMIC res = RES_OK;

  if(!scn || !args || !args->nrealisations || args->nrealisations > INT64_MAX
  || args->time_range[0] < 0 || args->time_range[1] < args->time_range[0]
  || (args->time_range[1]>DBL_MAX && args->time_range[0] != args->time_range[1])
  || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

#if SDIS_XD_DIMENSION == 2
  if(scene_is_2d(scn) == 0) { res = RES_BAD_ARG; goto error; }
#else
  if(scene_is_2d(scn) != 0) { res = RES_BAD_ARG; goto error; }
#endif

  /* Check the primitive identifier */
  if(args->iprim >= scene_get_primitives_count(scn)) {
    log_err(scn->dev,
      "%s: invalid primitive identifier `%lu'. "
      "It must be in the [0 %lu] range.\n",
      FUNC_NAME,
      (unsigned long)args->iprim,
      (unsigned long)scene_get_primitives_count(scn)-1);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check parametric coordinates */
  if(scene_is_2d(scn)) {
    const double v = CLAMP(1.0 - args->uv[0], 0, 1);
    if(args->uv[0] < 0 || args->uv[0] > 1
    || !eq_eps(args->uv[0] + v, 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates %g. "
        "u + (1-u) must be equal to 1 with u [0, 1].\n",
        FUNC_NAME, args->uv[0]);
      res = RES_BAD_ARG;
      goto error;
    }
  } else {
    const double w = CLAMP(1 - args->uv[0] - args->uv[1], 0, 1);
    if(args->uv[0] < 0
    || args->uv[1] < 0
    || args->uv[0] > 1
    || args->uv[1] > 1
    || !eq_eps(w + args->uv[0] + args->uv[1], 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates [%g, %g]. "
        "u + v + (1-u-v) must be equal to 1 with u and v in [0, 1].\n",
        FUNC_NAME, args->uv[0], args->uv[1]);
      res = RES_BAD_ARG;
      goto error;
    }
  }
  /* Check medium is fluid on one side and solid on the other */
  interf = scene_get_interface(scn, (unsigned)args->iprim);
  fmd = interface_get_medium(interf, SDIS_FRONT);
  bmd = interface_get_medium(interf, SDIS_BACK);
  if(!fmd || !bmd
  || (  !(fmd->type == SDIS_FLUID && bmd->type == SDIS_SOLID)
     && !(fmd->type == SDIS_SOLID && bmd->type == SDIS_FLUID))) {
    log_err(scn->dev,
      "%s: Attempt to compute a flux at a %s-%s interface.\n",
      FUNC_NAME,
      (!fmd ? "undefined" : (fmd->type == SDIS_FLUID ? "fluid" : "solid")),
      (!bmd ? "undefined" : (bmd->type == SDIS_FLUID ? "fluid" : "solid")));
    res = RES_BAD_ARG;
    goto error;
  }
  solid_side = (fmd->type == SDIS_SOLID) ? SDIS_FRONT : SDIS_BACK;
  fluid_side = (fmd->type == SDIS_FLUID) ? SDIS_FRONT : SDIS_BACK;

  /* Create the proxy RNG */
  if(args->rng_state) {
    res = ssp_rng_proxy_create_from_rng(scn->dev->allocator, args->rng_state,
      scn->dev->nthreads, &rng_proxy);
    if(res != RES_OK) goto error;
  } else {
    res = ssp_rng_proxy_create(scn->dev->allocator, SSP_RNG_MT19937_64,
      scn->dev->nthreads, &rng_proxy);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread RNG */
  rngs = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(struct ssp_rng*));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs + i);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread accumulator */
  #define ALLOC_ACCUMS(Dst) {                                                  \
    Dst = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*Dst));   \
    if(!Dst) { res = RES_MEM_ERR; goto error; }                                \
  } (void)0
  ALLOC_ACCUMS(acc_tp);
  ALLOC_ACCUMS(acc_ti);
  ALLOC_ACCUMS(acc_fc);
  ALLOC_ACCUMS(acc_fl);
  ALLOC_ACCUMS(acc_fr);
  ALLOC_ACCUMS(acc_fi);
  #undef ALLOC_ACCUMS

  /* Prebuild the interface fragment */
  res = XD(build_interface_fragment)
    (&frag, scn, (unsigned)args->iprim, args->uv, fluid_side);
  if(res != RES_OK) goto error;

  /* Create the estimator */
  res = estimator_create(scn->dev, SDIS_ESTIMATOR_FLUX, &estimator);
  if(res != RES_OK) goto error;

  /* Here we go! Launch the Monte Carlo estimation */
  nrealisations = args->nrealisations;
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    struct boundary_flux_realisation_args realis_args =
      BOUNDARY_FLUX_REALISATION_ARGS_NULL;
    struct time t0, t1;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    struct accum* acc_temp = &acc_tp[ithread];
    struct accum* acc_time = &acc_ti[ithread];
    struct accum* acc_flux = &acc_fl[ithread];
    struct accum* acc_fcon = &acc_fc[ithread];
    struct accum* acc_frad = &acc_fr[ithread];
    struct accum* acc_fimp = &acc_fi[ithread];
    double time, epsilon, hc, hr, imposed_flux, imposed_temp;
    int flux_mask = 0;
    struct bound_flux_result result = BOUND_FLUX_RESULT_NULL__;
    double Tref = -1;
    size_t n;
    int pcent;
    res_T res_simul = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    /* Begin time registration */
    time_current(&t0);

    time = sample_time(rng, args->time_range);

    /* Compute hr and hc */
    frag.time = time;
    frag.side = fluid_side;
    epsilon = interface_side_get_emissivity(interf, &frag);
    Tref = interface_side_get_reference_temperature(interf, &frag);
    hc = interface_get_convection_coef(interf, &frag);
    hr = 4.0 * BOLTZMANN_CONSTANT * Tref * Tref * Tref * epsilon;
    frag.side = solid_side;
    imposed_flux = interface_side_get_flux(interf, &frag);
    imposed_temp = interface_side_get_temperature(interf, &frag);
    if(imposed_temp >= 0) {
      /* Flux computation on T boundaries is not supported yet */
      log_err(scn->dev,"%s: Attempt to compute a flux at a Dirichlet boundary "
        "(not available yet).\n",  FUNC_NAME);
      ATOMIC_SET(&res, RES_BAD_ARG);
      continue;
    }

    /* Fluid, Radiative and Solid temperatures */
    flux_mask = 0;
    if(hr > 0) flux_mask |= FLUX_FLAG_RADIATIVE;
    if(hc > 0) flux_mask |= FLUX_FLAG_CONVECTIVE;

    /* Invoke the boundary flux realisation */
    realis_args.rng = rng;
    realis_args.iprim = args->iprim;
    realis_args.time = time;
    realis_args.picard_order = args->picard_order;
    realis_args.solid_side = solid_side;
    realis_args.flux_mask = flux_mask;
    realis_args.uv[0] = args->uv[0];
#if SDIS_XD_DIMENSION == 3
    realis_args.uv[1] = args->uv[1];
#endif
    res_simul = XD(boundary_flux_realisation)(scn, &realis_args, &result);

    /* Stop time registration */
    time_sub(&t0, time_current(&t1), &t0);

    if(res_simul != RES_OK && res_simul != RES_BAD_OP) {
      ATOMIC_SET(&res, res_simul);
      continue;
    } else if(res_simul == RES_OK) { /* Update accumulators */
      const double usec = (double)time_val(&t0, TIME_NSEC) * 0.001;
      const double w_conv = hc * (result.Tboundary - result.Tfluid);
      const double w_rad = (result.Tradiative < 0) ?
        0 : hr * (result.Tboundary - result.Tradiative);
      const double w_imp = (imposed_flux != SDIS_FLUX_NONE) ? imposed_flux : 0;
      const double w_total = w_conv + w_rad + w_imp;
      /* Temperature */
      acc_temp->sum += result.Tboundary;
      acc_temp->sum2 += result.Tboundary*result.Tboundary;
      ++acc_temp->count;
      /* Time */
      acc_time->sum += usec;
      acc_time->sum2 += usec*usec;
      ++acc_time->count;
      /* Overwall flux */
      acc_flux->sum += w_total;
      acc_flux->sum2 += w_total*w_total;
      ++acc_flux->count;
      /* Convective flux */
      acc_fcon->sum  += w_conv;
      acc_fcon->sum2 += w_conv*w_conv;
      ++acc_fcon->count;
      /* Radiative flux */
      acc_frad->sum += w_rad;
      acc_frad->sum2 += w_rad*w_rad;
      ++acc_frad->count;
      /* Imposed flux */
      acc_fimp->sum += w_imp;
      acc_fimp->sum2 += w_imp*w_imp;
      ++acc_fimp->count;
    }

    /* Update progress */
    n = (size_t)ATOMIC_INCR(&nsolved_realisations);
    pcent = (int)((double)n * 100.0 / (double)nrealisations + 0.5/*round*/);
    #pragma omp critical
    if(pcent > progress) {
      progress = pcent;
      log_info(scn->dev, "Solving probe boundary flux: %3d%%\r", progress);
    }
  }
  if(res != RES_OK) goto error;

  /* Add a new line after the progress status */
  log_info(scn->dev, "Solving probe boundary flux: %3d%%\n", progress);

  /* Redux the per thread accumulators  */
  sum_accums(acc_tp, scn->dev->nthreads, &acc_tp[0]);
  sum_accums(acc_ti, scn->dev->nthreads, &acc_ti[0]);
  sum_accums(acc_fc, scn->dev->nthreads, &acc_fc[0]);
  sum_accums(acc_fr, scn->dev->nthreads, &acc_fr[0]);
  sum_accums(acc_fl, scn->dev->nthreads, &acc_fl[0]);
  sum_accums(acc_fi, scn->dev->nthreads, &acc_fi[0]);
  ASSERT(acc_tp[0].count == acc_fl[0].count);
  ASSERT(acc_tp[0].count == acc_ti[0].count);
  ASSERT(acc_tp[0].count == acc_fr[0].count);
  ASSERT(acc_tp[0].count == acc_fc[0].count);
  ASSERT(acc_tp[0].count == acc_fi[0].count);

  /* Setup the estimated values */
  estimator_setup_realisations_count(estimator, nrealisations, acc_tp[0].count);
  estimator_setup_temperature(estimator, acc_tp[0].sum, acc_tp[0].sum2);
  estimator_setup_realisation_time(estimator, acc_ti[0].sum, acc_ti[0].sum2);
  estimator_setup_flux(estimator, FLUX_CONVECTIVE, acc_fc[0].sum, acc_fc[0].sum2);
  estimator_setup_flux(estimator, FLUX_RADIATIVE, acc_fr[0].sum, acc_fr[0].sum2);
  estimator_setup_flux(estimator, FLUX_IMPOSED, acc_fi[0].sum, acc_fi[0].sum2);
  estimator_setup_flux(estimator, FLUX_TOTAL, acc_fl[0].sum, acc_fl[0].sum2);

  res = estimator_save_rng_state(estimator, rng_proxy);
  if(res != RES_OK) goto error;

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) {if(rngs[i]) SSP(rng_ref_put(rngs[i]));}
    MEM_RM(scn->dev->allocator, rngs);
  }
  if(acc_tp) MEM_RM(scn->dev->allocator, acc_tp);
  if(acc_ti) MEM_RM(scn->dev->allocator, acc_ti);
  if(acc_fc) MEM_RM(scn->dev->allocator, acc_fc);
  if(acc_fr) MEM_RM(scn->dev->allocator, acc_fr);
  if(acc_fl) MEM_RM(scn->dev->allocator, acc_fl);
  if(acc_fi) MEM_RM(scn->dev->allocator, acc_fi);
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_estimator) *out_estimator = estimator;
  return (res_T)res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

#include "sdis_Xd_end.h"
