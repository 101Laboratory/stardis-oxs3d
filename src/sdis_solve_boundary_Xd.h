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

#include "sdis_device_c.h"
#include "sdis_estimator_c.h"
#include "sdis_interface_c.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_realisation.h"
#include "sdis_scene_c.h"

#include <rsys/clock_time.h>
#include <rsys/rsys.h>
#include <star/ssp.h>
#include <omp.h>

#include "sdis_Xd_begin.h"

struct XD(boundary_context) {
  struct sXd(scene_view)* view;
  const size_t* primitives;
};
static const struct XD(boundary_context) XD(BOUNDARY_CONTEXT_NULL) = {
  NULL, NULL
};

/*******************************************************************************
 * Help functions
 ******************************************************************************/
static INLINE void
XD(boundary_get_indices)(const unsigned iprim, unsigned ids[DIM], void* context)
{
  unsigned i;
  (void)context;
  ASSERT(ids);
  FOR_EACH(i, 0, DIM) ids[i] = iprim*DIM + i;
}

static INLINE void
XD(boundary_get_position)(const unsigned ivert, float pos[DIM], void* context)
{
  struct XD(boundary_context)* ctx = context;
  struct sXd(primitive) prim;
  struct sXd(attrib) attr;
  const unsigned iprim_id = ivert / DIM;
  const unsigned iprim_vert = ivert % DIM;
  unsigned iprim;
  ASSERT(pos && context);

  iprim = (unsigned)ctx->primitives[iprim_id];
  SXD(scene_view_get_primitive(ctx->view, iprim, &prim));
#if DIM == 2
  s2d_segment_get_vertex_attrib(&prim, iprim_vert, SXD_POSITION, &attr);
#else
  s3d_triangle_get_vertex_attrib(&prim, iprim_vert, SXD_POSITION, &attr);
#endif
  ASSERT(attr.type == SXD_FLOATX);
  fX(set)(pos, attr.value);
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
static res_T
XD(solve_boundary)
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_args* args,
   struct sdis_green_function** out_green,
   struct sdis_estimator** out_estimator)
{
  struct XD(boundary_context) ctx = XD(BOUNDARY_CONTEXT_NULL);
  struct sXd(vertex_data) vdata = SXD_VERTEX_DATA_NULL;
  struct sXd(scene)* scene = NULL;
  struct sXd(shape)* shape = NULL;
  struct sXd(scene_view)* view = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_green_function** greens = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  struct accum* acc_temps = NULL;
  struct accum* acc_times = NULL;
  size_t nrealisations = 0;
  int64_t irealisation = 0;
  size_t i;
  size_t view_nprims;
  int progress = 0;
  int register_paths = SDIS_HEAT_PATH_NONE;
  ATOMIC nsolved_realisations = 0;
  ATOMIC res = RES_OK;

  if(!scn || !args || !args->nrealisations || args->nrealisations > INT64_MAX
  || !args->primitives || !args->sides || !args->nprimitives
  || args->fp_to_meter <= 0) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(!out_estimator && !out_green) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(out_estimator) {
    if(args->time_range[0] < 0
    || args->time_range[1] < args->time_range[0]
    || (  args->time_range[1] > DBL_MAX
       && args->time_range[0] != args->time_range[1])) {
      res = RES_BAD_ARG;
      goto error;
    }
  }

#if SDIS_XD_DIMENSION == 2
  if(scene_is_2d(scn) == 0) { res = RES_BAD_ARG; goto error; }
#else
  if(scene_is_2d(scn) != 0) { res = RES_BAD_ARG; goto error; }
#endif

  SXD(scene_view_primitives_count(scn->sXd(view), &view_nprims));
  FOR_EACH(i, 0, args->nprimitives) {
    if(args->primitives[i] >= view_nprims) {
      log_err(scn->dev,
        "%s: invalid primitive identifier `%lu'. It must be in the [0 %lu] range.\n",
        FUNC_NAME,
        (unsigned long)args->primitives[i],
        (unsigned long)scene_get_primitives_count(scn)-1);
      res = RES_BAD_ARG;
      goto error;
    }
    if((unsigned)args->sides[i] >= SDIS_SIDE_NULL__) {
      log_err(scn->dev,
        "%s: invalid side for the primitive `%lu'.\n",
        FUNC_NAME, (unsigned long)args->primitives[i]);
      res = RES_BAD_ARG;
      goto error;
    }
  }

  /* Create the Star-XD shape of the boundary */
#if SDIS_XD_DIMENSION == 2
  res = s2d_shape_create_line_segments(scn->dev->sXd(dev), &shape);
#else
  res = s3d_shape_create_mesh(scn->dev->sXd(dev), &shape);
#endif
  if(res != RES_OK) goto error;

  /* Initialise the boundary shape with the triangles/segments of the
   * submitted primitives  */
  ctx.primitives = args->primitives;
  ctx.view = scn->sXd(view);
  vdata.usage = SXD_POSITION;
  vdata.get = XD(boundary_get_position);
#if SDIS_XD_DIMENSION == 2
  vdata.type = S2D_FLOAT2;
  res = s2d_line_segments_setup_indexed_vertices(shape,
    (unsigned)args->nprimitives, boundary_get_indices_2d,
    (unsigned)(args->nprimitives*2), &vdata, 1, &ctx);
#else
  vdata.type = S3D_FLOAT3;
  res = s3d_mesh_setup_indexed_vertices(shape,
    (unsigned)args->nprimitives, boundary_get_indices_3d,
    (unsigned)(args->nprimitives*3), &vdata, 1, &ctx);
#endif
  if(res != RES_OK) goto error;

  /* Create and setup the boundary Star-XD scene */
  res = sXd(scene_create)(scn->dev->sXd(dev), &scene);
  if(res != RES_OK) goto error;
  res = sXd(scene_attach_shape)(scene, shape);
  if(res != RES_OK) goto error;
  res = sXd(scene_view_create)(scene, SXD_SAMPLE, &view);
  if(res != RES_OK) goto error;

  /* Create the proxy RNG */
  if(args->rng_state) {
    res = ssp_rng_proxy_create_from_rng(scn->dev->allocator, args->rng_state,
      scn->dev->nthreads, &rng_proxy);
    if(res != RES_OK) goto error;
  } else {
    res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
      scn->dev->nthreads, &rng_proxy);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread RNG */
  rngs = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*rngs));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs+i);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread accumulators */
  acc_temps = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(*acc_temps));
  if(!acc_temps) { res = RES_MEM_ERR; goto error; }
  acc_times = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(*acc_times));
  if(!acc_times) { res = RES_MEM_ERR; goto error; }

  /* Create the per thread green function */
  if(out_green) {
    greens = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*greens));
    if(!greens) { res = RES_MEM_ERR; goto error; }
    FOR_EACH(i, 0, scn->dev->nthreads) {
      res = green_function_create(scn, &greens[i]);
      if(res != RES_OK) goto error;
    }
  }

  /* Create the estimator */
  if(out_estimator) {
    res = estimator_create(scn->dev, SDIS_ESTIMATOR_TEMPERATURE, &estimator);
    if(res != RES_OK) goto error;
  }

  nrealisations = args->nrealisations;
  register_paths = out_estimator ? args->register_paths : SDIS_HEAT_PATH_NONE;
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation=0; irealisation<(int64_t)nrealisations; ++irealisation) {
    struct time t0, t1;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    struct accum* acc_temp = &acc_temps[ithread];
    struct accum* acc_time = &acc_times[ithread];
    struct green_path_handle* pgreen_path = NULL;
    struct green_path_handle green_path = GREEN_PATH_HANDLE_NULL;
    struct sdis_heat_path* pheat_path = NULL;
    struct sdis_heat_path heat_path;
    struct sXd(primitive) prim;
    enum sdis_side side;
    size_t iprim;
    double w = NaN;
    double uv[DIM-1];
    float st[DIM-1];
    double time;
    size_t n;
    int pcent;
    res_T res_local = RES_OK;
    res_T res_simul = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    /* Begin time registration */
    time_current(&t0);

    if(!out_green) {
      time = sample_time(rng, args->time_range);
      if(register_paths) {
        heat_path_init(scn->dev->allocator, &heat_path);
        pheat_path = &heat_path;
      }
    } else {
      /* Do not take care of the submitted time when registering the green
       * function. Simply takes 0 as relative time */
      time = 0;
      res_local = green_function_create_path(greens[ithread], &green_path);
      if(res_local != RES_OK) {
        ATOMIC_SET(&res, res_local);
        goto error_it;
      }
      pgreen_path = &green_path;
    }

    /* Sample a position onto the boundary */
#if SDIS_XD_DIMENSION == 2
    res_local = s2d_scene_view_sample
      (view,
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, st);
   uv[0] = (double)st[0];
#else
    res_local = s3d_scene_view_sample
      (view,
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, st);
    d2_set_f2(uv, st);
#endif
    if(res_local != RES_OK) {
      ATOMIC_SET(&res, res_local);
      goto error_it;
    }

    /* Map from boundary scene to sdis scene */
    ASSERT(prim.prim_id < args->nprimitives);
    iprim = args->primitives[prim.prim_id];
    side = args->sides[prim.prim_id];

    /* Invoke the boundary realisation */
    res_simul = XD(boundary_realisation)(scn, rng, iprim, uv, time, side,
      args->fp_to_meter, args->ambient_radiative_temperature,
      args->reference_temperature, pgreen_path, pheat_path, &w);

    /* Fatal error */
    if(res_simul != RES_OK && res_simul != RES_BAD_OP) {
      ATOMIC_SET(&res, res_simul);
      goto error_it;
    }

    /* Register heat path */
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
    if(pcent > progress) {
      progress = pcent;
      log_info(scn->dev, "Solving boundary temperature: %3d%%\r", progress);
    }
  exit_it:
    if(pheat_path) heat_path_release(pheat_path);
    continue;
  error_it:
    goto exit_it;
  }
  if(res != RES_OK) goto error;

  /* Add a new line after the progress status */
  log_info(scn->dev, "Solving boundary temperature: %3d%%\n", progress);

  /* Setup the estimated temperature */
  if(out_estimator) {
    struct accum acc_temp;
    struct accum acc_time;

    sum_accums(acc_temps, scn->dev->nthreads, &acc_temp);
    sum_accums(acc_times, scn->dev->nthreads, &acc_time);
    ASSERT(acc_temp.count == acc_time.count);

    estimator_setup_realisations_count(estimator, nrealisations, acc_temp.count);
    estimator_setup_temperature(estimator, acc_temp.sum, acc_temp.sum2);
    estimator_setup_realisation_time(estimator, acc_time.sum, acc_time.sum2);
    res = estimator_save_rng_state(estimator, rng_proxy);
    if(res != RES_OK) goto error;
  }

  /* Setup the green function */
  if(out_green) {
    struct accum acc_time;

    /* Redux the per thread green function into the green of the 1st thread */
    green = greens[0]; /* Return the green of the 1st thread */
    greens[0] = NULL; /* Make invalid the 1st green for 'on exit' clean up*/
    res = green_function_redux_and_clear(green, greens+1, scn->dev->nthreads-1);
    if(res != RES_OK) goto error;

    /* Finalize the estimated green */
    sum_accums(acc_times, scn->dev->nthreads, &acc_time);
    res = green_function_finalize(green, rng_proxy, &acc_time);
    if(res != RES_OK) goto error;
  }

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) {
      if(rngs[i]) SSP(rng_ref_put(rngs[i]));
    }
    MEM_RM(scn->dev->allocator, rngs);
  }
  if(greens) {
    FOR_EACH(i, 0, scn->dev->nthreads) {
      if(greens[i]) SDIS(green_function_ref_put(greens[i]));
    }
    MEM_RM(scn->dev->allocator, greens);
  }
  if(acc_temps) MEM_RM(scn->dev->allocator, acc_temps);
  if(acc_times) MEM_RM(scn->dev->allocator, acc_times);
  if(scene) SXD(scene_ref_put(scene));
  if(shape) SXD(shape_ref_put(shape));
  if(view) SXD(scene_view_ref_put(view));
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_green) *out_green = green;
  if(out_estimator) *out_estimator = estimator;
  return (res_T)res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  if(green) {
    SDIS(green_function_ref_put(green));
    green = NULL;
  }
  goto exit;
}

static res_T
XD(solve_boundary_flux)
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_flux_args* args,
   struct sdis_estimator** out_estimator)
{
  struct XD(boundary_context) ctx = XD(BOUNDARY_CONTEXT_NULL);
  struct sXd(vertex_data) vdata = SXD_VERTEX_DATA_NULL;
  struct sXd(scene)* scene = NULL;
  struct sXd(shape)* shape = NULL;
  struct sXd(scene_view)* view = NULL;
  struct sdis_estimator* estimator = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  struct accum* acc_tp = NULL; /* Per thread temperature accumulator */
  struct accum* acc_ti = NULL; /* Per thread realisation time accumulator */
  struct accum* acc_fl = NULL; /* Per thread flux accumulator */
  struct accum* acc_fc = NULL; /* Per thread convective flux accumulator */
  struct accum* acc_fr = NULL; /* Per thread radiative flux accumulator */
  struct accum* acc_fi = NULL; /* Per thread imposed flux accumulator */
  size_t nrealisations = 0;
  int64_t irealisation;
  size_t i;
  size_t view_nprims;
  int progress = 0;
  int msg1 = 0, msg2 = 0;
  ATOMIC nsolved_realisations = 0;
  ATOMIC res = RES_OK;

  if(!scn
  || !args
  || !args->nrealisations
  || args->nrealisations > INT64_MAX
  || !args->primitives
  || args->time_range[0] < 0
  || args->time_range[1] < args->time_range[0]
  || (args->time_range[1] > DBL_MAX && args->time_range[0] != args->time_range[1])
  || !args->nprimitives
  || args->fp_to_meter < 0
  || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

#if SDIS_XD_DIMENSION == 2
  if(scene_is_2d(scn) == 0) { res = RES_BAD_ARG; goto error; }
#else
  if(scene_is_2d(scn) != 0) { res = RES_BAD_ARG; goto error; }
#endif

  SXD(scene_view_primitives_count(scn->sXd(view), &view_nprims));
  FOR_EACH(i, 0, args->nprimitives) {
    if(args->primitives[i] >= view_nprims) {
      log_err(scn->dev,
        "%s: invalid primitive identifier `%lu'. It must be in the [0 %lu] range.\n",
        FUNC_NAME,
        (unsigned long)args->primitives[i],
        (unsigned long)scene_get_primitives_count(scn)-1);
      res = RES_BAD_ARG;
      goto error;
    }
  }

  /* Create the Star-XD shape of the boundary */
#if SDIS_XD_DIMENSION == 2
  res = s2d_shape_create_line_segments(scn->dev->sXd(dev), &shape);
#else
  res = s3d_shape_create_mesh(scn->dev->sXd(dev), &shape);
#endif
  if(res != RES_OK) goto error;

  /* Initialise the boundary shape with the triangles/segments of the
   * submitted primitives  */
  ctx.primitives = args->primitives;
  ctx.view = scn->sXd(view);
  vdata.get = XD(boundary_get_position);
#if SDIS_XD_DIMENSION == 2
  vdata.usage = S2D_POSITION;
  vdata.type = S2D_FLOAT2;
  res = s2d_line_segments_setup_indexed_vertices(shape,
    (unsigned)args->nprimitives, XD(boundary_get_indices),
    (unsigned)(args->nprimitives*2), &vdata, 1, &ctx);
#else /* DIM == 3 */
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  res = s3d_mesh_setup_indexed_vertices(shape, (unsigned)args->nprimitives,
    XD(boundary_get_indices), (unsigned)(args->nprimitives*3), &vdata, 1, &ctx);
#endif
  if(res != RES_OK) goto error;

  /* Create and setup the boundary Star-XD scene */
  res = sXd(scene_create)(scn->dev->sXd(dev), &scene);
  if(res != RES_OK) goto error;
  res = sXd(scene_attach_shape)(scene, shape);
  if(res != RES_OK) goto error;
  res = sXd(scene_view_create)(scene, SXD_SAMPLE, &view);
  if(res != RES_OK) goto error;

  /* Create the proxy RNG */
  if(args->rng_state) {
    res = ssp_rng_proxy_create_from_rng(scn->dev->allocator, args->rng_state,
      scn->dev->nthreads, &rng_proxy);
    if(res != RES_OK) goto error;
  } else {
    res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
      scn->dev->nthreads, &rng_proxy);
    if(res != RES_OK) goto error;
  }

  /* Create the per thread RNG */
  rngs = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*rngs));
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

  /* Create the estimator */
  res = estimator_create(scn->dev, SDIS_ESTIMATOR_FLUX, &estimator);
  if(res != RES_OK) goto error;

  nrealisations = args->nrealisations;
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) shared(msg1, msg2)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    struct time t0, t1;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    struct accum* acc_temp = &acc_tp[ithread];
    struct accum* acc_time = &acc_ti[ithread];
    struct accum* acc_flux = &acc_fl[ithread];
    struct accum* acc_fcon = &acc_fc[ithread];
    struct accum* acc_frad = &acc_fr[ithread];
    struct accum* acc_fimp = &acc_fi[ithread];
    struct sXd(primitive) prim;
    struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
    const struct sdis_interface* interf;
    const struct sdis_medium *fmd, *bmd;
    enum sdis_side solid_side, fluid_side;
    double T_brf[3] = { 0, 0, 0 };
    const double Tref = args->reference_temperature;
    const double Tarad = args->ambient_radiative_temperature;
    double epsilon, hc, hr, imposed_flux, imposed_temp;
    size_t iprim;
    double uv[DIM - 1];
    float st[DIM - 1];
    double time;
    int flux_mask = 0;
    size_t n;
    int pcent;
    res_T res_local = RES_OK;
    res_T res_simul = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    /* Begin time registration */
    time_current(&t0);

    time = sample_time(rng, args->time_range);

    /* Sample a position onto the boundary */
#if SDIS_XD_DIMENSION == 2
    res_local = s2d_scene_view_sample
      (view,
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, st);
    uv[0] = (double)st[0];
#else
    res_local = s3d_scene_view_sample
      (view,
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       ssp_rng_canonical_float(rng),
       &prim, st);
    d2_set_f2(uv, st);
#endif
    if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); continue; }

    /* Map from boundary scene to sdis scene */
    ASSERT(prim.prim_id < args->nprimitives);
    iprim = args->primitives[prim.prim_id];

    interf = scene_get_interface(scn, (unsigned)iprim);
    fmd = interface_get_medium(interf, SDIS_FRONT);
    bmd = interface_get_medium(interf, SDIS_BACK);
    if(!fmd || !bmd
      || (  !(fmd->type == SDIS_FLUID && bmd->type == SDIS_SOLID)
         && !(fmd->type == SDIS_SOLID && bmd->type == SDIS_FLUID)))
    {
      #pragma omp critical
      {
        if(msg1 == 0) {
          msg1 = 1,
          log_err(scn->dev,
            "%s: Attempt to compute a flux at a %s-%s interface.\n",
            FUNC_NAME,
            (fmd->type == SDIS_FLUID ? "fluid" : "solid"),
            (bmd->type == SDIS_FLUID ? "fluid" : "solid"));
        }
      }
      ATOMIC_SET(&res, RES_BAD_ARG);
      continue;
    }
    solid_side = (fmd->type == SDIS_SOLID) ? SDIS_FRONT : SDIS_BACK;
    fluid_side = (fmd->type == SDIS_FLUID) ? SDIS_FRONT : SDIS_BACK;

    /* Build interface fragment on the fluid side of the primitive */
    res_local = XD(build_interface_fragment)
      (&frag, scn, (unsigned)iprim, uv, fluid_side);
    if(res_local!= RES_OK) { ATOMIC_SET(&res, res_local); continue; }

    /* Fetch interface parameters */
    epsilon = interface_side_get_emissivity(interf, &frag);
    hc = interface_get_convection_coef(interf, &frag);
    hr = 4.0 * BOLTZMANN_CONSTANT * Tref * Tref * Tref * epsilon;
    frag.side = solid_side;
    imposed_flux = interface_side_get_flux(interf, &frag);
    imposed_temp = interface_side_get_temperature(interf, &frag);
    if(imposed_temp >= 0) {
      /* Flux computation on T boundaries is not supported yet */
      #pragma omp critical
      {
        if(msg2 == 0) {
          msg2 = 1,
            log_err(scn->dev,
              "%s: Attempt to compute a flux at a Dirichlet boundary.\n",
              FUNC_NAME);
        }
      }
      ATOMIC_SET(&res, RES_BAD_ARG);
      continue;
    }

    /* Fluid, Radiative and Solid temperatures */
    flux_mask = 0;
    if(hr > 0) flux_mask |= FLUX_FLAG_RADIATIVE;
    if(hc > 0) flux_mask |= FLUX_FLAG_CONVECTIVE;
    res_simul = XD(boundary_flux_realisation)(scn, rng, iprim, uv, time,
      solid_side, args->fp_to_meter, Tarad, Tref, flux_mask, T_brf);

    /* Stop time registration */
    time_sub(&t0, time_current(&t1), &t0);

    if(res_simul != RES_OK && res_simul != RES_BAD_OP) {
      ATOMIC_SET(&res, res_simul);
      continue;
    } else if(res_simul == RES_OK) { /* Update accumulators */
      const double usec = (double)time_val(&t0, TIME_NSEC) * 0.001;
      const double Tboundary = T_brf[0];
      const double Tradiative = T_brf[1];
      const double Tfluid = T_brf[2];
      const double w_conv = hc * (Tboundary - Tfluid);
      const double w_rad = hr * (Tboundary - Tradiative);
      const double w_imp = (imposed_flux != SDIS_FLUX_NONE) ? imposed_flux : 0;
      const double w_total = w_conv + w_rad + w_imp;
      /* Temperature */
      acc_temp->sum += Tboundary;
      acc_temp->sum2 += Tboundary*Tboundary;
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
      log_info(scn->dev, "Solving boundary flux: %3d%%\r", progress);
    }
  }
  if(res != RES_OK) goto error;

  /* Add a new line after the progress status */
  log_info(scn->dev, "Solving boundary flux: %3d%%\n", progress);

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
    FOR_EACH(i, 0, scn->dev->nthreads) { if(rngs[i]) SSP(rng_ref_put(rngs[i])); }
    MEM_RM(scn->dev->allocator, rngs);
  }
  if(acc_tp) MEM_RM(scn->dev->allocator, acc_tp);
  if(acc_ti) MEM_RM(scn->dev->allocator, acc_ti);
  if(acc_fc) MEM_RM(scn->dev->allocator, acc_fc);
  if(acc_fr) MEM_RM(scn->dev->allocator, acc_fr);
  if(acc_fl) MEM_RM(scn->dev->allocator, acc_fl);
  if(acc_fi) MEM_RM(scn->dev->allocator, acc_fi);
  if(scene) SXD(scene_ref_put(scene));
  if(shape) SXD(shape_ref_put(shape));
  if(view) SXD(scene_view_ref_put(view));
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
