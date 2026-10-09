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

/*
 * Generate the cstr_to_list conversion function with respect to the
 * CSTR_LIST_TYPE macro.
 */

#ifndef CSTR_LIST_TYPE
#ifndef CSTR_TO_LIST_H
#define CSTR_TO_LIST_H

#include "cstr.h"
#include "str.h"

#endif /* CSTR_TO_LIST_H */
#else /* defined(CSTR_LIST_TYPE) */

#ifndef CSTR_LIST_TYPE
  #error "Missing the CSTR_LIST_TYPE macro defining the type of the list"
#endif

#ifndef CSTR_LIST_SUFFIX
  #define CSTR_LIST_SUFFIX CSTR_LIST_TYPE
#endif

res_T
CONCAT(cstr_to_list_, CSTR_LIST_SUFFIX)
  (const char* str,
   const char delimiter,
   CSTR_LIST_TYPE dst[],
   size_t* length,
   const size_t max_length) /* Maximum size of dst */
{
  struct str buf;
  char delim[2] = {'\0', '\0'};
  size_t i;
  char* tk;
  char* tk_ctx;
  res_T res = RES_OK;

  str_init(NULL, &buf);

  if(!str) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(!dst && !length) { /* Useless invocation */
    goto exit;
  }

  /* Copy str in a temporary buffer to parse */
  res = str_set(&buf, str);
  if(res != RES_OK) goto error;

  /* Parse the string */
  delim[0] = delimiter;
#if defined(OS_WINDOWS)
  tk = strtok_s(str_get(&buf), delim, &tk_ctx);
#else
  tk = strtok_r(str_get(&buf), delim, &tk_ctx);
#endif
  for(i=0; tk;
#if defined(OS_WINDOWS)
      tk = strtok_s(NULL, delim, &tk_ctx),
#else
      tk = strtok_r(NULL, delim, &tk_ctx),
#endif
      ++i) {
    if(dst) {
      if(i >= max_length) {
        res = RES_BAD_ARG;
        goto error;
      }
      res = CONCAT(cstr_to_, CSTR_LIST_SUFFIX)(tk, dst + i);
    } else {
      CSTR_LIST_TYPE d;
      res = CONCAT(cstr_to_, CSTR_LIST_SUFFIX)(tk, &d);
    }
    if(res != RES_OK) goto error;
  }

  if(length)
    *length = i;

exit:
  str_release(&buf);
  return res;
error:
  goto exit;
}

#undef CSTR_LIST_TYPE
#undef CSTR_LIST_SUFFIX

#endif /* defined(CSTR_LIST_TYPE) */
