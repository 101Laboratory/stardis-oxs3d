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
#include "sdis_interface_c.h"
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
  s2d_segment_get_vertex_attrib(&prim, iprim_vert, S2D_POSITION, &attr);
  ASSERT(attr.type == S2D_FLOAT2);
#else
  s3d_triangle_get_vertex_attrib(&prim, iprim_vert, S3D_POSITION, &attr);
  ASSERT(attr.type == S3D_FLOAT3);
#endif
  fX(set)(pos, attr.value);
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
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
  struct accum* accums = NULL;
  size_t i;
  size_t view_nprims;
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

  /* Create the per thread accumulator */
  accums = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*accums));
  if(!accums) { res = RES_MEM_ERR; goto error; }

  /* Create the estimator */
  res = estimator_create(scn->dev, SDIS_ESTIMATOR_TEMPERATURE, &estimator);
  if(res != RES_OK) goto error;

  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation=0; irealisation<(int64_t)nrealisations; ++irealisation) {
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    struct accum* accum = &accums[ithread];
    struct sXd(primitive) prim;
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
      accum->sum += w;
      accum->sum2 += w*w;
      ++accum->count;
    } else if(res_local != RES_BAD_OP) {
      ATOMIC_SET(&res, res_local);
      continue;
    }
  }

  sum_accums(accums, scn->dev->nthreads, &accums[0]);

  /* Setup the estimated temperature */
  estimator_setup_realisations_count(estimator, nrealisations, accums[0].count);
  estimator_setup_temperature(estimator, accums[0].sum, accums[0].sum2);

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) {if(rngs[i]) SSP(rng_ref_put(rngs[i]));}
    MEM_RM(scn->dev->allocator, rngs);
  }
  if(accums) MEM_RM(scn->dev->allocator, accums);
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
  struct accum* acc_tp = NULL; /* Per thread temperature accumulator */
  struct accum* acc_fl = NULL; /* Per thread flux accumulator */
  struct accum* acc_fc = NULL; /* Per thread convective flux accumulator */
  struct accum* acc_fr = NULL; /* Per thread radiative flux accumulator */
  size_t i;
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

  /* Create the per thread accumulator */
  #define ALLOC_ACCUMS(Dst) {                                                  \
    Dst = MEM_CALLOC(scn->dev->allocator, scn->dev->nthreads, sizeof(*Dst));   \
    if(!Dst) { res = RES_MEM_ERR; goto error; }                                \
  } (void)0
  ALLOC_ACCUMS(acc_tp);
  ALLOC_ACCUMS(acc_fc);
  ALLOC_ACCUMS(acc_fl);
  ALLOC_ACCUMS(acc_fr);
  #undef ALLOC_ACCUMS

  /* Create the estimator */
  res = estimator_create(scn->dev, SDIS_ESTIMATOR_FLUX, &estimator);
  if(res != RES_OK) goto error;

  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static)
  for(irealisation = 0; irealisation < (int64_t)nrealisations; ++irealisation) {
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    struct accum* acc_temp = &acc_tp[ithread];
    struct accum* acc_flux = &acc_fl[ithread];
    struct accum* acc_fcon = &acc_fc[ithread];
    struct accum* acc_frad = &acc_fr[ithread];
    struct sXd(primitive) prim;
    struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
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
      || (  !(fmd->type == SDIS_FLUID && bmd->type == SDIS_SOLID)
         && !(fmd->type == SDIS_SOLID && bmd->type == SDIS_FLUID)))
    {
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

    /* Fluid, Radiative and Solid temperatures */
    flux_mask = 0;
    if(hr > 0) flux_mask |= FLUX_FLAG_RADIATIVE;
    if(hc > 0) flux_mask |= FLUX_FLAG_CONVECTIVE;
    res_local = XD(boundary_flux_realisation)(scn, rng, iprim, uv, time,
      solid_side, fp_to_meter, Tarad, Tref, flux_mask, T_brf);
    if(res_local == RES_OK) {
      const double Tboundary = T_brf[0];
      const double Tradiative = T_brf[1];
      const double Tfluid = T_brf[2];
      const double w_conv = hc * (Tboundary - Tfluid);
      const double w_rad = hr * (Tboundary - Tradiative);
      const double w_total = w_conv + w_rad;
      /* Temperature */
      acc_temp->sum += Tboundary;
      acc_temp->sum2 += Tboundary*Tboundary;
      ++acc_temp->count;
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
    } else if(res_local != RES_BAD_OP) {
      ATOMIC_SET(&res, res_local);
      continue;
    }
  }
  if(res != RES_OK) goto error;

  /* Redux the per thread accumulators  */
  sum_accums(acc_tp, scn->dev->nthreads, &acc_tp[0]);
  sum_accums(acc_fc, scn->dev->nthreads, &acc_fc[0]);
  sum_accums(acc_fr, scn->dev->nthreads, &acc_fr[0]);
  sum_accums(acc_fl, scn->dev->nthreads, &acc_fl[0]);
  ASSERT(acc_tp[0].count == acc_fl[0].count);
  ASSERT(acc_tp[0].count == acc_fr[0].count);
  ASSERT(acc_tp[0].count == acc_fc[0].count);

  /* Setup the estimated values */
  estimator_setup_realisations_count(estimator, nrealisations, acc_tp[0].count);
  estimator_setup_temperature(estimator, acc_tp[0].sum, acc_tp[0].sum2);
  estimator_setup_flux(estimator, FLUX_CONVECTIVE, acc_fc[0].sum, acc_fc[0].sum2);
  estimator_setup_flux(estimator, FLUX_RADIATIVE, acc_fr[0].sum, acc_fr[0].sum2);
  estimator_setup_flux(estimator, FLUX_TOTAL, acc_fl[0].sum, acc_fl[0].sum2);

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads) { if(rngs[i]) SSP(rng_ref_put(rngs[i])); }
    MEM_RM(scn->dev->allocator, rngs);
  }
  if(acc_tp) MEM_RM(scn->dev->allocator, acc_tp);
  if(acc_fc) MEM_RM(scn->dev->allocator, acc_fc);
  if(acc_fr) MEM_RM(scn->dev->allocator, acc_fr);
  if(acc_fl) MEM_RM(scn->dev->allocator, acc_fl);
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
