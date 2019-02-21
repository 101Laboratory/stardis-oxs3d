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

#include "sdis.h"
#include "sdis_camera.h"
#include "sdis_device_c.h"
#include "sdis_estimator_c.h"
#include "sdis_interface_c.h"

/* Generate the solvers */
#define SDIS_XD_DIMENSION 2
#include "sdis_solve_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_solve_Xd.h"

#include <star/ssp.h>
#include <omp.h>

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

static res_T
solve_pixel
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct sdis_medium* mdm,
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
  ASSERT(scn && mdm && rng && cam && ipix && nrealisations && Tref >= 0);
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
   struct sdis_medium* mdm,
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
  ASSERT(scn && rng && mdm && cam && spp && origin && accums && Tref >= 0);
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
   const double time_range[2],
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   const double Tarad, /* Ambient radiative temperature */
   const double Tref, /* Reference temperature */
   const int register_paths, /* Combination of enum sdis_heat_path_flag */
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_2d(scn, nrealisations, position, time_range,
      fp_to_meter, Tarad, Tref, register_paths, NULL, out_estimator);
  } else {
    return solve_probe_3d(scn, nrealisations, position, time_range,
      fp_to_meter, Tarad, Tref, register_paths, NULL, out_estimator);
  }
}

res_T
sdis_solve_probe_green_function
  (struct sdis_scene* scn,
   const size_t nrealisations,
   const double position[3],
   const double fp_to_meter,/* Scale factor from floating point unit to meter */
   const double Tarad, /* Ambient radiative temperature */
   const double Tref, /* Reference temperature */
   struct sdis_green_function** out_green)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_2d(scn, nrealisations, position, NULL,
      fp_to_meter, Tarad, Tref, SDIS_HEAT_PATH_NONE, out_green, NULL);
  } else {
    return solve_probe_3d(scn, nrealisations, position, NULL,
      fp_to_meter, Tarad, Tref, SDIS_HEAT_PATH_NONE, out_green, NULL);
  }
}

res_T
sdis_solve_probe_boundary
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
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_boundary_2d(scn, nrealisations, iprim, uv, time_range,
      side, fp_to_meter, Tarad, Tref, out_estimator);
  } else {
    return solve_probe_boundary_3d(scn, nrealisations, iprim, uv, time_range,
      side, fp_to_meter, Tarad, Tref, out_estimator);
  }
}

res_T
sdis_solve_boundary
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
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_boundary_2d(scn, nrealisations, primitives, sides, nprimitives,
      time_range, fp_to_meter, Tarad, Tref, out_estimator);
  } else {
    return solve_boundary_3d(scn, nrealisations, primitives, sides, nprimitives,
      time_range, fp_to_meter, Tarad, Tref, out_estimator);
  }
}

res_T
sdis_solve_probe_boundary_flux
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
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_boundary_flux_2d(scn, nrealisations, iprim, uv,
      time_range, fp_to_meter, Tarad, Tref, out_estimator);
  } else {
    return solve_probe_boundary_flux_3d(scn, nrealisations, iprim, uv,
      time_range, fp_to_meter, Tarad, Tref, out_estimator);
  }
}

res_T
sdis_solve_boundary_flux
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
  res_T res = RES_OK;
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    res = solve_boundary_flux_2d(scn, nrealisations, primitives, nprimitives,
      time_range, fp_to_meter, Tarad, Tref, out_estimator);
  } else {
    res = solve_boundary_flux_3d(scn, nrealisations, primitives, nprimitives,
      time_range, fp_to_meter, Tarad, Tref, out_estimator);
  }
  return res;
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

  struct sdis_medium* medium = NULL;
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

