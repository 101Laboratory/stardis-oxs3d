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
#include "sdis_device_c.h"
#include "sdis_log.h"

#include <rsys/cstr.h>
#include <rsys/logger.h>
#include <rsys/mem_allocator.h>

#include <star/s2d.h>
#include <star/s3d.h>

#include <omp.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
device_release(ref_T* ref)
{
  struct sdis_device* dev;
  ASSERT(ref);
  dev = CONTAINER_OF(ref, struct sdis_device, ref);
  if(dev->s2d_dev) S2D(device_ref_put(dev->s2d_dev));
  if(dev->s3d_dev) S3D(device_ref_put(dev->s3d_dev));
  if(dev->logger == &dev->logger__) logger_release(&dev->logger__);
  ASSERT(flist_name_is_empty(&dev->interfaces_names));
  ASSERT(flist_name_is_empty(&dev->media_names));
  flist_name_release(&dev->interfaces_names);
  flist_name_release(&dev->media_names);
  MEM_RM(dev->allocator, dev);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_device_create
  (struct logger* logger,
   struct mem_allocator* mem_allocator,
   const unsigned nthreads_hint,
   const int verbose,
   struct sdis_device** out_dev)
{
  struct logger* log = NULL;
  struct sdis_device* dev = NULL;
  struct mem_allocator* allocator = NULL;
  res_T res = RES_OK;

  if(nthreads_hint == 0 || !out_dev) {
    res = RES_BAD_ARG;
    goto error;
  }

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  dev = MEM_CALLOC(allocator, 1, sizeof(struct sdis_device));
  if(!dev) {
    if(verbose) {
      #define ERR_STR STR(FUNC_NAME)": could not allocate the Stardis device -- %s."
      if(logger) {
        logger_print(logger, LOG_ERROR, ERR_STR, res_to_cstr(res));
      } else {
        fprintf(stderr, MSG_ERROR_PREFIX ERR_STR, res_to_cstr(res));
      }
      #undef ERR_STR
    }
    res = RES_MEM_ERR;
    goto error;
  }
  dev->allocator = allocator;
  dev->verbose = verbose;
  dev->nthreads = MMIN(nthreads_hint, (unsigned)omp_get_num_procs());
  ref_init(&dev->ref);
  flist_name_init(allocator, &dev->interfaces_names);
  flist_name_init(allocator, &dev->media_names);

  if(logger) {
    dev->logger = logger;
  } else {
    setup_log_default(dev);
  }
  log_info(dev, "use %lu %s.\n", (unsigned long)dev->nthreads,
    dev->nthreads == 1 ? "thread" : "threads");

  res = s2d_device_create(log, allocator, 0, &dev->s2d_dev);
  if(res != RES_OK) {
    log_err(dev,
      "%s: could not create the Star-2D device on Stardis -- %s.\n",
      FUNC_NAME, res_to_cstr(res));
  }

  res = s3d_device_create(log, allocator, 0, &dev->s3d_dev);
  if(res != RES_OK) {
    log_err(dev,
      "%s: could not create the Star-3D device on Stardis -- %s.\n",
      FUNC_NAME, res_to_cstr(res));
    goto error;
  }

exit:
  if(out_dev) *out_dev = dev;
  return res;
error:
  if(dev) {
    SDIS(device_ref_put(dev));
    dev = NULL;
  }
  goto exit;
}

res_T
sdis_device_ref_get(struct sdis_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_get(&dev->ref);
  return RES_OK;
}

res_T
sdis_device_ref_put(struct sdis_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_put(&dev->ref, device_release);
  return RES_OK;
}

