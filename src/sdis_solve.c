/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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

#include "sdis.h"
#include "sdis_camera.h"
#include "sdis_device_c.h"
#include "sdis_estimator_c.h"
#include "sdis_solve_Xd.h"

/* Generate the 2D solver */
#define SDIS_SOLVE_DIMENSION 2
#include "sdis_solve_Xd.h"

/* Generate the 3D solver */
#define SDIS_SOLVE_DIMENSION 3
#include "sdis_solve_Xd.h"

#include <star/ssp.h>
#include <omp.h>

struct boundary_context {
  struct s3d_scene_view* s3d_view;
  const size_t* primitives;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static FINLINE uint16_t
morton2D_decode(const uint32_t u32)
{
  uint32_t x = u32 & 0x55555555;
  x = (x | (x >> 1)) & 0x33333333;
  x = (x | (x >> 2)) & 0x0F0F0F0F;
  x = (x | (x >> 4)) & 0x00FF00FF;
  x = (x | (x >> 8)) & 0x0000FFFF;
  return (uint16_t)x;
}

static INLINE void
setup_estimator
  (struct sdis_estimator* estimator,
   const size_t nrealisations,
   const size_t nsuccesses,
   const double accum_weights,
   const double accum_sqr_weights)
{
  ASSERT(estimator && nrealisations && nsuccesses);
  estimator->nrealisations = nsuccesses;
  estimator->nfailures = nrealisations - nsuccesses;
  estimator->temperature.E = accum_weights / (double)nsuccesses;
  estimator->temperature.V =
    accum_sqr_weights / (double)nsuccesses
  - estimator->temperature.E * estimator->temperature.E;
  estimator->temperature.V = MMAX(estimator->temperature.V, 0);
  estimator->temperature.SE = sqrt(estimator->temperature.V / (double)nsuccesses);
}

static void
get_indices(const unsigned itri, unsigned ids[3], void* context)
{
  (void)context;
  ASSERT(ids);
  ids[0] = itri*3+0;
  ids[1] = itri*3+1;
  ids[2] = itri*3+2;
}

static void
get_position(const unsigned ivert, float pos[3], void* context)
{
  struct boundary_context* ctx = context;
  struct s3d_primitive prim;
  struct s3d_attrib attr;
  const unsigned iprim = ivert / 3;
  const unsigned itri_vert = ivert % 3;
  unsigned itri;
  ASSERT(pos && context);

  itri = (unsigned)ctx->primitives[iprim];
  S3D(scene_view_get_primitive(ctx->s3d_view, itri, &prim));
  S3D(triangle_get_vertex_attrib(&prim, itri_vert, S3D_POSITION, &attr));
  ASSERT(attr.type == S3D_FLOAT3);
  f3_set(pos, attr.value);
}

static res_T
solve_pixel
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct sdis_medium* mdm,
   const struct sdis_camera* cam,
   const double time, /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
   const size_t ipix[2], /* Pixel coordinate in the image plane */
   const size_t nrealisations,
   const double pix_sz[2], /* Pixel size in the normalized image plane */
   struct sdis_accum* accum)
{
  double sum_weights = 0;
  double sum_weights_sqr = 0;
  size_t N = 0; /* #realisations that do not fail */
  size_t irealisation;
  res_T res = RES_OK;
  ASSERT(scn && mdm && rng && cam && ipix && nrealisations);
  ASSERT(pix_sz && pix_sz[0] > 0 && pix_sz[1] > 0);

  FOR_EACH(irealisation, 0, nrealisations) {
    double samp[2]; /* Pixel sample */
    double ray_pos[3];
    double ray_dir[3];
    double w = 0;

    /* Generate a sample into the pixel to estimate */
    samp[0] = ((double)ipix[0] + ssp_rng_canonical(rng)) * pix_sz[0];
    samp[1] = ((double)ipix[1] + ssp_rng_canonical(rng)) * pix_sz[1];

    /* Generate a ray starting from the camera position and passing through
     * pixel sample */
    camera_ray(cam, samp, ray_pos, ray_dir);

    /* Launch the realisation */
    res = ray_realisation_3d(scn, rng, mdm, ray_pos, ray_dir,
      time, fp_to_meter, Tarad, Tref, &w);
    if(res == RES_OK) {
      sum_weights += w;
      sum_weights_sqr += w*w;
      ++N;
    } else if(res == RES_BAD_OP) {
      res = RES_OK;
    } else {
      goto error;
    }
  }

  accum->sum_weights = sum_weights;
  accum->sum_weights_sqr = sum_weights_sqr;
  accum->nweights = N;
  accum->nfailures = nrealisations - N;

exit:
  return res;
error:
  goto exit;
}

static res_T
solve_tile
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const struct sdis_medium* mdm,
   const struct sdis_camera* cam,
   const double time,
   const double fp_to_meter,
   const double Tarad,
   const double Tref,
   const size_t origin[2], /* Tile origin in image plane */
   const size_t size[2], /* #pixels in X and Y */
   const size_t spp, /* #samples per pixel */
   const double pix_sz[2], /* Pixel size in the normalized image plane */
   struct sdis_accum* accums)
{
  size_t mcode; /* Morton code of the tile pixel */
  size_t npixels;
  res_T res = RES_OK;
  ASSERT(scn && rng && mdm && cam && spp && origin && accums);
  ASSERT(size &&size[0] && size[1]);
  ASSERT(pix_sz && pix_sz[0] > 0 && pix_sz[1] > 0);

  /* Adjust the #pixels to process them wrt a morton order */
  npixels = round_up_pow2(MMAX(size[0], size[1]));
  npixels *= npixels;

  FOR_EACH(mcode, 0, npixels) {
    size_t ipix[2];
    struct sdis_accum* accum;

    ipix[0] = morton2D_decode((uint32_t)(mcode>>0));
    if(ipix[0] >= size[0]) continue;
    ipix[1] = morton2D_decode((uint32_t)(mcode>>1));
    if(ipix[1] >= size[1]) continue;

    accum = accums + ipix[1]*size[0] + ipix[0];
    ipix[0] = ipix[0] + origin[0];
    ipix[1] = ipix[1] + origin[1];

    res = solve_pixel(scn, rng, mdm, cam, time, fp_to_meter, Tarad, Tref, ipix,
      spp, pix_sz, accum);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_solve_probe
  (struct sdis_scene* scn,
   const size_t nrealisations,
   const double position[3],
   const double time,
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   const double Tarad, /* Ambient radiative temperature */
   const double Tref, /* Reference temperature */
   struct sdis_estimator** out_estimator)
{
  const struct sdis_medium* medium = NULL;
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

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !position
    || time < 0 || fp_to_meter <= 0 || Tref < 0 || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
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

  /* Create the estimator */
  res = estimator_create(scn->dev, &estimator);
  if(res != RES_OK) goto error;

  /* Retrieve the medium in which the submitted position lies */
  res = scene_get_medium(scn, position, NULL, &medium);
  if(res != RES_OK) goto error;

  /* Here we go! Launch the Monte Carlo estimation */
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) reduction(+:weight,sqr_weight,N)
  for(irealisation = 0; irealisation < rcount; ++irealisation) {
    res_T res_local;
    double w = NaN;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occured */

    if(scene_is_2d(scn)) {
      res_local = probe_realisation_2d
        (scn, rng, medium, position, time, fp_to_meter, Tarad, Tref, &w);
    } else {
      res_local = probe_realisation_3d
        (scn, rng, medium, position, time, fp_to_meter, Tarad, Tref, &w);
    }
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

  setup_estimator(estimator, nrealisations, N, weight, sqr_weight);

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads)  {
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

res_T
sdis_solve_probe_boundary
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t iprim, /* Identifier of the primitive on which the probe lies */
   const double uv[2], /* Parametric coordinates of the probe onto the primitve */
   const double time, /* Observation time */
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

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !uv || time < 0
  || fp_to_meter <= 0 || Tref < 0 || (side != SDIS_FRONT && side != SDIS_BACK)
  || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check the primitive identifier */
  if(iprim >= scene_get_primitives_count(scn)) {
    log_err(scn->dev,
"%s: invalid primitive identifier `%lu'. It must be less than %lu.\n",
      FUNC_NAME,
      (unsigned long)iprim,
      (unsigned long)scene_get_primitives_count(scn));
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check parametric coordinates */
  if(scene_is_2d(scn)) {
    const double v = CLAMP(1.0 - uv[0], 0, 1);
    if(uv[0] < 0 || uv[0] > 1 || !eq_eps(uv[0] + v, 1, 1.e-6)) {
      log_err(scn->dev,
"%s: invalid parametric coordinates %g.\n"
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
"%s: invalid parametric coordinates [%g, %g].\n"
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

  /* Create the estimator */
  res = estimator_create(scn->dev, &estimator);
  if(res != RES_OK) goto error;

  /* Here we go! Launch the Monte Carlo estimation */
  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) reduction(+:weight,sqr_weight,N)
  for(irealisation = 0; irealisation < rcount; ++irealisation) {
    res_T res_local;
    double w = NaN;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    if(scene_is_2d(scn)) {
      res_local = boundary_realisation_2d
        (scn, rng, iprim, uv, time, side, fp_to_meter, Tarad, Tref, &w);
    } else {
      res_local = boundary_realisation_3d
        (scn, rng, iprim, uv, time, side, fp_to_meter, Tarad, Tref, &w);
    }
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

  setup_estimator(estimator, nrealisations, N, weight, sqr_weight);

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

res_T
sdis_solve_camera
  (struct sdis_scene* scn,
   const struct sdis_camera* cam,
   const double time,
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
   const size_t width, /* #pixels in X */
   const size_t height, /* #pixels in Y */
   const size_t spp, /* #samples per pixel */
   sdis_write_accums_T writer,
   void* writer_data)
{
  #define TILE_SIZE 32 /* definition in X & Y of a tile */
  STATIC_ASSERT(IS_POW2(TILE_SIZE), TILE_SIZE_must_be_a_power_of_2);

  const struct sdis_medium* medium = NULL;
  struct darray_accum* tiles = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  size_t ntiles_x, ntiles_y, ntiles;
  double pix_sz[2]; /* Size of a pixel in the normalized image plane */
  int64_t mcode; /* Morton code of a tile */
  size_t i;
  ATOMIC res = RES_OK;

  if(!scn || !cam || time < 0 || fp_to_meter <= 0 || Tref < 0 || !width
  || !height || !spp || !writer) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(scene_is_2d(scn)) {
    log_err(scn->dev, "%s: 2D scene are not supported.\n", FUNC_NAME);
    goto error;
  }

  /* Retrieve the medium in which the submitted position lies */
  res = scene_get_medium(scn, cam->position, NULL, &medium);
  if(res != RES_OK) goto error;

  if(medium->type != SDIS_FLUID) {
    log_err(scn->dev, "%s: the camera position `%g %g %g' is not in a fluid.\n",
      FUNC_NAME, SPLIT3(cam->position));
    res = RES_BAD_ARG;
    goto error;
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

  /* Allocate per thread buffer of accumulations */
  tiles = darray_tile_data_get(&scn->dev->tiles);
  ASSERT(darray_tile_size_get(&scn->dev->tiles) == scn->dev->nthreads);
  FOR_EACH(i, 0, scn->dev->nthreads) {
    const size_t naccums = TILE_SIZE * TILE_SIZE;
    res = darray_accum_resize(tiles+i, naccums);
    if(res != RES_OK) goto error;
  }

  ntiles_x = (width  + (TILE_SIZE-1)/*ceil*/)/TILE_SIZE;
  ntiles_y = (height + (TILE_SIZE-1)/*ceil*/)/TILE_SIZE;
  ntiles = round_up_pow2(MMAX(ntiles_x, ntiles_y));
  ntiles *= ntiles;

  pix_sz[0] = 1.0 / (double)width;
  pix_sz[1] = 1.0 / (double)height;

  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static, 1/*chunk size*/)
  for(mcode = 0; mcode < (int64_t)ntiles; ++mcode) {
    size_t tile_org[2] = {0, 0};
    size_t tile_sz[2] = {0, 0};
    const int ithread = omp_get_thread_num();
    struct sdis_accum* accums = NULL;
    struct ssp_rng* rng = rngs[ithread];
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue;

    tile_org[0] = morton2D_decode((uint32_t)(mcode>>0));
    if(tile_org[0] >= ntiles_x) continue; /* Discard tile */
    tile_org[1] = morton2D_decode((uint32_t)(mcode>>1));
    if(tile_org[1] >= ntiles_y) continue; /* Disaard tile */

    /* Setup the tile coordinates in the image plane */
    tile_org[0] *= TILE_SIZE;
    tile_org[1] *= TILE_SIZE;
    tile_sz[0] = MMIN(TILE_SIZE, width - tile_org[0]);
    tile_sz[1] = MMIN(TILE_SIZE, height - tile_org[1]);

    /* Fetch the accumulations buffer */
    accums = darray_accum_data_get(tiles+ithread);

    /* Draw the tile */
    res_local = solve_tile(scn, rng, medium, cam, time, fp_to_meter, Tarad,
      Tref, tile_org, tile_sz, spp, pix_sz, accums);
    if(res_local != RES_OK) {
      ATOMIC_SET(&res, res_local);
      continue;
    }

    /* Write the accumulations */
    res_local = writer(writer_data, tile_org, tile_sz, accums);
    if(res_local != RES_OK) {
      ATOMIC_SET(&res, res_local);
      continue;
    }
  }

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads)  {
      if(rngs[i]) SSP(rng_ref_put(rngs[i]));
    }
    MEM_RM(scn->dev->allocator, rngs);
  }
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  return (res_T)res;
error:
  goto exit;
}

res_T
sdis_solve_boundary
  (struct sdis_scene* scn,
   const size_t nrealisations, /* #realisations */
   const size_t primitives[], /* List of boundary primitives to handle */
   const enum sdis_side sides[], /* Per primitive side to consider */
   const size_t nprimitives, /* #primitives */
   const double time, /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
   struct sdis_estimator** out_estimator)
{
  struct boundary_context ctx;
  struct sdis_estimator* estimator = NULL;
  struct s3d_scene* s3d_scene = NULL;
  struct s3d_shape* s3d_shape = NULL;
  struct s3d_scene_view* s3d_view = NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  size_t i;
  size_t N = 0; /* #realisations that do not fail */
  double weight=0, sqr_weight=0;
  int64_t irealisation;
  ATOMIC res = RES_OK;

  if(!scn || !nrealisations || nrealisations > INT64_MAX || !primitives
  || !sides || !nprimitives || time < 0 || fp_to_meter < 0 || Tref < 0
  || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(scene_is_2d(scn)) {
    log_err(scn->dev, "%s: this function does not support 2D scene yet.\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Create the Star-3D shape setuped of the boundary */
  res = s3d_shape_create_mesh(scn->dev->s3d, &s3d_shape);
  if(res != RES_OK) goto error;

  /* Initialise the boundary shape with the triangles/segments of the
   * submitted primitives  */
  ctx.primitives = primitives;
  ctx.s3d_view = scn->s3d_view;
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT;
  vdata.get = get_position;
  res = s3d_mesh_setup_indexed_vertices(s3d_shape, (unsigned)nprimitives,
    get_indices, (unsigned)(nprimitives*3), &vdata, 1, &ctx);
  if(res != RES_OK) goto error;

  /* Create and setup the boundary Star-3D scene */
  res = s3d_scene_create(scn->dev->s3d, &s3d_scene);
  if(res != RES_OK) goto error;
  res = s3d_scene_attach_shape(s3d_scene, s3d_shape);
  if(res != RES_OK) goto error;
  res = s3d_scene_view_create(s3d_scene, S3D_SAMPLE, &s3d_view);
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

  /* Create the estimator */
  res = estimator_create(scn->dev, &estimator);
  if(res != RES_OK) goto error;

  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static) reduction(+:weight,sqr_weight,N)
  for(irealisation=0; irealisation<(int64_t)nrealisations; ++irealisation) {
    const int ithread = omp_get_thread_num();
    struct s3d_primitive prim;
    struct ssp_rng* rng = rngs[ithread];
    enum sdis_side side;
    size_t iprim;
    double w = NaN;
    double uv[2];
    float st[2];
    float r0, r1, r2;
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue; /* An error occurred */

    /* Sample a position onto the boundary */
    r0 = ssp_rng_canonical_float(rng);
    r1 = ssp_rng_canonical_float(rng);
    r2 = ssp_rng_canonical_float(rng);
    res_local = s3d_scene_view_sample(s3d_view, r0, r1, r2, &prim, st);
    if(res_local != RES_OK) { ATOMIC_SET(&res, res_local); continue; }

    /* Map from boundary scene to sdis scene */
    iprim = primitives[prim.prim_id];
    side = sides[prim.prim_id];
    d2_set_f2(uv, st);

    /* Invoke the boundary realisation */
    res_local = boundary_realisation_3d
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

  setup_estimator(estimator, nrealisations, N, weight, sqr_weight);

exit:
  if(s3d_scene) S3D(scene_ref_put(s3d_scene));
  if(s3d_shape) S3D(shape_ref_put(s3d_shape));
  if(s3d_view) S3D(scene_view_ref_put(s3d_view));
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

