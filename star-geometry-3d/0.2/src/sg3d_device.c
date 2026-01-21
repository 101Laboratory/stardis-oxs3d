/* Copyright (C) 2019, 2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#include "sg3d.h"
#include "sg3d_device.h"

#include <rsys/logger.h>
#include <rsys/mem_allocator.h>

#include <stdarg.h>

/******************************************************************************
 * Helper functions
 *****************************************************************************/
static void
log_msg
  (struct sg3d_device* dev,
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
  struct sg3d_device* dev;
  ASSERT(ref);
  dev = CONTAINER_OF(ref, struct sg3d_device, ref);
  MEM_RM(dev->allocator, dev);
}

/******************************************************************************
 * Exported functions
 *****************************************************************************/
res_T
sg3d_device_create
  (struct logger* logger,
   struct mem_allocator* mem_allocator,
   const int verbose,
   struct sg3d_device** out_dev)
{
  struct logger* log = NULL;
  struct sg3d_device* dev = NULL;
  struct mem_allocator* allocator = NULL;
  res_T res = RES_OK;

  if(!out_dev) {
    res = RES_BAD_ARG;
    goto error;
  }

  log = logger ? logger : LOGGER_DEFAULT;
  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  dev = MEM_CALLOC(allocator, 1, sizeof(struct sg3d_device));
  if(!dev) {
    if(verbose) {
      /* Do not use helper log functions since dev is not initialised */
      CHK(logger_print(log, LOG_ERROR,
        "%s: could not allocate the star-geometry-3d device.\n", FUNC_NAME)
        == RES_OK);
    }
    res = RES_MEM_ERR;
    goto error;
  }
  dev->logger = log;
  dev->allocator = allocator;
  dev->verbose = verbose;
  ref_init(&dev->ref);

exit:
  if(out_dev) *out_dev = dev;
  return res;
error:
  if(dev) {
    SG3D(device_ref_put(dev));
    dev = NULL;
  }
  goto exit;
}

res_T
sg3d_device_ref_get(struct sg3d_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_get(&dev->ref);
  return RES_OK;
}

res_T
sg3d_device_ref_put(struct sg3d_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_put(&dev->ref, device_release);
  return RES_OK;
}

/******************************************************************************
 * Local functions
 *****************************************************************************/
void
log_err(struct sg3d_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

  va_start(vargs_list, msg);
  log_msg(dev, LOG_ERROR, msg, vargs_list);
  va_end(vargs_list);
}

void
log_warn(struct sg3d_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

  va_start(vargs_list, msg);
  log_msg(dev, LOG_WARNING, msg, vargs_list);
  va_end(vargs_list);
}
