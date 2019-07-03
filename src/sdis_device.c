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

#include <rsys/logger.h>
#include <rsys/mem_allocator.h>

#include <star/s2d.h>
#include <star/s3d.h>

#include <omp.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
log_msg
  (struct sdis_device* dev,
   const enum log_type stream,
   const char* msg,
   va_list vargs)
{
  ASSERT(dev && msg);
  if(dev->verbose) {
    res_T res; (void)res;
    res = logger_vprint(dev->logger, stream, msg, vargs);
    ASSERT(res == RES_OK);
  }
}

static void
device_release(ref_T* ref)
{
  struct sdis_device* dev;
  ASSERT(ref);
  dev = CONTAINER_OF(ref, struct sdis_device, ref);
  if(dev->s2d) S2D(device_ref_put(dev->s2d));
  if(dev->s3d) S3D(device_ref_put(dev->s3d));
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

  log = logger ? logger : LOGGER_DEFAULT;
  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  dev = MEM_CALLOC(allocator, 1, sizeof(struct sdis_device));
  if(!dev) {
    if(verbose) {
      /* Do not use helper log functions since dev is not initialised */
      CHK(logger_print(log, LOG_ERROR,
        "%s: could not allocate the Stardis device.\n", FUNC_NAME) == RES_OK);
    }
    res = RES_MEM_ERR;
    goto error;
  }
  dev->logger = log;
  dev->allocator = allocator;
  dev->verbose = verbose;
  dev->nthreads = MMIN(nthreads_hint, (unsigned)omp_get_num_procs());
  ref_init(&dev->ref);
  flist_name_init(allocator, &dev->interfaces_names);
  flist_name_init(allocator, &dev->media_names);

  res = s2d_device_create(log, allocator, 0, &dev->s2d);
  if(res != RES_OK) {
    log_err(dev,
      "%s: could not create the Star-2D device on Stardis.\n", FUNC_NAME);
  }

  res = s3d_device_create(log, allocator, 0, &dev->s3d);
  if(res != RES_OK) {
    log_err(dev,
      "%s: could not create the Star-3D device on Stardis.\n", FUNC_NAME);
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

/*******************************************************************************
 * Local functions
 ******************************************************************************/
void
log_err(struct sdis_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

  va_start(vargs_list, msg);
  log_msg(dev, LOG_ERROR, msg, vargs_list);
  va_end(vargs_list);
}

void
log_warn(struct sdis_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

  va_start(vargs_list, msg);
  log_msg(dev, LOG_WARNING, msg, vargs_list);
  va_end(vargs_list);
}

