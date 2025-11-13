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

#define _POSIX_C_SOURCE 200112L /* snprintf support */

#include "sdis_device_c.h"
#include "sdis_log.h"

#include <rsys/logger.h>

#include <stdarg.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
log_msg
  (const struct sdis_device* dev,
   const enum log_type stream,
   const char* msg,
   va_list vargs)
{
  ASSERT(dev && msg);
  if(dev->verbose) {
    CHK(logger_vprint(dev->logger, stream, msg, vargs) == RES_OK);
  }
}

static void
print_info(const char* msg, void* ctx)
{
  struct sdis_device* dev = ctx;

  if(dev->no_escape_sequence) {
    fprintf(stderr, MSG_INFO_PREFIX_PLAIN_TEXT"%s", msg);
  } else {
    fprintf(stderr, MSG_INFO_PREFIX"%s", msg);
  }
}

static void
print_err(const char* msg, void* ctx)
{
  struct sdis_device* dev = ctx;
  if(dev->no_escape_sequence) {
    fprintf(stderr, MSG_ERROR_PREFIX_PLAIN_TEXT"%s", msg);
  } else {
    fprintf(stderr, MSG_ERROR_PREFIX"%s", msg);
  }
}

static void
print_warn(const char* msg, void* ctx)
{
  struct sdis_device* dev = ctx;
  if(dev->no_escape_sequence) {
    fprintf(stderr, MSG_WARNING_PREFIX_PLAIN_TEXT"%s", msg);
  } else {
    fprintf(stderr, MSG_WARNING_PREFIX"%s", msg);
  }
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
setup_log_default(struct sdis_device* dev)
{
  res_T res = RES_OK;
  ASSERT(dev);

  res = logger_init(dev->allocator, &dev->logger__);
  if(res != RES_OK) {
    if(dev->verbose) print_err("Could not setup the logger.\n", NULL);
    goto error;
  }
  logger_set_stream(&dev->logger__, LOG_OUTPUT, print_info, dev);
  logger_set_stream(&dev->logger__, LOG_ERROR, print_err, dev);
  logger_set_stream(&dev->logger__, LOG_WARNING, print_warn, dev);
  dev->logger = &dev->logger__;

exit:
  return res;
error:
  goto exit;
}

void
log_info(const struct sdis_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

#ifdef SDIS_ENABLE_MPI
  /* Log standard messages only on master process */
  if(dev->mpi_rank == 0)
#endif
  {
    va_start(vargs_list, msg);
    log_msg(dev, LOG_OUTPUT, msg, vargs_list);
    va_end(vargs_list);
  }
}

void
log_err(const struct sdis_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

#ifdef SDIS_ENABLE_MPI
  /* Log error messages only on master process */
  if(dev->mpi_rank == 0)
#endif
  {
    va_start(vargs_list, msg);
    log_msg(dev, LOG_ERROR, msg, vargs_list);
    va_end(vargs_list);
  }
}

void
log_warn(const struct sdis_device* dev, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(dev && msg);

#ifdef SDIS_ENABLE_MPI
  /* Log warnings only on master process */
  if(dev->mpi_rank == 0)
#endif
  {
    va_start(vargs_list, msg);
    log_msg(dev, LOG_WARNING, msg, vargs_list);
    va_end(vargs_list);
  }
}

