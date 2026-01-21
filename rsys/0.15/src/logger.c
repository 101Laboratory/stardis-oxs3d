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

#define _POSIX_C_SOURCE 200112L /* vsnprintf support */
#include "logger.h"

res_T
logger_print
  (struct logger* logger, const enum log_type type, const char* log, ...)
{
  va_list vargs_list;
  res_T res;
  ASSERT(logger && log && (unsigned)type < LOG_TYPES_COUNT__);
  va_start(vargs_list, log);
  res = logger_vprint(logger, type, log, vargs_list);
  va_end(vargs_list);
  return res;
}

res_T
logger_vprint
  (struct logger* logger,
   const enum log_type type,
   const char* log,
   va_list vargs_list)
{
  va_list vargs_list_cp;
  int sz;
  ASSERT(logger && log && (unsigned)type < LOG_TYPES_COUNT__);

  if(logger == LOGGER_DEFAULT) {
    FILE* stream = type == LOG_OUTPUT ? stdout : stderr;
    vfprintf(stream, log, vargs_list);
    fflush(stream);
    return RES_OK;
  }

  if(!logger_has_stream(logger, type))
    return RES_OK;

  mutex_lock(logger->mutex);

  VA_COPY(vargs_list_cp, vargs_list);
  sz = vsnprintf
    (darray_char_data_get(&logger->buffer),
     darray_char_size_get(&logger->buffer),
     log, vargs_list_cp);
  va_end(vargs_list_cp);
  ASSERT(sz > 0);

  /* If there is not sufficient space in the logger buffer, resize it */
  if((size_t)sz >= darray_char_size_get(&logger->buffer)) {
    res_T res = darray_char_resize(&logger->buffer, (size_t)sz+1/*+1<=>'\0'*/);
    if(res != RES_OK) {
      mutex_unlock(logger->mutex);
      return res;
    }

    VA_COPY(vargs_list_cp, vargs_list);
    sz = vsnprintf
      (darray_char_data_get(&logger->buffer),
       darray_char_size_get(&logger->buffer),
       log, vargs_list);
    va_end(vargs_list_cp);
    ASSERT((size_t)sz < darray_char_size_get(&logger->buffer));
  }
  mutex_unlock(logger->mutex);
  /* Print the formatted string */
  logger->streams[type].writer
    (darray_char_cdata_get(&logger->buffer), logger->streams[type].ctx);
  return RES_OK;
}
