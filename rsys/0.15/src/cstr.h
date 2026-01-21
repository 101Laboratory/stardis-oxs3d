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

#ifndef CSTR_H
#define CSTR_H

#include "rsys.h"

#include "rsys_math.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>

enum size_unit {
  SIZE_BYTE = BIT(0),
  SIZE_KBYTE = BIT(1),
  SIZE_MBYTE = BIT(2),
  SIZE_GBYTE = BIT(3),
  SIZE_TBYTE = BIT(4),
  SIZE_ALL = -1
};

static INLINE res_T
cstr_to_double(const char* str, double* dst)
{
  char* end;
  ASSERT(dst);
  if(!str) return RES_BAD_ARG;
  *dst = strtod(str, &end);
  if(end == str)
    return RES_BAD_ARG;
  for(;*end != '\0'; ++end) {
    if(*end != ' ' && *end != '\t')
      return RES_BAD_ARG;
  }
  return RES_OK;
}

static INLINE res_T
cstr_to_float(const char* str, float* dst)
{
  double dbl;
  double tmp;
  res_T res;
  ASSERT(dst);
  res = cstr_to_double(str, &dbl);
  if(res != RES_OK)  return res;
  tmp = fabs(dbl);
  if(tmp != INF && tmp > 0.0 && (tmp < FLT_MIN || tmp > FLT_MAX))
    return RES_BAD_ARG;
  *dst = (float)dbl;
  return RES_OK;
}

static INLINE res_T
cstr_to_long(const char* str, long* dst)
{
  char* end;
  ASSERT(dst);
  if(!str) return RES_BAD_ARG;
  errno = 0;
  *dst = strtol(str, &end, 10/* base */);
  if(end == str || errno == ERANGE)
    return RES_BAD_ARG;
  for(;*end != '\0'; ++end) {
    if(*end != ' ' && *end != '\t')
      return RES_BAD_ARG;
  }
  return RES_OK;
}

static INLINE res_T
cstr_to_int(const char* str, int* dst)
{
  long l;
  res_T res;
  ASSERT(dst);
  res = cstr_to_long(str, &l);
  if(res != RES_OK)
    return res;
  if(l > INT_MAX || l < INT_MIN)
    return RES_BAD_ARG;
  *dst = (int)l;
  return RES_OK;
}

static INLINE res_T
cstr_to_ulong(const char* str, unsigned long* dst)
{
  char* end;
  ASSERT(dst);
  if (!str) return RES_BAD_ARG;
  errno = 0;
  *dst = strtoul(str, &end, 10/* base */);
  if(end == str || errno == ERANGE)
    return RES_BAD_ARG;
  ASSERT(errno == 0);
  for(; *end != '\0'; ++end) {
    if(*end != ' ' && *end != '\t')
      return RES_BAD_ARG;
  }
  return RES_OK;
}

static INLINE res_T
cstr_to_uint(const char* str, unsigned* dst)
{
  res_T res;
  ASSERT(dst);
#if UINT_MAX == ULONG_MAX
  {
    unsigned long l;
    res = cstr_to_ulong(str, &l);
    if(res != RES_OK) return res;
    *dst = (unsigned)l;
  }
#else /* UINT_MAX < ULONG_MAX */
  {
    long l;
    res = cstr_to_long(str, &l);
    if(res != RES_OK) return res;
    if(l > UINT_MAX) return RES_BAD_ARG;
    *dst = (unsigned)l;
  }
#endif
  return RES_OK;
}

static INLINE const char*
res_to_cstr(const res_T res)
{
  const char* cstr = NULL;
  switch(res) {
    case RES_OK: cstr = "Success"; break;
    case RES_BAD_ARG: cstr = "Invalid argument"; break;
    case RES_MEM_ERR: cstr = "Could not allocate memory"; break;
    case RES_IO_ERR: cstr = "Input/Ouput error"; break;
    case RES_UNKNOWN_ERR: cstr = "Unknown error"; break;
    case RES_BAD_OP: cstr = "Invalid operation"; break;
    case RES_EOF: cstr = "Reached end of file"; break;
    default: FATAL("Ureachable code.\n"); break;
  }
  return cstr;
}

BEGIN_DECLS

/* Parse a string representing a list whose its elements are separated by the
 * 'delimeter' char. The functor 'parse_element' is invoked on each element of
 * the list. If it notifies an error, i.e. if the parsing of an element failed,
 * the overall parsing is instantly stopped and the error is returned to the
 * caller */
RSYS_API res_T
cstr_parse_list
  (const char* str,
   const char delimiter,
   res_T (*parse_element)(const char* elmt, void* ctx),
   void* ctx); /* User defined data sent to 'parse_element' */

/* Convert a string "A:B:C:D:E:F" in a list of { A, B, C, D, E, F }. ':' can be
 * any user defined character */
RSYS_API res_T
cstr_to_list_double
  (const char* str,
   const char delimiter,
   double dst[], /* May be NULL */
   size_t* length, /* May be NULL. Length of the filled list */
   const size_t max_length); /* Maximum size of dst */

RSYS_API res_T
cstr_to_list_float
  (const char* str,
   const char delimiter,
   float dst[], /* May be NULL */
   size_t* length, /* May be NULL. Length of the filled list */
   const size_t max_length); /* Maximum size of dst */

RSYS_API res_T
cstr_to_list_uint
  (const char* str,
   const char delimiter,
   unsigned dst[],
   size_t* length,
   const size_t max_length);

RSYS_API void
size_to_cstr
  (const size_t size,
   const int flag, /* Combination of size_unit */
   size_t* real_cstr_len, /* May be NULL (does not handle null char) */
   char* cstr, /* May be NULL */
   const size_t sizeof_cstr); /* Include null char */

END_DECLS

#endif /* CSTR_H */
