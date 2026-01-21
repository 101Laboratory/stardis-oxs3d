/* Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#include "scam_c.h"
#include "scam_log.h"

#include <rsys/cstr.h>
#include <rsys/logger.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE void
log_msg
  (const struct scam* cam,
   const enum log_type stream,
   const char* msg,
   va_list vargs)
{
  ASSERT(cam && msg);
  if(cam->verbose) {
    res_T res; (void)res;
    res = logger_vprint(cam->logger, stream, msg, vargs);
    ASSERT(res == RES_OK);
  }
}

static void
print_info(const char* msg, void* ctx)
{
  (void)ctx;
  fprintf(stderr, MSG_INFO_PREFIX"%s", msg);
}

static void
print_err(const char* msg, void* ctx)
{
  (void)ctx;
  fprintf(stderr, MSG_ERROR_PREFIX"%s", msg);
}

static void
print_warn(const char* msg, void* ctx)
{
  (void)ctx;
  fprintf(stderr, MSG_WARNING_PREFIX"%s", msg);
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
setup_log_default(struct scam* cam)
{
  res_T res = RES_OK;
  ASSERT(cam);

  res = logger_init(cam->allocator, &cam->logger__);
  if(res != RES_OK) {
    if(cam->verbose) {
      fprintf(stderr,
        MSG_ERROR_PREFIX
        "Could not setup the Star-Camera default logger -- %s.\n",
        res_to_cstr(res));
    }
    goto error;
  }
  logger_set_stream(&cam->logger__, LOG_OUTPUT, print_info, NULL);
  logger_set_stream(&cam->logger__, LOG_ERROR, print_err, NULL);
  logger_set_stream(&cam->logger__, LOG_WARNING, print_warn, NULL);
  cam->logger = &cam->logger__;

exit:
  return res;
error:
  goto exit;
}

void
log_info(const struct scam* cam, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(cam && msg);

  va_start(vargs_list, msg);
  log_msg(cam, LOG_OUTPUT, msg, vargs_list);
  va_end(vargs_list);
}

void
log_err(const struct scam* cam, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(cam && msg);

  va_start(vargs_list, msg);
  log_msg(cam, LOG_ERROR, msg, vargs_list);
  va_end(vargs_list);
}

void
log_warn(const struct scam* cam, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(cam && msg);

  va_start(vargs_list, msg);
  log_msg(cam, LOG_WARNING, msg, vargs_list);
  va_end(vargs_list);
}
