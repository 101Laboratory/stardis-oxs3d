/* Copyright (C) 2018, 2020-2025 |Méso|Star> (contact@meso-star.com)
 * Copyright (C) 2018 Université Paul Sabatier
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

#include "svx.h"
#include "svx_device.h"

#include <rsys/logger.h>
#include<rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
log_msg
  (const struct svx_device* dev,
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
  struct svx_device* dev;
  ASSERT(ref);
  dev = CONTAINER_OF(ref, struct svx_device, ref);
  MEM_RM(dev->allocator, dev);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
svx_device_create
  (struct logger* log,
   struct mem_allocator* mem_allocator,
   const int verbose,
   struct svx_device** out_dev)
{
  struct svx_device* dev = NULL;
  struct mem_allocator* allocator = NULL;
  struct logger* logger = NULL;
  res_T res = RES_OK;

  if(!out_dev) {
    res = RES_BAD_ARG;
    goto error;
  }

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  logger = log ? log : LOGGER_DEFAULT;

  dev = MEM_CALLOC(allocator, 1, sizeof(struct svx_device));
  if(!dev) {
    res = RES_MEM_ERR;
    goto error;
  }


  ref_init(&dev->ref);
  dev->allocator = allocator;
  dev->logger = logger;
  dev->verbose = verbose;

exit:
  if(out_dev) *out_dev = dev;
  return res;
error:
  if(dev) {
    SVX(device_ref_put(dev));
    dev = NULL;
  }
  goto exit;
}

res_T
svx_device_ref_get(struct svx_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_get(&dev->ref);
  return RES_OK;
}

res_T
svx_device_ref_put(struct svx_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_put(&dev->ref, device_release);
  return RES_OK;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
void
log_err(const struct svx_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

  va_start(vargs_list, msg);
  log_msg(dev, LOG_ERROR, msg, vargs_list);
  va_end(vargs_list);
}

