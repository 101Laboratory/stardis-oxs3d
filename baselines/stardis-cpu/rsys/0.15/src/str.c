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
#include "str.h"

#include <string.h>

/*******************************************************************************
 * helper function
 ******************************************************************************/
static res_T
ensure_allocated(struct str* str, const size_t len, const char keep_old)
{
  char* buf = NULL;
  const size_t alloc_granularity = 32;
  size_t mod = 0;
  size_t new_len = 0;
  size_t new_size = 0;
  ASSERT( str );

  if(len * sizeof(char) <= str->allocated)
    return RES_OK;

  mod = len % alloc_granularity;
  new_len = !mod ? len : len - mod + alloc_granularity;
  new_size = new_len * sizeof(char);
  buf = MEM_ALLOC(str->allocator, new_size);
  if(!buf)
    return RES_MEM_ERR;

  if(keep_old) {
    strncpy( buf, str->cstr, new_len - 1);
    buf[new_len - 1] = '\0';
  }

  str->allocated = new_len * sizeof(char);
  if(str->cstr != str->buffer)
    MEM_RM(str->allocator, str->cstr);

  str->cstr = buf;
  return RES_OK;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
str_set(struct str* str, const char* cstr)
{
  size_t cstr_len = 0;
  res_T res = 0;
  ASSERT(str && cstr);

  cstr_len = strlen(cstr);
  res = ensure_allocated(str, cstr_len + 1, 0);
  if(res != RES_OK) return res;
  strncpy(str->cstr, cstr, cstr_len + 1);
  return RES_OK;
}

res_T
str_insert(struct str* str, const size_t i, const char* cstr)
{
  size_t len = 0;
  size_t cstr_len = 0;
  ASSERT(str);

  len = str_len(str);

  if(i > len)
    return RES_BAD_ARG;

  cstr_len = strlen(cstr);
  ASSERT(!MEM_AREA_OVERLAP
    (str->cstr, str->allocated, cstr, (cstr_len + 1) * sizeof(char)));

  if(i == len) {
    return str_append(str, cstr);
  } else {
    const res_T res = ensure_allocated(str, cstr_len + len + 1, 1);
    if(res != RES_OK)
      return res;
    memmove
      (str->cstr + i + cstr_len,
       str->cstr + i,
       (len - i) * sizeof(char));
    memcpy(str->cstr + i, cstr, cstr_len * sizeof(char));
    str->cstr[len + cstr_len] = '\0';
  }
  return RES_OK;
}

res_T
str_insert_char(struct str* str, const size_t i, const char ch)
{
  size_t len = 0;
  ASSERT(str);

  len = str_len(str);
  if(i > len)
    return RES_BAD_ARG;

  if(i == len) {
    return str_append_char(str, ch);
  } else if(ch == '\0') {
    str->cstr[i] = ch;
  } else {
    const res_T res = ensure_allocated(str, len + 2, 1);
    if(res != RES_OK)
      return res;
    memmove
      (str->cstr + i + 1,
       str->cstr + i,
       (len - i) * sizeof(char));
    str->cstr[i] = ch;
    str->cstr[len+1] = '\0';
  }
  return RES_OK;
}

res_T
str_append(struct str* str, const char* cstr)
{
  size_t len = 0;
  size_t cstr_len = 0;
  res_T res = RES_OK;
  ASSERT(str && cstr);

  cstr_len = strlen(cstr);
  ASSERT(!MEM_AREA_OVERLAP
    (str->cstr, str->allocated, cstr, (cstr_len + 1) * sizeof(char)));

  len = str_len(str);
  res = ensure_allocated(str, cstr_len + len + 1, 1);
  if(res != RES_OK)
    return res;

  memcpy(str->cstr + len, cstr, cstr_len * sizeof(char));
  str->cstr[len + cstr_len] = '\0';
  return RES_OK;
}

res_T
str_append_char(struct str* str, const char ch)
{
  size_t len = 0;
  res_T res = RES_OK;
  ASSERT(str);

  if(ch == '\0')
    return RES_OK;

  len = str_len(str);
  res = ensure_allocated(str, len + 2, 1);
  if(res != RES_OK) return res;

  str->cstr[len+0] = ch;
  str->cstr[len+1] = '\0';
  return RES_OK;
}

res_T
str_reserve(struct str* str, const size_t capacity)
{
  return ensure_allocated(str, capacity / sizeof(char), 1);
}


res_T
str_printf(struct str* str, const char* fmt, ...)
{
  va_list ap;
  res_T res = RES_OK;
  ASSERT(str && fmt);
  va_start(ap, fmt);
  res = str_vprintf(str, fmt, ap);
  va_end(ap);
  return res;
}

res_T
str_append_printf(struct str* str, const char* fmt, ...)
{
  va_list ap;
  res_T res = RES_OK;
  ASSERT(str && fmt);
  va_start(ap, fmt);
  res = str_append_vprintf(str, fmt, ap);
  va_end(ap);
  return res;
}

res_T
str_vprintf(struct str* str, const char* fmt, va_list vargs_list)
{
  ASSERT(str && fmt);
  str_clear(str);
  return str_append_vprintf(str, fmt, vargs_list);
}

res_T
str_append_vprintf(struct str* str, const char* fmt, va_list vargs_list)
{
  va_list ap;
  size_t flen; /* Length of the formatted message */
  size_t slen; /* Length of the string */
  res_T res = RES_OK;
  ASSERT(str && fmt);

  slen = str_len(str);

  VA_COPY(ap, vargs_list);
  flen = (size_t)vsnprintf(str->cstr + slen, str->allocated - slen, fmt, ap);
  va_end(ap);

  if(slen + flen >= str->allocated) {
    res = ensure_allocated(str, slen + flen + 1/* Null char */, 1);
    if(res != RES_OK) goto error;

    VA_COPY(ap, vargs_list);
    flen = (size_t)vsnprintf(str->cstr + slen, str->allocated - slen, fmt, ap);
    va_end(ap);
    CHK(slen + flen < str->allocated);
  }

exit:
  return res;
error:
  goto exit;
}
