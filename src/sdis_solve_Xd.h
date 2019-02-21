/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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
#include "sdis_green.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_realisation.h"
#include "sdis_scene_c.h"

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
 * Helper functions
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
  s2d_segment_get_vertex_attrib(&prim, iprim_vert, S2D_POSITION, &attr);
  ASSERT(attr.type == S2D_FLOAT2);
#else
  s3d_triangle_get_vertex_attrib(&prim, iprim_vert, S3D_POSITION, &attr);
  ASSERT(attr.type == S3D_FLOAT3);
#endif
  fX(set)(pos, attr.value);
}

static INLINE res_T
XD(interface_prebuild_fragment)
  (struct sdis_interface_fragment* frag,
   const struct sdis_scene* scn,
   const unsigned iprim,
   const double* uv,
   const enum sdis_side fluid_side)
{
  struct sXd(attrib) attr;
  struct sXd(primitive) prim;
  struct sXd(hit) hit;
  struct sdis_rwalk_vertex vtx;
#if SDIS_XD_DIMENSION == 2
  float st;
#else
  float st[2];
#endif
  res_T res = RES_OK;

  ASSERT(frag && scn && uv);
  ASSERT(fluid_side == SDIS_FRONT || fluid_side == SDIS_BACK);

  *frag = SDIS_INTERFACE_FRAGMENT_NULL;

#if SDIS_XD_DIMENSION == 2
#define SET_PARAM(Dest, Src) (Dest).u = (Src);
  st = (float)uv[0];
#else
#define SET_PARAM(Dest, Src) f2_set((Dest).uv, (Src));
  f2_set_d2(st, uv);
#endif
  res = sXd(scene_view_get_primitive(scn->sXd(view), iprim, &prim));
  if(res != RES_OK) return res;
  res = sXd(primitive_get_attrib(&prim, SXD_POSITION, st, &attr));
  if(res != RES_OK) return res;
  dX_set_fX(vtx.P, attr.value);
  res = sXd(primitive_get_attrib(&prim, SXD_GEOMETRY_NORMAL, st, &attr));
  if(res != RES_OK) return res;
  fX(set)(hit.normal, attr.value);

  hit.distance = 0;
  hit.prim = prim;
  vtx.time = NaN;
  SET_PARAM(hit, st);
  #undef SET_PARAM
  XD(setup_interface_fragment)(frag, &vtx, &hit, fluid_side);

  return RES_OK;
}

static res_T
XD(interface_get_hc_epsilon)
  (double *hc,
   double* epsilon,
   const struct sdis_scene* scn,
   const unsigned iprim,
   const double* uv,
   const double time,
   const enum sdis_side fluid_side)
{
  struct sdis_interface_fragment frag;
  const struct sdis_interface* interf;
  res_T res = RES_OK;

  res = XD(interface_prebuild_fragment)(&frag, scn, iprim, uv, fluid_side);
  if(res != RES_OK) return res;

  frag.time = time;
  interf = scene_get_interface(scn, iprim);
  ASSERT(interf);
  *epsilon = interface_side_get_emissivity(interf, &frag);
  *hc = interface_get_convection_coef(interf, &frag);
  return RES_OK;
}

/*******************************************************************************
 * Generic solve function
 ******************************************************************************/
static res_T
XD(solve_probe)
  (struct sdis_scene* scn,
   const size_t nrealisations,
   const double position[3],
   const double time_range[2],
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   const double Tarad, /* Ambient radiative temperature */
   const double Tref, /* Reference temperature */
   struct sdis_green_function** out_green, /* May be NULL <=> No green func */
   struct sdis_estimator** out_estimator)
{
  struct sdis_medium* medium = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_green_function* green = NULL;
  struct sdis_green_function** greens = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  double weight = 0;
  double sqr_weight = 0;
  const int64_t rcount = (int64_t)nrealisations;
  int64_t irealisation = 0;
  size_t N = 0; /* #realisations that do not fail */
  size_t i;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !position
  || fp_to_meter <= 0 || Tref < 0) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(!out_estimator && !out_green) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(out_estimator) {
    if(!time_range || time_range[0] < 0 || time_range[1] < time_range[0]
    || (time_range[1] > DBL_MAX && time_range[0] != time_range[1])) {
      res = RES_BAD_ARG;
      goto error;
    }
  }

#if SDIS_XD_DIMENSION == 2
  if(scene_is_2d(scn) == 0) { res = RES_BAD_ARG; goto error; }
#else
  if(scene_is_2d(scn) != 0) { res = RES_BAD_ARG; goto error; }
#endif

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*rngs));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs+i);
    if(res != RES_OK) goto error;
  }

  /* Retrieve the medium in which the submitted position lies */
  res = scene_get_medium(scn, position, NULL, &medium);
  if(res != RES_OK) goto error;

  if(out_green) {
    /* Create the per thread green function */
    greens = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*greens));
    if(!greens) { res = RES_MEM_ERR; goto error; }
    FOR_EACH(i, 0, scn->dev->nthreads) {
      res = green_function_create(scn->dev, &greens[i]);
      if(res != RES_OK) goto error;
    }

  }

  /* Here we go! Launch the Monte Carlo estimation */
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) reduction(+:weight,sqr_weight,N)
  for(irealisation = 0; irealisation < rcount; ++irealisation) {
    res_T res_local;
    double w = NaN;
    double time;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    struct green_path_handle* pgreen_path = NULL;
    struct green_path_handle green_path = GREEN_PATH_HANDLE_NULL;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occured */

    if(!out_green) {
      time = sample_time(rng, time_range);
    } else {
      /* Do not take care of the submitted time when registering the green
       * function. Simply takes 0 as relative time */
      time = 0;
      res_local = green_function_create_path(greens[ithread], &green_path);
      if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); continue; }

      pgreen_path = &green_path;
    }

    res_local = XD(probe_realisation)(scn, rng, medium, position, time,
      fp_to_meter, Tarad, Tref, pgreen_path, &w);
    if(res_local != RES_OK) {
      if(res_local != RES_BAD_OP) { ATOMIC_SET(&res, res_local); continue; }
    } else {
      weight += w;
      sqr_weight += w*w;
      ++N;
    }
  }
  if(res != RES_OK) goto error;

  if(out_estimator) {
    /* Create the estimator */
    res = estimator_create
      (scn->dev, SDIS_ESTIMATOR_TEMPERATURE, nrealisations, N, &estimator);
    if(res != RES_OK) goto error;

    /* Setup the estimated temperature */
    estimator_setup_temperature(estimator, weight, sqr_weight);
  }

  if(out_green) {
    green = greens[0]; /* Return the green of the 1st thread */
    greens[0] = NULL; /* Make invalid the 1st green for 'on exit' clean up*/
    FOR_EACH(i, 1, scn->dev->nthreads) { /* Merge the per thread green */
      res = green_function_merge_and_clear(green, greens[i]);
      if(res != RES_OK) goto error;
    }

    /* Finalize the estimated green */
    res = green_function_finalize(green);
    if(res != RES_OK) goto error;
  }

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads)  {
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
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_green) *out_green = green;
  if(out_estimator) *out_estimator = estimator;
  return (res_T)res;
error:
  if(green) {
    SDIS(green_function_ref_put(green));
    green = NULL;
  }
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

static res_T
XD(solve_probe_boundary)
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t iprim, /* Identifier of the primitive on which the probe lies */
   const double uv[2], /* Parametric coordinates of the probe onto the primitve */
   const double time_range[2], /* Observation time */
   const enum sdis_side side, /* Side of iprim on which the probe lies */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
   struct sdis_estimator** out_estimator)
{
  struct sdis_estimator* estimator = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  double weight = 0;
  double sqr_weight = 0;
  const int64_t rcount = (int64_t)nrealisations;
  int64_t irealisation = 0;
  size_t N = 0; /* #realisations that do not fail */
  size_t i;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !uv
    || !time_range || time_range[0] < 0 || time_range[1] < time_range[0]
    || (time_range[1] > DBL_MAX && time_range[0] != time_range[1])
    || fp_to_meter <= 0 || Tref < 0 || (side != SDIS_FRONT && side != SDIS_BACK)
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
  if(iprim >= scene_get_primitives_count(scn)) {
    log_err(scn->dev,
      "%s: invalid primitive identifier `%lu'. "
      "It must be in the [0 %lu] range.\n",
      FUNC_NAME,
      (unsigned long)iprim,
      (unsigned long)scene_get_primitives_count(scn)-1);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check parametric coordinates */
  if(scene_is_2d(scn)) {
    const double v = CLAMP(1.0 - uv[0], 0, 1);
    if(uv[0] < 0 || uv[0] > 1 || !eq_eps(uv[0] + v, 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates %g."
        "u + (1-u) must be equal to 1 with u [0, 1].\n",
        FUNC_NAME, uv[0]);
      res = RES_BAD_ARG;
      goto error;
    }
  } else {
    const double w = CLAMP(1 - uv[0] - uv[1], 0, 1);
    if(uv[0] < 0 || uv[1] < 0 || uv[0] > 1 || uv[1] > 1
    || !eq_eps(w + uv[0] + uv[1], 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates [%g, %g]. "
        "u + v + (1-u-v) must be equal to 1 with u and v in [0, 1].\n",
        FUNC_NAME, uv[0], uv[1]);
      res = RES_BAD_ARG;
      goto error;
    }
  }

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(struct ssp_rng*));
  if(!rngs) {
    res = RES_MEM_ERR;
    goto error;
  }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs+i);
    if(res != RES_OK) goto error;
  }

  /* Here we go! Launch the Monte Carlo estimation */
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) reduction(+:weight,sqr_weight,N)
  for(irealisation = 0; irealisation < rcount; ++irealisation) {
    res_T res_local;
    double w = NaN;
    double time;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    time = sample_time(rng, time_range);

    res_local = XD(boundary_realisation)
      (scn, rng, iprim, uv, time, side, fp_to_meter, Tarad, Tref, &w);
    if(res_local != RES_OK) {
      if(res_local != RES_BAD_OP) {
        ATOMIC_SET(&res, res_local);
        continue;
      }
    } else {
      weight += w;
      sqr_weight += w*w;
      ++N;
    }
  }
  if(res != RES_OK) goto error;

  /* Create the estimator */
  res = estimator_create
    (scn->dev, SDIS_ESTIMATOR_TEMPERATURE, nrealisations, N, &estimator);
  if(res != RES_OK) goto error;

  /* Setup the estimated temperature */
  estimator_setup_temperature(estimator, weight, sqr_weight);

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) {
      if(rngs[i]) SSP(rng_ref_put(rngs[i]));
    }
    MEM_RM(scn->dev->allocator, rngs);
  }
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

static res_T
XD(solve_boundary)
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t primitives[], /* List of boundary primitives to handle */
   const enum sdis_side sides[], /* Per primitive side to consider */
   const size_t nprimitives, /* #primitives */
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
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
  size_t i;
  size_t N = 0; /* #realisations that do not fail */
  size_t view_nprims;
  double weight=0, sqr_weight=0;
  int64_t irealisation;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !primitives
  || !time_range || time_range[0] < 0 || time_range[1] < time_range[0]
  || (time_range[1] > DBL_MAX && time_range[0] != time_range[1])
  || !sides || !nprimitives || fp_to_meter < 0 || Tref < 0
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
  FOR_EACH(i, 0, nprimitives) {
    if(primitives[i] >= view_nprims) {
      log_err(scn->dev,
        "%s: invalid primitive identifier `%lu'. It must be in the [0 %lu] range.\n",
        FUNC_NAME,
        (unsigned long)primitives[i],
        (unsigned long)scene_get_primitives_count(scn)-1);
      res = RES_BAD_ARG;
      goto error;
    }
  }

  /* Create the Star-XD shape of the boundary */
#if SDIS_XD_DIMENSION == 2
  res = s2d_shape_create_line_segments(scn->dev->sXd_dev, &shape);
#else
  res = s3d_shape_create_mesh(scn->dev->sXd_dev, &shape);
#endif
  if(res != RES_OK) goto error;

  /* Initialise the boundary shape with the triangles/segments of the
   * submitted primitives  */
  ctx.primitives = primitives;
  ctx.view = scn->sXd(view);
  vdata.usage = SXD_POSITION;
  vdata.get = XD(boundary_get_position);
#if SDIS_XD_DIMENSION == 2
  vdata.type = S2D_FLOAT2;
  res = s2d_line_segments_setup_indexed_vertices(shape, (unsigned)nprimitives,
    boundary_get_indices_2d, (unsigned)(nprimitives*2), &vdata, 1, &ctx);
#else
  vdata.type = S3D_FLOAT3;
  res = s3d_mesh_setup_indexed_vertices(shape, (unsigned)nprimitives,
    boundary_get_indices_3d, (unsigned)(nprimitives*3), &vdata, 1, &ctx);
#endif
  if(res != RES_OK) goto error;

  /* Create and setup the boundary Star-XD scene */
  res = sXd(scene_create)(scn->dev->sXd_dev, &scene);
  if(res != RES_OK) goto error;
  res = sXd(scene_attach_shape)(scene, shape);
  if(res != RES_OK) goto error;
  res = sXd(scene_view_create)(scene, SXD_SAMPLE, &view);
  if(res != RES_OK) goto error;

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*rngs));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs+i);
    if(res != RES_OK) goto error;
  }

  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) reduction(+:weight,sqr_weight,N)
  for(irealisation=0; irealisation<(int64_t)nrealisations; ++irealisation) {
    const int ithread = omp_get_thread_num();
    struct sXd(primitive) prim;
    struct ssp_rng* rng = rngs[ithread];
    enum sdis_side side;
    size_t iprim;
    double w = NaN;
    double uv[DIM-1];
    float st[DIM-1];
    double time;
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    time = sample_time(rng, time_range);

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
    ASSERT(prim.prim_id < nprimitives);
    iprim = primitives[prim.prim_id];
    side = sides[prim.prim_id];

    /* Invoke the boundary realisation */
    res_local = XD(boundary_realisation)
      (scn, rng, iprim, uv, time, side, fp_to_meter, Tarad, Tref, &w);

    /* Update the MC accumulators */
    if(res_local == RES_OK) {
      weight += w;
      sqr_weight += w*w;
      ++N;
    } else if(res_local != RES_BAD_OP) {
      ATOMIC_SET(&res, res_local);
      continue;
    }
  }

  /* Create the estimator */
  res = estimator_create
    (scn->dev, SDIS_ESTIMATOR_TEMPERATURE, nrealisations, N, &estimator);
  if(res != RES_OK) goto error;

  /* Setup the estimated temperature */
  estimator_setup_temperature(estimator, weight, sqr_weight);

exit:
  if(scene) SXD(scene_ref_put(scene));
  if(shape) SXD(shape_ref_put(shape));
  if(view) SXD(scene_view_ref_put(view));
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_estimator) *out_estimator = estimator;
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) {if(rngs[i]) SSP(rng_ref_put(rngs[i]));}
    MEM_RM(scn->dev->allocator, rngs);
  }
  return (res_T)res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

static res_T
XD(solve_probe_boundary_flux)
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t iprim, /* Identifier of the primitive on which the probe lies */
   const double uv[2], /* Parametric coordinates of the probe onto the primitve */
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
   struct sdis_estimator** out_estimator)
{
  struct sdis_estimator* estimator = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  const struct sdis_interface* interf;
  const struct sdis_medium *fmd, *bmd;
  enum sdis_side solid_side, fluid_side;
  struct sdis_interface_fragment frag;
  double weight_t = 0, sqr_weight_t = 0;
  double weight_fc = 0, sqr_weight_fc = 0;
  double weight_fr = 0, sqr_weight_fr = 0;
  double weight_f= 0, sqr_weight_f = 0;
  const int64_t rcount = (int64_t)nrealisations;
  int64_t irealisation = 0;
  size_t N = 0; /* #realisations that do not fail */
  size_t i;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !uv
  || !time_range || time_range[0] < 0 || time_range[1] < time_range[0]
  || (time_range[1] > DBL_MAX && time_range[0] != time_range[1])
  || fp_to_meter <= 0 || Tref < 0
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
  if(iprim >= scene_get_primitives_count(scn)) {
    log_err(scn->dev,
      "%s: invalid primitive identifier `%lu'. "
      "It must be in the [0 %lu] range.\n",
      FUNC_NAME,
      (unsigned long)iprim,
      (unsigned long)scene_get_primitives_count(scn)-1);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check parametric coordinates */
  if(scene_is_2d(scn)) {
    const double v = CLAMP(1.0 - uv[0], 0, 1);
    if(uv[0] < 0 || uv[0] > 1 || !eq_eps(uv[0] + v, 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates %g. "
        "u + (1-u) must be equal to 1 with u [0, 1].\n",
        FUNC_NAME, uv[0]);
      res = RES_BAD_ARG;
      goto error;
    }
  } else {
    const double w = CLAMP(1 - uv[0] - uv[1], 0, 1);
    if(uv[0] < 0 || uv[1] < 0 || uv[0] > 1 || uv[1] > 1
      || !eq_eps(w + uv[0] + uv[1], 1, 1.e-6)) {
      log_err(scn->dev,
        "%s: invalid parametric coordinates [%g, %g]. "
        "u + v + (1-u-v) must be equal to 1 with u and v in [0, 1].\n",
        FUNC_NAME, uv[0], uv[1]);
      res = RES_BAD_ARG;
      goto error;
    }
  }
  /* Check medium is fluid on one side and solid on the other */
  interf = scene_get_interface(scn, (unsigned)iprim);
  fmd = interface_get_medium(interf, SDIS_FRONT);
  bmd = interface_get_medium(interf, SDIS_BACK);
  if(!fmd || !bmd
  || (  !(fmd->type == SDIS_FLUID && bmd->type == SDIS_SOLID)
     && !(fmd->type == SDIS_SOLID && bmd->type == SDIS_FLUID))) {
    res = RES_BAD_ARG;
    goto error;
  }
  solid_side = (fmd->type == SDIS_SOLID) ? SDIS_FRONT : SDIS_BACK;
  fluid_side = (fmd->type == SDIS_FLUID) ? SDIS_FRONT : SDIS_BACK;

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC
    (scn->dev->allocator, scn->dev->nthreads, sizeof(struct ssp_rng*));
  if(!rngs) {
    res = RES_MEM_ERR;
    goto error;
  }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs + i);
    if(res != RES_OK) goto error;
  }

  /* Prebuild the interface fragment */
  res = XD(interface_prebuild_fragment)
    (&frag, scn, (unsigned)iprim, uv, fluid_side);

  /* Here we go! Launch the Monte Carlo estimation */
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) reduction(+:weight_t,sqr_weight_t,\
     weight_fc,sqr_weight_fc,weight_fr,sqr_weight_fr,weight_f,sqr_weight_f,N)
  for(irealisation = 0; irealisation < rcount; ++irealisation) {
    res_T res_local;
    double T_brf[3] = { 0, 0, 0 };
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    double time, epsilon, hc, hr;
    int flux_mask = 0;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    time = sample_time(rng, time_range);

    /* Compute hr and hc */
    frag.time = time;
    epsilon = interface_side_get_emissivity(interf, &frag);
    hc = interface_get_convection_coef(interf, &frag);
    hr = 4.0 * BOLTZMANN_CONSTANT * Tref * Tref * Tref * epsilon;

    /* Fluid, Radiative and Solid temperatures */
    flux_mask = 0;
    if(hr > 0) flux_mask |= FLUX_FLAG_RADIATIVE;
    if(hc > 0) flux_mask |= FLUX_FLAG_CONVECTIVE;
    res_local = XD(boundary_flux_realisation)(scn, rng, iprim, uv, time,
      solid_side, fp_to_meter, Tarad, Tref, flux_mask, T_brf);
    if(res_local != RES_OK) {
      if(res_local != RES_BAD_OP) {
        ATOMIC_SET(&res, res_local);
        continue;
      }
    } else {
      const double Tboundary = T_brf[0];
      const double Tradiative = T_brf[1];
      const double Tfluid = T_brf[2];
      const double w_conv = hc * (Tboundary - Tfluid);
      const double w_rad = hr * (Tboundary - Tradiative);
      const double w_total = w_conv + w_rad;
      weight_t += Tboundary;
      sqr_weight_t += Tboundary * Tboundary;
      weight_fc += w_conv;
      sqr_weight_fc += w_conv * w_conv;
      weight_fr += w_rad;
      sqr_weight_fr += w_rad * w_rad;
      weight_f += w_total;
      sqr_weight_f += w_total * w_total;
      ++N;
    }
  }
  if(res != RES_OK) goto error;

  /* Create the estimator */
  res = estimator_create
    (scn->dev, SDIS_ESTIMATOR_FLUX, nrealisations, N, &estimator);
  if(res != RES_OK) goto error;

  /* Setup the estimated values */
  estimator_setup_temperature(estimator, weight_t, sqr_weight_t);
  estimator_setup_flux(estimator, FLUX_CONVECTIVE, weight_fc, sqr_weight_fc);
  estimator_setup_flux(estimator, FLUX_RADIATIVE, weight_fr, sqr_weight_fr);
  estimator_setup_flux(estimator, FLUX_TOTAL, weight_f, sqr_weight_f);

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) {
      if(rngs[i]) SSP(rng_ref_put(rngs[i]));
    }
    MEM_RM(scn->dev->allocator, rngs);
  }
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
static res_T
XD(solve_boundary_flux)
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t primitives[], /* List of boundary primitives to handle */
   const size_t nprimitives, /* #primitives */
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
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
  double weight_t = 0, sqr_weight_t = 0;
  double weight_fc = 0, sqr_weight_fc = 0;
  double weight_fr = 0, sqr_weight_fr = 0;
  double weight_f = 0, sqr_weight_f = 0;
  size_t i;
  size_t N = 0; /* #realisations that do not fail */
  size_t view_nprims;
  int64_t irealisation;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !primitives
  || !time_range || time_range[0] < 0 || time_range[1] < time_range[0]
  || (time_range[1] > DBL_MAX && time_range[0] != time_range[1])
  || !nprimitives || fp_to_meter < 0 || Tref < 0
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
  FOR_EACH(i, 0, nprimitives) {
    if(primitives[i] >= view_nprims) {
      log_err(scn->dev,
        "%s: invalid primitive identifier `%lu'. It must be in the [0 %lu] range.\n",
        FUNC_NAME,
        (unsigned long)primitives[i],
        (unsigned long)scene_get_primitives_count(scn)-1);
      res = RES_BAD_ARG;
      goto error;
    }
  }

  /* Create the Star-XD shape of the boundary */
#if SDIS_XD_DIMENSION == 2
  res = s2d_shape_create_line_segments(scn->dev->s2d, &shape);
#else
  res = s3d_shape_create_mesh(scn->dev->s3d, &shape);
#endif
  if(res != RES_OK) goto error;

  /* Initialise the boundary shape with the triangles/segments of the
   * submitted primitives  */
  ctx.primitives = primitives;
  ctx.view = scn->sXd(view);
  vdata.get = XD(boundary_get_position);
#if SDIS_XD_DIMENSION == 2
  vdata.usage = S2D_POSITION;
  vdata.type = S2D_FLOAT2;
  res = s2d_line_segments_setup_indexed_vertices(shape, (unsigned)nprimitives,
    XD(boundary_get_indices), (unsigned)(nprimitives*2), &vdata, 1, &ctx);
#else /* DIM == 3 */
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  res = s3d_mesh_setup_indexed_vertices(shape, (unsigned)nprimitives,
    XD(boundary_get_indices), (unsigned)(nprimitives*3), &vdata, 1, &ctx);
#endif
  if(res != RES_OK) goto error;

  /* Create and setup the boundary Star-XD scene */
  res = sXd(scene_create)(scn->dev->sXd_dev, &scene);
  if(res != RES_OK) goto error;
  res = sXd(scene_attach_shape)(scene, shape);
  if(res != RES_OK) goto error;
  res = sXd(scene_view_create)(scene, SXD_SAMPLE, &view);
  if(res != RES_OK) goto error;

  /* Create the proxy RNG */
  res = ssp_rng_proxy_create(scn->dev->allocator, &ssp_rng_mt19937_64,
    scn->dev->nthreads, &rng_proxy);
  if(res != RES_OK) goto error;

  /* Create the per thread RNG */
  rngs = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*rngs));
  if(!rngs) { res = RES_MEM_ERR; goto error; }
  FOR_EACH(i, 0, scn->dev->nthreads) {
    res = ssp_rng_proxy_create_rng(rng_proxy, i, rngs + i);
    if(res != RES_OK) goto error;
  }

  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) reduction(+:weight_t,sqr_weight_t,\
     weight_fc,sqr_weight_fc,weight_fr,sqr_weight_fr,weight_f,sqr_weight_f,N)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    const int ithread = omp_get_thread_num();
    struct sXd(primitive) prim;
    struct ssp_rng* rng = rngs[ithread];
    const struct sdis_interface* interf;
    const struct sdis_medium *fmd, *bmd;
    enum sdis_side solid_side, fluid_side;
    double T_brf[3] = { 0, 0, 0 };
    double epsilon, hc, hr;
    size_t iprim;
    double uv[DIM - 1];
    float st[DIM - 1];
    double time;
    int flux_mask = 0;
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    time = sample_time(rng, time_range);

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
    ASSERT(prim.prim_id < nprimitives);
    iprim = primitives[prim.prim_id];

    interf = scene_get_interface(scn, (unsigned)iprim);
    fmd = interface_get_medium(interf, SDIS_FRONT);
    bmd = interface_get_medium(interf, SDIS_BACK);
    if(!fmd || !bmd
      || (!(fmd->type == SDIS_FLUID && bmd->type == SDIS_SOLID)
        && !(fmd->type == SDIS_SOLID && bmd->type == SDIS_FLUID)))
    {
      ATOMIC_SET(&res, RES_BAD_ARG);
      continue;
    }
    solid_side = (fmd->type == SDIS_SOLID) ? SDIS_FRONT : SDIS_BACK;
    fluid_side = (fmd->type == SDIS_FLUID) ? SDIS_FRONT : SDIS_BACK;

    res_local = XD(interface_get_hc_epsilon)(&hc, &epsilon, scn,
      (unsigned)iprim, uv, time, fluid_side);
    if(res_local != RES_OK) {
      ATOMIC_SET(&res, res_local);
      continue;
    }
    hr = 4.0 * BOLTZMANN_CONSTANT * Tref * Tref * Tref * epsilon;

    /* Fluid, Radiative and Solid temperatures */
    flux_mask = 0;
    if(hr > 0) flux_mask |= FLUX_FLAG_RADIATIVE;
    if(hc > 0) flux_mask |= FLUX_FLAG_CONVECTIVE;
    res_local = XD(boundary_flux_realisation)(scn, rng, iprim, uv, time,
      solid_side, fp_to_meter, Tarad, Tref, flux_mask, T_brf);
    if(res_local != RES_OK) {
      if(res_local != RES_BAD_OP) {
        ATOMIC_SET(&res, res_local);
        continue;
      }
    } else {
      const double Tboundary = T_brf[0];
      const double Tradiative = T_brf[1];
      const double Tfluid = T_brf[2];
      const double w_conv = hc * (Tboundary - Tfluid);
      const double w_rad = hr * (Tboundary - Tradiative);
      const double w_total = w_conv + w_rad;
      weight_t += Tboundary;
      sqr_weight_t += Tboundary * Tboundary;
      weight_fc += w_conv;
      sqr_weight_fc += w_conv * w_conv;
      weight_fr += w_rad;
      sqr_weight_fr += w_rad * w_rad;
      weight_f += w_total;
      sqr_weight_f += w_total * w_total;
      ++N;
    }
  }
  if(res != RES_OK) goto error;

  /* Create the estimator */
  res = estimator_create
    (scn->dev, SDIS_ESTIMATOR_FLUX, nrealisations, N, &estimator);
  if(res != RES_OK) goto error;

  /* Setup the estimated values */
  estimator_setup_temperature(estimator, weight_t, sqr_weight_t);
  estimator_setup_flux(estimator, FLUX_CONVECTIVE, weight_fc, sqr_weight_fc);
  estimator_setup_flux(estimator, FLUX_RADIATIVE, weight_fr, sqr_weight_fr);
  estimator_setup_flux(estimator, FLUX_TOTAL, weight_f, sqr_weight_f);

exit:
  if(scene) SXD(scene_ref_put(scene));
  if(shape) SXD(shape_ref_put(shape));
  if(view) SXD(scene_view_ref_put(view));
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_estimator) *out_estimator = estimator;
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) { if(rngs[i]) SSP(rng_ref_put(rngs[i])); }
    MEM_RM(scn->dev->allocator, rngs);
  }
  return (res_T)res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

#include "sdis_Xd_end.h"
