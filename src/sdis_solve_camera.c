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

#include "sdis.h"
#include "sdis_c.h"
#include "sdis_camera.h"
#include "sdis_device_c.h"
#include "sdis_estimator_buffer_c.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_realisation.h"
#include "sdis_scene_c.h"
#include "sdis_tile.h"
#ifdef SDIS_ENABLE_MPI
  #include "sdis_mpi.h"
#endif

#include <rsys/clock_time.h>
#include <rsys/cstr.h>
#include <rsys/list.h>
#include <rsys/morton.h>

#include <omp.h>

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static res_T
check_solve_camera_args(const struct sdis_solve_camera_args* args)
{
  if(!args) return RES_BAD_ARG;
  if(!args->cam) return RES_BAD_ARG;

  /* Check the image resolution */
  if(!args->image_definition[0] || !args->image_definition[1]) {
    return RES_BAD_ARG;
  }

  /* Check the number of samples per pixel */
  if(!args->spp) {
    return RES_BAD_ARG;
  }

  /* Check the time range */
  if(args->time_range[0] < 0 || args->time_range[1] < args->time_range[0]) {
    return RES_BAD_ARG;
  }
  if(args->time_range[1] > DBL_MAX
  && args->time_range[0] != args->time_range[1]) {
    return RES_BAD_ARG;
  }

  /* Check the picard order */
  if(args->picard_order < 1) {
    return RES_BAD_ARG;
  }

  /* Check RNG type */
  if(!args->rng_state && args->rng_type >= SSP_RNG_TYPES_COUNT__) {
    return RES_BAD_ARG;
  }

  /* Check the diffusion algorithm */
  if((unsigned)args->diff_algo >= SDIS_DIFFUSION_ALGORITHMS_COUNT__) {
    return RES_BAD_ARG;
  }

  return RES_OK;
}

static res_T
solve_pixel
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   const unsigned enc_id,
   const struct sdis_camera* cam,
   const double time_range[2], /* Observation time */
   const size_t ipix[2], /* Pixel coordinate in the image space */
   const size_t nrealisations,
   const int register_paths, /* Combination of enum sdis_heat_path_flag */
   const double pix_sz[2], /* Pixel size in the normalized image plane */
   const size_t picard_order,
   const enum sdis_diffusion_algorithm diff_algo,
   struct sdis_estimator* estimator,
   struct pixel* pixel)
{
  struct sdis_heat_path* pheat_path = NULL;
  size_t irealisation;
  res_T res = RES_OK;
  ASSERT(scn && rng && cam && ipix && nrealisations);
  ASSERT(pix_sz && pix_sz[0] > 0 && pix_sz[1] > 0);
  ASSERT(pixel && time_range);

  pixel->acc_temp = ACCUM_NULL;
  pixel->acc_time = ACCUM_NULL;

  FOR_EACH(irealisation, 0, nrealisations) {
    struct ray_realisation_args realis_args = RAY_REALISATION_ARGS_NULL;
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
    realis_args.rng = rng;
    realis_args.enc_id = enc_id;
    realis_args.time = time;
    realis_args.picard_order = picard_order;
    realis_args.heat_path = pheat_path;
    realis_args.irealisation = (size_t)irealisation;
    realis_args.diff_algo = diff_algo;
    d3_set(realis_args.position, ray_pos);
    d3_set(realis_args.direction, ray_dir);
    res_simul = ray_realisation_3d(scn, &realis_args, &w);

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
        ASSERT(estimator);
        res = estimator_add_and_release_heat_path(estimator, pheat_path);
        if(res != RES_OK) goto error;
        pheat_path = NULL;
      }
    }

    /* Stop time registration */
    time_sub(&t0, time_current(&t1), &t0);

    if(res_simul == RES_OK) {
      /* Update pixel accumulators */
      const double usec = (double)time_val(&t0, TIME_NSEC) * 0.001;
      pixel->acc_temp.sum += w;
      pixel->acc_temp.sum2 += w*w;
      pixel->acc_temp.count += 1;
      pixel->acc_time.sum += usec;
      pixel->acc_time.sum2 += usec*usec;
      pixel->acc_time.count += 1;
    }
  }

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
   const unsigned enc_id,
   const struct sdis_camera* cam,
   const double time_range[2],
   const size_t tile_org[2], /* Origin of the tile in pixel space */
   const size_t tile_size[2], /* #pixels in the tile in X and Y */
   const size_t spp, /* #samples per pixel */
   const int register_paths, /* Combination of enum sdis_heat_path_flag */
   const double pix_sz[2], /* Pixel size in the normalized image plane */
   const size_t picard_order,
   const enum sdis_diffusion_algorithm diff_algo,
   struct sdis_estimator_buffer* buf,
   struct tile* tile)
{
  size_t mcode; /* Morton code of the tile pixel */
  size_t npixels;
  res_T res = RES_OK;
  ASSERT(scn && rng && cam && spp);
  ASSERT(tile_size && tile_size[0] && tile_size[1]);
  ASSERT(pix_sz && pix_sz[0] > 0 && pix_sz[1] > 0 && time_range);

  /* Adjust the #pixels to process them wrt a morton order */
  npixels = round_up_pow2(MMAX(tile_size[0], tile_size[1]));
  npixels *= npixels;

  FOR_EACH(mcode, 0, npixels) {
    struct pixel* pixel = NULL;
    struct sdis_estimator* estimator = NULL;
    uint16_t ipix_tile[2];
    size_t ipix_image[2];

    ipix_tile[0] = morton2D_decode_u16((uint32_t)(mcode>>0));
    if(ipix_tile[0] >= tile_size[0]) continue;
    ipix_tile[1] = morton2D_decode_u16((uint32_t)(mcode>>1));
    if(ipix_tile[1] >= tile_size[1]) continue;

    pixel = tile_at(tile, ipix_tile[0], ipix_tile[1]);

    /* Compute the pixel coordinates in image space */
    ipix_image[0] = ipix_tile[0] + tile_org[0];
    ipix_image[1] = ipix_tile[1] + tile_org[1];

    if(register_paths != SDIS_HEAT_PATH_NONE) {
      ASSERT(buf);
      estimator = estimator_buffer_grab(buf, ipix_image[0], ipix_image[1]);
    }
    res = solve_pixel
      (scn, rng, enc_id, cam, time_range, ipix_image, spp, register_paths,
       pix_sz, picard_order, diff_algo, estimator, pixel);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static INLINE res_T
setup_estimator_from_pixel
  (struct sdis_estimator* estimator,
   const size_t spp, /* #samples per pixel */
   struct pixel* pixel)
{
  ASSERT(estimator && spp && pixel);
  ASSERT(pixel->acc_temp.count == pixel->acc_time.count);
  estimator_setup_realisations_count
    (estimator, spp, pixel->acc_temp.count);
  estimator_setup_temperature
    (estimator, pixel->acc_temp.sum, pixel->acc_temp.sum2);
  estimator_setup_realisation_time
    (estimator, pixel->acc_time.sum, pixel->acc_time.sum2);
  return RES_OK;
}

static res_T
write_tile
  (struct sdis_estimator_buffer* buf,
   const size_t spp, /* #samples per pixel */
   struct tile* tile)
{
  res_T res = RES_OK;
  size_t tile_org[2];
  size_t buf_sz[2];
  size_t tile_sz[2];
  uint16_t x, y;
  ASSERT(buf && spp && tile);

  SDIS(estimator_buffer_get_definition(buf, buf_sz));

  tile_org[0] = (size_t)(tile->data.x * TILE_SIZE);
  tile_org[1] = (size_t)(tile->data.y * TILE_SIZE);
  tile_sz[0] = MMIN(TILE_SIZE, buf_sz[0] - tile_org[0]);
  tile_sz[1] = MMIN(TILE_SIZE, buf_sz[1] - tile_org[1]);

  FOR_EACH(y, 0, tile_sz[1]) {
    const size_t pix_y = tile_org[1] + y;
    FOR_EACH(x, 0, tile_sz[0]) {
      const size_t pix_x = tile_org[0] + x;
      struct sdis_estimator* estimator = NULL;
      struct pixel* pixel = NULL;

      estimator = estimator_buffer_grab(buf, pix_x, pix_y);
      pixel = tile_at(tile, x, y);

      res = setup_estimator_from_pixel(estimator, spp, pixel);
      if(res != RES_OK) goto error;
    }
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
write_list_of_tiles
  (struct sdis_estimator_buffer* buf,
   const size_t spp, /* #samples per pixel */
   struct list_node* tiles) /* Tiles to write */
{
  struct list_node* node = NULL;
  res_T res = RES_OK;
  ASSERT(buf && spp && tiles);

  LIST_FOR_EACH(node, tiles) {
    struct tile* tile = CONTAINER_OF(node, struct tile, node);
    res = write_tile(buf, spp, tile);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

#ifndef SDIS_ENABLE_MPI
static INLINE res_T
gather_tiles
  (struct sdis_device* dev,
   struct sdis_estimator_buffer* buf, /* NULL on non master processes */
   const size_t spp, /* #realisations per pixel */
   const size_t ntiles, /* Overall #tiles that must be written into `buf' */
   struct list_node* tiles) /* List of tiles of the current process */
{
  (void)dev, (void)ntiles;
  return write_list_of_tiles(buf, spp, tiles);
}
#else
/* Gather the tiles and write them into the estimator buffer. The master process
 * write its own tiles into the estimator buffer. Then, it gathers the tiles
 * send by non master processes and write them too into the estimator buffer */
static res_T
gather_tiles
  (struct sdis_device* dev,
   struct sdis_estimator_buffer* buf, /* NULL on non master processes */
   const size_t spp, /* #realisations per pixel */
   const size_t ntiles, /* Overall #tiles that must be written into `buf' */
   struct list_node* tiles) /* List of tiles of the current process */
{
  struct tile* tile_temp = NULL;
  struct list_node* node = NULL;
  res_T res = RES_OK;
  ASSERT(dev && spp && tiles);

  if(!dev->use_mpi) {
    res = write_list_of_tiles(buf, spp, tiles);
    if(res != RES_OK) goto error;
    goto exit; /* No more to do */
  }

  /* Non master process */
  if(dev->mpi_rank != 0) {

    /* Send to the master process the list of tiles solved by the current
     * process */
    LIST_FOR_EACH(node, tiles) {
      struct tile* tile = CONTAINER_OF(node, struct tile, node);
      mutex_lock(dev->mpi_mutex);
      MPI(Send(&tile->data, sizeof(tile->data), MPI_CHAR, 0, MPI_SDIS_MSG_TILE,
        MPI_COMM_WORLD));
      mutex_unlock(dev->mpi_mutex);
    }

  /* Master process */
  } else {
    size_t itile;
    size_t ntiles_master;

    /* Write into the buffer the tiles solved by the master process itself */
    res = write_list_of_tiles(buf, spp, tiles);
    if(res != RES_OK) goto error;

    /* Create a temporary tile to store the tile sent by the non master
     * processes */
    res = tile_create(dev->allocator, &tile_temp);
    if(res != RES_OK) {
      log_err(dev,
        "Could not allocate the tile to temporary store the tiles sent by "
        "the non master processes -- %s", res_to_cstr(res));
      goto error;
    }

    /* Count the number of tiles rendered onto the master process */
    ntiles_master = 0;
    LIST_FOR_EACH(node, tiles) ++ntiles_master;
    ASSERT(ntiles_master <= ntiles);

    /* Receive the remaining tiles sent by the non master processes */
    FOR_EACH(itile, ntiles_master, ntiles) {
      MPI_Request req;

      /* Asynchronously receive a tile */
      mutex_lock(dev->mpi_mutex);
      MPI(Irecv(&tile_temp->data, sizeof(tile_temp->data), MPI_CHAR,
        MPI_ANY_SOURCE, MPI_SDIS_MSG_TILE, MPI_COMM_WORLD, &req));
      mutex_unlock(dev->mpi_mutex);
      mpi_waiting_for_request(dev, &req);

      /* Write the tile into the estimator buffer */
      res = write_tile(buf, spp, tile_temp);
      if(res != RES_OK) goto error;
    }
  }

exit:
  if(tile_temp) tile_ref_put(tile_temp);
  return res;
error:
  goto exit;
}
#endif

/* Setup the accumulators of the whole estimator buffer */
static res_T
finalize_estimator_buffer
  (struct sdis_estimator_buffer* buf,
   struct ssp_rng_proxy* rng_proxy,
   const size_t spp) /* #samples per pixel */
{
  struct accum acc_temp = ACCUM_NULL;
  struct accum acc_time = ACCUM_NULL;
  size_t definition[2];
  size_t x, y;
  size_t nrealisations = 0;
  size_t nsuccesses = 0;
  res_T res = RES_OK;
  ASSERT(buf && rng_proxy && spp);

  SDIS(estimator_buffer_get_definition(buf, definition));

  FOR_EACH(y, 0, definition[1]) {
    FOR_EACH(x, 0, definition[0]) {
      const struct sdis_estimator* estimator;
      SDIS(estimator_buffer_at(buf, x, y, &estimator));
      acc_temp.sum += estimator->temperature.sum;
      acc_temp.sum2 += estimator->temperature.sum2;
      acc_temp.count += estimator->temperature.count;
      acc_time.sum += estimator->realisation_time.sum;
      acc_time.sum2 += estimator->realisation_time.sum2;
      acc_time.count += estimator->realisation_time.count;
      nsuccesses += estimator->nrealisations;
    }
  }

  nrealisations = definition[0]*definition[1]*spp;
  ASSERT(acc_temp.count == acc_time.count);
  ASSERT(acc_temp.count == nsuccesses);
  estimator_buffer_setup_realisations_count(buf, nrealisations, nsuccesses);
  estimator_buffer_setup_temperature(buf, acc_temp.sum, acc_temp.sum2);
  estimator_buffer_setup_realisation_time(buf, acc_time.sum, acc_time.sum2);
  res = estimator_buffer_save_rng_state(buf, rng_proxy);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

static void
free_rendered_tiles(struct list_node* tiles)
{
  struct list_node* node;
  struct list_node* tmp;
  ASSERT(tiles);
  LIST_FOR_EACH_SAFE(node, tmp, tiles) {
    struct tile* tile = CONTAINER_OF(node, struct tile, node);
    list_del(node);
    tile_ref_put(tile);
  }
}

/*******************************************************************************
 * Exported function
 ******************************************************************************/
res_T
sdis_solve_camera
  (struct sdis_scene* scn,
   const struct sdis_solve_camera_args* args,
   struct sdis_estimator_buffer** out_buf)
{
  /* Time registration */
  struct time time0, time1;
  char buffer[128]; /* Temporary buffer used to store formated time */

  /* Stardis variables */
  struct sdis_estimator_buffer* buf = NULL;

  /* Random number generators */
  struct ssp_rng_proxy* rng_proxy = NULL;
  struct ssp_rng** per_thread_rng = NULL;

  /* Enclosure & medium in which the probe lies */
  unsigned enc_id = ENCLOSURE_ID_NULL;

  /* Miscellaneous */
  size_t ntiles_x, ntiles_y, ntiles, ntiles_adjusted;
  size_t ntiles_proc; /* #tiles for the current proc */
  struct list_node tiles; /* List of tiles rendered by the process */
  double pix_sz[2]; /* Size of a pixel in the normalized image plane */
  int64_t mcode; /* Morton code of a tile */
  int64_t mcode_1st; /* morton code of the 1st tile computed by the process */
  int64_t mcode_incr; /* Increment toward the next morton code */
  int32_t* progress = NULL; /* Per process progress bar */
  int pcent_progress = 1; /* Percentage requiring progress update */
  int register_paths = SDIS_HEAT_PATH_NONE;
  int is_master_process = 1;
  ATOMIC nsolved_tiles = 0;
  ATOMIC res = RES_OK;

  list_init(&tiles);

  if(!scn || !out_buf) { res = RES_BAD_ARG; goto error; }
  res = check_solve_camera_args(args);
  if(res != RES_OK) goto error;

  if(scene_is_2d(scn)) {
    log_err(scn->dev, "%s: 2D scenes are not supported.\n", FUNC_NAME);
    goto error;
  }

  /* Retrieve the medium in which the submitted position lies */
  res = scene_get_enclosure_id(scn, args->cam->position, &enc_id);
  if(res != RES_OK) goto error;

  /* Create the per thread RNGs */
  res = create_per_thread_rng
    (scn->dev, args->rng_state, args->rng_type, &rng_proxy, &per_thread_rng);
  if(res != RES_OK) goto error;

  /* Allocate the per process progress status */
  res = alloc_process_progress(scn->dev, &progress);
  if(res != RES_OK) goto error;

  /* Compute the overall number of tiles */
  ntiles_x = (args->image_definition[0] + (TILE_SIZE-1)/*ceil*/)/TILE_SIZE;
  ntiles_y = (args->image_definition[1] + (TILE_SIZE-1)/*ceil*/)/TILE_SIZE;
  ntiles = ntiles_x * ntiles_y;
  ntiles_adjusted = round_up_pow2(MMAX(ntiles_x, ntiles_y));
  ntiles_adjusted *= ntiles_adjusted;

#ifdef SDIS_ENABLE_MPI
  is_master_process = !scn->dev->use_mpi || scn->dev->mpi_rank == 0;
  if(scn->dev->use_mpi) {
    mcode_1st = scn->dev->mpi_rank;
    mcode_incr = scn->dev->mpi_nprocs;

    /* Compute the #tiles of the current proc */
    ntiles_proc = ntiles / (size_t)scn->dev->mpi_nprocs;
    if(ntiles % (size_t)scn->dev->mpi_nprocs > (size_t)scn->dev->mpi_rank) {
      ++ntiles_proc;
    }
  } else
#endif
  {
    is_master_process = 1;
    mcode_1st = 0;
    mcode_incr = 1;
    ntiles_proc = ntiles;
  }

  /* Update the progress bar every percent if escape sequences are allowed in
   * log messages or only every 10 percent when only plain text is allowed.
   * This reduces the number of lines of plain text printed */
  pcent_progress = scn->dev->no_escape_sequence ? 10 : 1;

  /* Compute the normalized pixel size */
  pix_sz[0] = 1.0 / (double)args->image_definition[0];
  pix_sz[1] = 1.0 / (double)args->image_definition[1];

  /* Create the global estimator on the master process only */
  if(is_master_process) {
    res = estimator_buffer_create
      (scn->dev, args->image_definition[0], args->image_definition[1], &buf);
    if(res != RES_OK) goto error;
  }

  /* Synchronise the processes */
  process_barrier(scn->dev);

  #define PROGRESS_MSG "Rendering: "
  print_progress(scn->dev, progress, PROGRESS_MSG);

  /* Begin time registration of the computation */
  time_current(&time0);

  /* Here we go! Launch the Monte Carlo estimation */
  omp_set_num_threads((int)scn->dev->nthreads);
  register_paths = is_master_process
    ? args->register_paths : SDIS_HEAT_PATH_NONE;
  #pragma omp parallel for schedule(static, 1/*chunk size*/)
  for(mcode = mcode_1st; mcode < (int64_t)ntiles_adjusted; mcode+=mcode_incr) {
    size_t tile_org[2] = {0, 0};
    size_t tile_sz[2] = {0, 0};
    struct tile* tile = NULL;
    const int ithread = omp_get_thread_num();
    struct ssp_rng* rng = per_thread_rng[ithread];
    size_t n;
    int pcent;
    res_T res_local = RES_OK;

    if(ATOMIC_GET(&res) != RES_OK) continue;

    tile_org[0] = morton2D_decode_u16((uint32_t)(mcode>>0));
    if(tile_org[0] >= ntiles_x) continue; /* Discard tile */
    tile_org[1] = morton2D_decode_u16((uint32_t)(mcode>>1));
    if(tile_org[1] >= ntiles_y) continue; /* Discard tile */

    res_local = tile_create(scn->dev->allocator, &tile);
    if(tile == NULL) {
      log_err(scn->dev, "%s: error allocating the tile (%lu, %lu) -- %s\n",
        FUNC_NAME,
        (unsigned long)tile_org[0],
        (unsigned long)tile_org[1],
        res_to_cstr(res_local));
      ATOMIC_SET(&res, res_local);
      continue;
    }

    /* Register the tile */
    #pragma omp critical
    list_add_tail(&tiles, &tile->node);

    /* Setup the tile coordinates */
    tile->data.x = (uint16_t)tile_org[0];
    tile->data.y = (uint16_t)tile_org[1];

    /* Setup the tile coordinates in the image plane */
    tile_org[0] *= TILE_SIZE;
    tile_org[1] *= TILE_SIZE;
    tile_sz[0] = MMIN(TILE_SIZE, args->image_definition[0] - tile_org[0]);
    tile_sz[1] = MMIN(TILE_SIZE, args->image_definition[1] - tile_org[1]);

    /* Draw the tile */
    res_local = solve_tile
      (scn, rng, enc_id, args->cam, args->time_range, tile_org, tile_sz,
       args->spp, register_paths, pix_sz, args->picard_order, args->diff_algo,
       buf, tile);
    if(res_local != RES_OK) {
      ATOMIC_SET(&res, res_local);
      continue;
    }

    /* Update progress */
    n = (size_t)ATOMIC_INCR(&nsolved_tiles);
    pcent = (int)((double)n*100.0 / (double)ntiles_proc + 0.5/*round*/);
    #pragma omp critical
    if(pcent/pcent_progress > progress[0]/pcent_progress) {
      progress[0] = pcent;
      print_progress_update(scn->dev, progress, PROGRESS_MSG);
    }
  }

  /* Synchronise the processes */
  process_barrier(scn->dev);

  res = gather_res_T(scn->dev, (res_T)res);
  if(res != RES_OK) goto error;

  print_progress_completion(scn->dev, progress, PROGRESS_MSG);
  #undef PROGRESS_MSG

  /* Report computation time */
  time_sub(&time0, time_current(&time1), &time0);
  time_dump(&time0, TIME_ALL, NULL, buffer, sizeof(buffer));
  log_info(scn->dev, "Image rendered in %s.\n", buffer);

  /* Gather the RNG proxy sequence IDs and ensure that the RNG proxy state of
   * the master process is greater than the RNG proxy state of all other
   * processes */
  res = gather_rng_proxy_sequence_id(scn->dev, rng_proxy);
  if(res != RES_OK) goto error;

  time_current(&time0);

  res = gather_tiles(scn->dev, buf, args->spp, ntiles, &tiles);
  if(res != RES_OK) goto error;

  time_sub(&time0, time_current(&time1), &time0);
  time_dump(&time0, TIME_ALL, NULL, buffer, sizeof(buffer));
  log_info(scn->dev, "Image tiles gathered in %s.\n", buffer);

  if(is_master_process) {
    res = finalize_estimator_buffer(buf, rng_proxy, args->spp);
    if(res != RES_OK) goto error;
  }

exit:
  free_rendered_tiles(&tiles);
  if(per_thread_rng)release_per_thread_rng(scn->dev, per_thread_rng);
  if(progress) free_process_progress(scn->dev, progress);
  if(rng_proxy) SSP(rng_proxy_ref_put(rng_proxy));
  if(out_buf) *out_buf = buf;
  return (res_T)res;
error:
  if(buf) { SDIS(estimator_buffer_ref_put(buf)); buf = NULL; }
  goto exit;
}
