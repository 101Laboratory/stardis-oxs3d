/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#include "suvm.h"
#include "suvm_c.h"
#include "suvm_device.h"

#include <rsys/logger.h>
#include <rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
log_msg
  (const struct suvm_device* dev,
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
  struct suvm_device* dev;
  ASSERT(ref);
  dev = CONTAINER_OF(ref, struct suvm_device, ref);
  if(dev->rtc) rtcReleaseDevice(dev->rtc);
  MEM_RM(dev->allocator, dev);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
suvm_device_create
  (struct logger* log,
   struct mem_allocator* mem_allocator,
   int verbose,
   struct suvm_device** out_dev)
{
  char embree_opts[512];
  struct suvm_device* dev = NULL;
  struct mem_allocator* allocator = NULL;
  struct logger* logger = NULL;
  int sz;
  res_T res = RES_OK;

  if(!out_dev) {
    res = RES_BAD_ARG;
    goto error;
  }

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  logger = log ? log : LOGGER_DEFAULT;

  dev = MEM_CALLOC(allocator, 1, sizeof(*dev));
  if(!dev) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&dev->ref);
  dev->allocator = allocator;
  dev->logger = logger;
  dev->verbose = verbose;

  sz = snprintf(embree_opts, sizeof(embree_opts), "verbose=%d", verbose > 1 ? 1 : 0);
  if((size_t)sz >= sizeof(embree_opts)) {
    log_err(dev, "Could not setup the Embree option string.\n");
    res = RES_MEM_ERR;
    goto error;
  }

  dev->rtc = rtcNewDevice(embree_opts);
  if(dev->rtc == NULL) {
    const enum RTCError err = rtcGetDeviceError(NULL);
    log_err(dev, "Could not create the Embree device -- %s.\n",
      rtc_error_string(err));
    res = rtc_error_to_res_T(err);
    goto error;
  }

exit:
  if(out_dev) *out_dev = dev;
  return res;
error:
  if(dev) {
    SUVM(device_ref_put(dev));
    dev = NULL;
  }
  goto exit;
}

res_T
suvm_device_ref_get(struct suvm_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_get(&dev->ref);
  return RES_OK;
}

res_T
suvm_device_ref_put(struct suvm_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_put(&dev->ref, device_release);
  return RES_OK;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
void
log_info(struct suvm_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

  va_start(vargs_list, msg);
  log_msg(dev, LOG_OUTPUT, msg, vargs_list);
  va_end(vargs_list);
}

void
log_err(struct suvm_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

  va_start(vargs_list, msg);
  log_msg(dev, LOG_ERROR, msg, vargs_list);
  va_end(vargs_list);
}

void
log_warn(struct suvm_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

  va_start(vargs_list, msg);
  log_msg(dev, LOG_WARNING, msg, vargs_list);
  va_end(vargs_list);
}

