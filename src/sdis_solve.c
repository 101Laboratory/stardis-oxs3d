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

#include "sdis.h"
#include "sdis_camera.h"
#include "sdis_device_c.h"
#include "sdis_estimator_c.h"
#include "sdis_estimator_buffer_c.h"
#include "sdis_interface_c.h"

#include <star/ssp.h>
#include <omp.h>

/* Generate the probe solvers */
#define SDIS_XD_DIMENSION 2
#include "sdis_solve_probe_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_solve_probe_Xd.h"

/* Generate the probe boundary solvers */
#define SDIS_XD_DIMENSION 2
#include "sdis_solve_probe_boundary_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_solve_probe_boundary_Xd.h"

/* Generate the boundary solvers */
#define SDIS_XD_DIMENSION 2
#include "sdis_solve_boundary_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_solve_boundary_Xd.h"

/* Generate the medium solvers */
#define SDIS_XD_DIMENSION 2
#include "sdis_solve_medium_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_solve_medium_Xd.h"

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
   const double time_range[2], /* Observation time */
   const double fp_to_meter, /* Scale from floating point units to meters */
   const double Tarad, /* In Kelvin */
   const double Tref, /* In Kelvin */
   const size_t ipix[2], /* Pixel coordinate in the image plane */
   const size_t nrealisations,
   const int register_paths, /* Combination of enum sdis_heat_path_flag */
   const double pix_sz[2], /* Pixel size in the normalized image plane */
   struct sdis_estimator* estimator)
{
  struct accum acc_temp = ACCUM_NULL;
  struct accum acc_time = ACCUM_NULL;
  struct sdis_heat_path* pheat_path = NULL;
  size_t irealisation;
  res_T res = RES_OK;
  ASSERT(scn && mdm && rng && cam && ipix && nrealisations && Tref >= 0);
  ASSERT(pix_sz && pix_sz[0] > 0 && pix_sz[1] > 0);
  ASSERT(estimator && time_range);

  FOR_EACH(irealisation, 0, nrealisations) {
    struct time t0, t1;
    double samp[2]; /* Pixel sample */
    double ray_pos[3];
    double ray_dir[3];
    double w = 0;
    struct sdis_heat_path heat_path;
    double time;
    res_T res_simul = RES_OK;

    /* Begin time registration */
    time_current(&t0);

    time = sample_time(rng, time_range);
    if(register_paths) {
      heat_path_init(scn->dev->allocator, &heat_path);
      pheat_path = &heat_path;
    }

    /* Generate a sample into the pixel to estimate */
    samp[0] = ((double)ipix[0] + ssp_rng_canonical(rng)) * pix_sz[0];
    samp[1] = ((double)ipix[1] + ssp_rng_canonical(rng)) * pix_sz[1];

    /* Generate a ray starting from the camera position and passing through
     * pixel sample */
    camera_ray(cam, samp, ray_pos, ray_dir);

    /* Launch the realisation */
    res_simul = ray_realisation_3d(scn, rng, mdm, ray_pos, ray_dir,
      time, fp_to_meter, Tarad, Tref, pheat_path, &w);

    /* Handle fatal error */
    if(res_simul != RES_OK && res_simul != RES_BAD_OP) {
      res = res_simul;
      goto error;
    }

    if(pheat_path) {
      pheat_path->status = res_simul == RES_OK
        ? SDIS_HEAT_PATH_SUCCESS
        : SDIS_HEAT_PATH_FAILURE;

      /* Check if the path must be saved regarding the register_paths mask */
      if(!(register_paths & (int)pheat_path->status)) {
        heat_path_release(pheat_path);
        pheat_path = NULL;
      } else { /* Register the sampled path */
        res = estimator_add_and_release_heat_path(estimator, pheat_path);
        if(res != RES_OK) goto error;
        pheat_path = NULL;
      }
    }

    /* Stop time registration */
    time_sub(&t0, time_current(&t1), &t0);

    if(res_simul == RES_OK) {
      /* Update global accumulators */
      const double usec = (double)time_val(&t0, TIME_NSEC) * 0.001;
      acc_temp.sum += w;    acc_temp.sum2 += w*w;       ++acc_temp.count;
      acc_time.sum += usec; acc_time.sum2 += usec*usec; ++acc_time.count;
    }
  }

  /* Setup the pixel estimator */
  ASSERT(acc_temp.count == acc_time.count);
  estimator_setup_realisations_count(estimator, nrealisations, acc_temp.count);
  estimator_setup_temperature(estimator, acc_temp.sum, acc_temp.sum2);
  estimator_setup_realisation_time(estimator, acc_time.sum, acc_time.sum2);

exit:
  if(pheat_path) heat_path_release(pheat_path);
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
   const double time_range[2],
   const double fp_to_meter,
   const double Tarad,
   const double Tref,
   const size_t origin[2], /* Tile origin in image plane */
   const size_t size[2], /* #pixels in X and Y */
   const size_t spp, /* #samples per pixel */
   const int register_paths, /* Combination of enum sdis_heat_path_flag */
   const double pix_sz[2], /* Pixel size in the normalized image plane */
   struct sdis_estimator_buffer* buf)
{
  size_t mcode; /* Morton code of the tile pixel */
  size_t npixels;
  res_T res = RES_OK;
  ASSERT(scn && rng && mdm && cam && spp && origin && Tref >= 0);
  ASSERT(size &&size[0] && size[1] && buf);
  ASSERT(pix_sz && pix_sz[0] > 0 && pix_sz[1] > 0 && time_range);

  /* Adjust the #pixels to process them wrt a morton order */
  npixels = round_up_pow2(MMAX(size[0], size[1]));
  npixels *= npixels;

  FOR_EACH(mcode, 0, npixels) {
    size_t ipix[2];
    struct sdis_estimator* estimator;

    ipix[0] = morton2D_decode((uint32_t)(mcode>>0));
    if(ipix[0] >= size[0]) continue;
    ipix[1] = morton2D_decode((uint32_t)(mcode>>1));
    if(ipix[1] >= size[1]) continue;

    ipix[0] = ipix[0] + origin[0];
    ipix[1] = ipix[1] + origin[1];

    /* Fetch the pixel estimator */
    estimator = estimator_buffer_grab(buf, ipix[0], ipix[1]);

    res = solve_pixel(scn, rng, mdm, cam, time_range, fp_to_meter, Tarad, Tref,
      ipix, spp, register_paths, pix_sz, estimator);
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
   const struct sdis_solve_probe_args* args,
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_2d(scn, args, NULL, out_estimator);
  } else {
    return solve_probe_3d(scn, args, NULL, out_estimator);
  }
}

res_T
sdis_solve_probe_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_args* args,
   struct sdis_green_function** out_green)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_2d(scn, args, out_green, NULL);
  } else {
    return solve_probe_3d(scn, args, out_green, NULL);
  }
}

res_T
sdis_solve_probe_boundary
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_args* args,
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_boundary_2d(scn, args, NULL, out_estimator);
  } else {
    return solve_probe_boundary_3d(scn, args, NULL, out_estimator);
  }
}

res_T
sdis_solve_probe_boundary_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_args* args,
   struct sdis_green_function** green)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_boundary_2d(scn, args, green, NULL);
  } else {
    return solve_probe_boundary_3d(scn, args, green, NULL);
  }
}

res_T
sdis_solve_boundary
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_args* args,
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_boundary_2d(scn, args, NULL, out_estimator);
  } else {
    return solve_boundary_3d(scn, args, NULL, out_estimator);
  }
}

res_T
sdis_solve_boundary_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_args* args,
   struct sdis_green_function** green)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_boundary_2d(scn, args, green, NULL);
  } else {
    return solve_boundary_3d(scn, args, green, NULL);
  }
}

res_T
sdis_solve_probe_boundary_flux
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_flux_args* args,
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_probe_boundary_flux_2d(scn, args, out_estimator);
  } else {
    return solve_probe_boundary_flux_3d(scn, args, out_estimator);
  }
}

res_T
sdis_solve_boundary_flux
  (struct sdis_scene* scn,
   const struct sdis_solve_boundary_flux_args* args,
   struct sdis_estimator** out_estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_boundary_flux_2d(scn, args, out_estimator);
  } else {
    return solve_boundary_flux_3d(scn, args, out_estimator);
  }
}

res_T
sdis_solve_camera
  (struct sdis_scene* scn,
   const struct sdis_solve_camera_args* args,
   struct sdis_estimator_buffer** out_buf)
{
  #define TILE_SIZE 32 /* definition in X & Y of a tile */
  STATIC_ASSERT(IS_POW2(TILE_SIZE), TILE_SIZE_must_be_a_power_of_2);

  struct sdis_estimator_buffer* buf = NULL;
  struct sdis_medium* medium = NULL;
  struct accum acc_temp = ACCUM_NULL;
  struct accum acc_time = ACCUM_NULL;
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** rngs = NULL;
  size_t ntiles_x, ntiles_y, ntiles;
  double pix_sz[2]; /* Size of a pixel in the normalized image plane */
  int64_t mcode; /* Morton code of a tile */
  size_t nrealisations;
  size_t nsuccesses;
  size_t ix, iy;
  size_t i;
  int progress = 0;
  ATOMIC nsolved_tiles = 0;
  ATOMIC res = RES_OK;

  if(!scn
  || !args
  || !out_buf
  || !args->cam
  || args->fp_to_meter <= 0
  || !args->image_resolution[0]
  || !args->image_resolution[1]
  || !args->spp
  || args->ambient_radiative_temperature < 0
  || args->reference_temperature < 0
  || args->time_range[0] < 0
  || args->time_range[1] < args->time_range[0]
  || (  args->time_range[1] > DBL_MAX
     && args->time_range[0] != args->time_range[1])) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(scene_is_2d(scn)) {
    log_err(scn->dev, "%s: 2D scene are not supported.\n", FUNC_NAME);
    goto error;
  }

  /* Retrieve the medium in which the submitted position lies */
  res = scene_get_medium(scn, args->cam->position, NULL, &medium);
  if(res != RES_OK) goto error;

  if(medium->type != SDIS_FLUID) {
    log_err(scn->dev, "%s: the camera position `%g %g %g' is not in a fluid.\n",
      FUNC_NAME, SPLIT3(args->cam->position));
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

  ntiles_x = (args->image_resolution[0] + (TILE_SIZE-1)/*ceil*/)/TILE_SIZE;
  ntiles_y = (args->image_resolution[1] + (TILE_SIZE-1)/*ceil*/)/TILE_SIZE;
  ntiles = round_up_pow2(MMAX(ntiles_x, ntiles_y));
  ntiles *= ntiles;

  pix_sz[0] = 1.0 / (double)args->image_resolution[0];
  pix_sz[1] = 1.0 / (double)args->image_resolution[1];

  /* Create the global estimator */
  res = estimator_buffer_create
    (scn->dev, args->image_resolution[0], args->image_resolution[1], &buf);
  if(res != RES_OK) goto error;

  omp_set_num_threads((int)scn->dev->nthreads);
  #pragma omp parallel for schedule(static, 1/*chunk size*/)
  for(mcode = 0; mcode < (int64_t)ntiles; ++mcode) {
    size_t tile_org[2] = {0, 0};
    size_t tile_sz[2] = {0, 0};
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = rngs[ithread];
    size_t n;
    int pcent;
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue;

    tile_org[0] = morton2D_decode((uint32_t)(mcode>>0));
    if(tile_org[0] >= ntiles_x) continue; /* Discard tile */
    tile_org[1] = morton2D_decode((uint32_t)(mcode>>1));
    if(tile_org[1] >= ntiles_y) continue; /* Disaard tile */

    /* Setup the tile coordinates in the image plane */
    tile_org[0] *= TILE_SIZE;
    tile_org[1] *= TILE_SIZE;
    tile_sz[0] = MMIN(TILE_SIZE, args->image_resolution[0] - tile_org[0]);
    tile_sz[1] = MMIN(TILE_SIZE, args->image_resolution[1] - tile_org[1]);

    /* Draw the tile */
    res_local = solve_tile(scn, rng, medium, args->cam, args->time_range,
      args->fp_to_meter, args->ambient_radiative_temperature,
      args->reference_temperature, tile_org, tile_sz, args->spp,
      args->register_paths, pix_sz, buf);
    if(res_local != RES_OK) {
      ATOMIC_SET(&res, res_local);
      continue;
    }

    /* Update progress */
    n = (size_t)ATOMIC_INCR(&nsolved_tiles);
    pcent = (int)((double)n*100.0 / (double)(ntiles_x*ntiles_y) + 0.5/*round*/);
    #pragma omp critical
    if(pcent > progress) {
      progress = pcent;
      log_info(scn->dev, "Infrared rendering: %3d%%\r", progress);
    }
  }
  if(res != RES_OK) goto error;

  /* Add a new line after the progress status */
  log_info(scn->dev, "Infrared rendering: %3d%%\n", progress);

  /* Setup the accumulators of the whole estimator buffer */
  acc_temp = ACCUM_NULL;
  acc_time = ACCUM_NULL;
  nsuccesses = 0;
  FOR_EACH(iy, 0, args->image_resolution[1]) {
    FOR_EACH(ix, 0, args->image_resolution[0]) {
      const struct sdis_estimator* estimator;
      SDIS(estimator_buffer_at(buf, ix, iy, &estimator));
      acc_temp.sum += estimator->temperature.sum;
      acc_temp.sum2 += estimator->temperature.sum2;
      acc_temp.count += estimator->temperature.count;
      acc_time.sum += estimator->realisation_time.sum;
      acc_time.sum2 += estimator->realisation_time.sum2;
      acc_time.count += estimator->realisation_time.count;
      nsuccesses += estimator->nrealisations;
    }
  }

  nrealisations = args->image_resolution[0]*args->image_resolution[1]*args->spp;
  ASSERT(acc_temp.count == acc_time.count);
  ASSERT(acc_temp.count == nsuccesses);
  estimator_buffer_setup_realisations_count(buf, nrealisations, nsuccesses);
  estimator_buffer_setup_temperature(buf, acc_temp.sum, acc_temp.sum2);
  estimator_buffer_setup_realisation_time(buf, acc_time.sum, acc_time.sum2);
  res = estimator_buffer_save_rng_state(buf, rng_proxy);
  if(res != RES_OK) goto error;

exit:
  if(rngs) {
    FOR_EACH(i, 0, scn->dev->nthreads)  {
      if(rngs[i]) SSP(rng_ref_put(rngs[i]));
    }
    MEM_RM(scn->dev->allocator, rngs);
  }
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_buf) *out_buf = buf;
  return (res_T)res;
error:
  if(buf) SDIS(estimator_buffer_ref_put(buf));
  goto exit;
}

res_T
sdis_solve_medium
  (struct sdis_scene* scn,
   const struct sdis_solve_medium_args* args,
   struct sdis_estimator** estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_medium_2d(scn, args, NULL, estimator);
  } else {
    return solve_medium_3d(scn, args, NULL, estimator);
  }
}

res_T
sdis_solve_medium_green_function
  (struct sdis_scene* scn,
   const struct sdis_solve_medium_args* args,
   struct sdis_green_function** green)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return solve_medium_2d(scn, args, green, NULL);
  } else {
    return solve_medium_3d(scn, args, green, NULL);
  }
}

res_T
sdis_compute_mean_power
  (struct sdis_scene* scn,
   const struct sdis_compute_mean_power_args* args,
   struct sdis_estimator** estimator)
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return compute_mean_power_2d(scn, args, estimator);
  } else {
    return compute_mean_power_3d(scn, args, estimator);
  }
}
