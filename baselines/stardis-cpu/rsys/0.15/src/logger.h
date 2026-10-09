/* Copyright (C) 2013-2023, 2025 Vincent Forest (vaplv@free.fr)
 *
 * The RSys library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The RSys library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the RSys library. If not, see <http://www.gnu.org/licenses/>. */

#ifndef LOGGER_H
#define LOGGER_H

#include "dynamic_array_char.h"
#include "list.h"
#include "mem_allocator.h"
#include "mutex.h"

#include <stdarg.h>

#define LOGGER_DEFAULT ((struct logger*)(int64_t)0xC0DE)

enum log_type {
  LOG_OUTPUT,
  LOG_ERROR,
  LOG_WARNING,
  LOG_TYPES_COUNT__
};

struct logger {
  /* Internal data */
  struct {
    void (*writer)(const char* msg, void* ctx);
    void* ctx;
  } streams[LOG_TYPES_COUNT__];
  struct mutex* mutex;
  struct mem_allocator* allocator;
  struct darray_char buffer;
};

static INLINE void
logger_release(struct logger* logger)
{
  ASSERT(logger);
  darray_char_release(&logger->buffer);
  if(logger->mutex) mutex_destroy(logger->mutex);
}

static INLINE res_T
logger_init
  (struct mem_allocator* allocator, /* May be NULL <=> default allocator */
   struct logger* logger)
{
  int i;
  res_T res = RES_OK;
  ASSERT(logger);

  memset(logger, 0, sizeof(struct logger));
  logger->allocator = allocator ? allocator : &mem_default_allocator;
  FOR_EACH(i, 0, LOG_TYPES_COUNT__) logger->streams[i].writer = NULL;

  darray_char_init(logger->allocator, &logger->buffer);
  res = darray_char_resize(&logger->buffer, 256);
  if(res != RES_OK) goto error;

  logger->mutex = mutex_create();
  if(!logger->mutex) {
    res = RES_MEM_ERR;
    goto error;
  }

exit:
  return res;
error:
  logger_release(logger);
  goto exit;
}

static INLINE void
logger_clear(struct logger* logger)
{
  int i;
  ASSERT(logger);
  FOR_EACH(i, 0, LOG_TYPES_COUNT__) logger->streams[i].writer = NULL;
}

static INLINE void
logger_set_stream
  (struct logger* logger,
   const enum log_type type,
   void (*writer)(const char* msg, void* ctx), /* May be NULL */
   void* ctx) /* May be NULL */
{
  ASSERT(logger && (unsigned)type < LOG_TYPES_COUNT__);
  mutex_lock(logger->mutex);
  logger->streams[type].writer = writer;
  logger->streams[type].ctx = ctx;
  mutex_unlock(logger->mutex);
}

static FINLINE char
logger_has_stream
  (struct logger* logger,
   const enum log_type type)
{
  char b;
  ASSERT(logger && (unsigned)type < LOG_TYPES_COUNT__);
  mutex_lock(logger->mutex);
  b = logger->streams[type].writer != NULL;
  mutex_unlock(logger->mutex);
  return b;
}

BEGIN_DECLS

RSYS_API res_T
logger_print
  (struct logger* logger,
   const enum log_type type,
   const char* log,
   ... )
#ifdef COMPILER_GCC
  __attribute((format(printf, 3, 4)))
#endif
;

/* The value of vargs is undefined after the call of logger_vprint */
RSYS_API res_T
logger_vprint
  (struct logger* logger,
   const enum log_type type,
   const char* log,
   va_list vargs);

END_DECLS

#endif /* LOGGER_H */
