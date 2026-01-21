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

#ifndef STR_H
#define STR_H

#include "hash.h"
#include "mem_allocator.h"
#include "rsys.h"

#include <stdarg.h>
#include <string.h>

struct str {
  /* Internal data. Should not be publicly accessed */
  struct mem_allocator* allocator;
  size_t allocated; /* <=> string capacity */
  char* cstr;
  char buffer[16]; /* static buffer. Avoid allocation on small string */
};

static INLINE void
str_init(struct mem_allocator* allocator, struct str* str)
{
  ASSERT(str);
  str->allocator = allocator ? allocator : &mem_default_allocator;
  str->allocated = sizeof(str->buffer);
  str->cstr = str->buffer;
  str->buffer[0] = '\0';
}

static INLINE void
str_release(struct str* str)
{
  ASSERT(str);
  if(str->cstr != str->buffer)
    MEM_RM(str->allocator, str->cstr);
  str->cstr = NULL;
}

static INLINE size_t
str_len(const struct str* str)
{
  ASSERT(str);
  return strlen(str->cstr);
}

static INLINE char
str_is_empty(const struct str* str)
{
  ASSERT(str);
  return str_len(str) == 0;
}

static INLINE void
str_clear(struct str* str)
{
  ASSERT(str);
  str->cstr[0] = '\0';
}

static INLINE char*
str_get(struct str* str)
{
  ASSERT(str);
  return str->cstr;
}

static INLINE const char*
str_cget(const struct str* str)
{
  ASSERT(str);
  return str->cstr;
}

static INLINE int
str_cmp(const struct str* str0, const struct str* str1)
{
  ASSERT(str0 && str1);
  return strcmp(str_cget(str0), str_cget(str1));
}

static INLINE char
str_eq(const struct str* str0, const struct str* str1)
{
  return str_cmp(str0, str1) == 0;
}

BEGIN_DECLS

RSYS_API res_T str_set(struct str* str, const char* cstr);
RSYS_API res_T str_insert(struct str* str, const size_t i, const char* cstr);
RSYS_API res_T str_insert_char(struct str* str, const size_t i, const char ch);
RSYS_API res_T str_append(struct str* str, const char* cstr);
RSYS_API res_T str_append_char(struct str* str, const char ch);
RSYS_API res_T str_reserve(struct str* str, const size_t capacity);

RSYS_API res_T
str_printf
  (struct str* str,
   const char* fmt,
   ...)
#ifdef COMPILER_GCC
  __attribute__((format(printf, 2, 3)))
#endif
;

RSYS_API res_T
str_append_printf
  (struct str* str,
   const char* fmt,
   ...)
#ifdef COMPILER_GCC
  __attribute__((format(printf, 2, 3)))
#endif
;

RSYS_API res_T
str_vprintf
  (struct str* str,
   const char* fmt,
   va_list vargs_list);

RSYS_API res_T
str_append_vprintf
  (struct str* str,
   const char* fmt,
   va_list vargs_list);

END_DECLS

static INLINE res_T
str_copy(struct str* dst, const struct str* src)
{
  ASSERT(dst && src);
  if(dst == src)
    return RES_OK;
  return str_set(dst, str_cget(src));
}

static INLINE res_T
str_copy_and_clear(struct str* dst, struct str* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  if(dst == src) {
    str_clear(dst);
    return RES_OK;
  }

  if(src->cstr != src->buffer && src->allocator == dst->allocator) {
    /* Give the ownership of src->cstr to dst */
    if(dst->cstr != dst->buffer )
      MEM_RM(dst->allocator, dst->cstr);
    dst->cstr = src->cstr;
    dst->allocated = src->allocated;
    /* Reset the src to its initial state */
    str_init(src->allocator, src);
  } else {
    res = str_copy(dst, src);
    if(res == RES_OK)
      str_clear(src);
  }
  return res;
}

static INLINE res_T
str_copy_and_release(struct str* dst, struct str* src)
{
  res_T res = RES_OK;
  ASSERT( dst && src );
  if(dst == src) {
    str_release(dst);
  } else {
    res = str_copy_and_clear(dst, src);
    if(res == RES_OK)
      str_release(src);
  }
  return res;
}

static INLINE size_t
str_hash(const struct str* str)
{
  const char* c_str;
  ASSERT(str);
  c_str = str_cget(str);
#ifdef ARCH_32BITS
  return (size_t)hash_fnv32(c_str, str_len(str));
#elif defined ARCH_64BITS
  return (size_t)hash_fnv64(c_str, str_len(str));
#else
  #error "Unexpected architecture"
#endif
}

#endif /* STR_H */
