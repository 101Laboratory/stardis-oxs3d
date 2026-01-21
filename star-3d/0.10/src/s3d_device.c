/* Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)
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

#include "s3d.h"
#include "s3d_c.h"

#include "s3d_backend.h"
#include "s3d_device_c.h"

#include <rsys/logger.h>
#include <rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE void
rtc_error_func(void* context, enum RTCError err, const char* str)
{
  (void)str, (void)context;
  VFATAL("Embree:error: %s\n", ARG1(rtc_error_string(err)));
}

static INLINE void
log_msg
  (struct s3d_device* dev,
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
  struct s3d_device* dev;
  ASSERT(ref);
  dev = CONTAINER_OF(ref, struct s3d_device, ref);
  ASSERT(flist_name_is_empty(&dev->names) == 1);
  flist_name_release(&dev->names);
  rtcReleaseDevice(dev->rtc);
  MEM_RM(dev->allocator, dev);
}

/*******************************************************************************
 * Exported s3d_device functions
 ******************************************************************************/
res_T
s3d_device_create
  (struct logger* logger,
   struct mem_allocator* mem_allocator,
   const int verbose,
   struct s3d_device** out_dev)
{
  char embree_opts[512];
  struct s3d_device* dev = NULL;
  struct mem_allocator* allocator;
  const int verbosity = MMAX(MMIN(verbose, 3), 0);
  int sz;
  res_T res = RES_OK;

  if(!out_dev) {
    res = RES_BAD_ARG;
    goto error;
  }

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  dev = (struct s3d_device*)MEM_CALLOC(allocator, 1, sizeof(struct s3d_device));
  if(!dev) {
    res = RES_MEM_ERR;
    goto error;
  }
  dev->logger = logger ? logger : LOGGER_DEFAULT;
  dev->allocator = allocator;
  dev->verbose = verbose;
  flist_name_init(allocator, &dev->names);
  ref_init(&dev->ref);

  sz = snprintf(embree_opts, sizeof(embree_opts), "verbose=%d", verbosity);
  if((size_t)sz >= sizeof(embree_opts)) {
    log_error(dev, "Could not setup the Embree option string.\n");
    res = RES_MEM_ERR;
    goto error;
  }

  dev->rtc = rtcNewDevice(embree_opts);
  if(dev->rtc == NULL) {
    const enum RTCError err = rtcGetDeviceError(NULL);
    log_error(dev, "Could not create the embree device -- %s.\n",
      rtc_error_string(err));
    res = rtc_error_to_res_T(err);
    goto error;
  }

#ifndef NDEBUG
  rtcSetDeviceErrorFunction(dev->rtc, rtc_error_func, dev);
#endif

exit:
  if(out_dev) *out_dev = dev;
  return res;
error:
  if(dev) {
    S3D(device_ref_put(dev));
    dev = NULL;
  }
  goto exit;
}

res_T
s3d_device_ref_get(struct s3d_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_get(&dev->ref);
  return RES_OK;
}

res_T
s3d_device_ref_put(struct s3d_device* dev)
{
  if(!dev) return RES_BAD_ARG;
  ref_put(&dev->ref, device_release);
  return RES_OK;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
void
log_error(struct s3d_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

  va_start(vargs_list, msg);
  log_msg(dev, LOG_ERROR, msg, vargs_list);
  va_end(vargs_list);
}

void
log_warning(struct s3d_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

  va_start(vargs_list, msg);
  log_msg(dev, LOG_WARNING, msg, vargs_list);
  va_end(vargs_list);
}

