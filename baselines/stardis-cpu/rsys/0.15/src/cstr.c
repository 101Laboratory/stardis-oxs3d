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

#define _POSIX_C_SOURCE 200112L /* strtok_r support */

#include "cstr_to_list.h"
#include "str.h"

#define CSTR_LIST_TYPE double
#include "cstr_to_list.h"

#define CSTR_LIST_TYPE float
#include "cstr_to_list.h"

#define CSTR_LIST_TYPE unsigned
#define CSTR_LIST_SUFFIX uint
#include "cstr_to_list.h"

res_T
cstr_parse_list
  (const char* str,
   const char delimiter,
   res_T (*parse_element)(const char* elmt, void* ctx),
   void* ctx) /* User defined data sent to 'parse_element' */
{
  struct str buf;
  char delim[2] = {'\0', '\0'};
  char* tk;
  char* tk_ctx;
  res_T res = RES_OK;

  str_init(NULL, &buf);

  if(!str || !parse_element) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Copy str in a temporary buffer to parse */
  res = str_set(&buf, str);
  if(res != RES_OK) goto error;

  /* Parse the list */
  delim[0] = delimiter;
#if defined(OS_WINDOWS)
  tk = strtok_s(str_get(&buf), delim, &tk_ctx);
#else
  tk = strtok_r(str_get(&buf), delim, &tk_ctx);
#endif
  while(tk) {
    res = parse_element(tk, ctx);
    if(res != RES_OK) goto error;
#if defined(OS_WINDOWS)
    tk = strtok_s(NULL, delim, &tk_ctx);
#else
    tk = strtok_r(NULL, delim, &tk_ctx);
#endif
  }

exit:
  str_release(&buf);
  return res;
error:
  goto exit;
}

void
size_to_cstr
  (const size_t size_byte,
   const int flag, /* Combination of size_unit */
   size_t* real_cstr_len, /* May be NULL */
   char* cstr, /* May be NULL */
   const size_t sizeof_cstr)
{
  size_t available_cstr_space = sizeof_cstr;
  size_t size = size_byte;
  char* dst = cstr;
  ASSERT((!sizeof_cstr || cstr));
  ASSERT(size_byte <= INT64_MAX);

  if(real_cstr_len) *real_cstr_len = 0;
  if(sizeof_cstr > 0) cstr[0] = '\0';
  if(!flag) return;

  #define NBYTES_PER_KBYTE ((size_t)1024)
  #define NBYTES_PER_MBYTE ((size_t)1024 * NBYTES_PER_KBYTE)
  #define NBYTES_PER_GBYTE ((size_t)1024 * NBYTES_PER_MBYTE)
  #define NBYTES_PER_TBYTE ((size_t)1024 * NBYTES_PER_GBYTE)

  #define DUMP(Size, Suffix)                                                   \
    {                                                                          \
      const int len = snprintf                                                 \
        (dst, available_cstr_space, "%li %s ", (long)Size, Suffix);            \
      ASSERT(len >= 0);                                                        \
      if(real_cstr_len) {                                                      \
        *real_cstr_len += (size_t)len;                                         \
      }                                                                        \
      if((size_t)len < available_cstr_space) {                                 \
        dst += len;                                                            \
        available_cstr_space -= (size_t)len;                                   \
      } else if(dst) {                                                         \
        available_cstr_space = 0;                                              \
        dst = NULL;                                                            \
      }                                                                        \
    } (void) 0

  if(flag & SIZE_TBYTE) {
    const size_t nb_teras = size / NBYTES_PER_TBYTE;
    if(nb_teras) DUMP(nb_teras, "TB");
    size -= nb_teras * NBYTES_PER_TBYTE;
  }
  if(flag & SIZE_GBYTE) {
    const size_t nb_gigas = size / NBYTES_PER_GBYTE;
    if(nb_gigas) DUMP(nb_gigas, "GB");
    size -= nb_gigas * NBYTES_PER_GBYTE;
  }
  if(flag & SIZE_MBYTE) {
    const size_t nb_megas = size / NBYTES_PER_MBYTE;
    if(nb_megas) DUMP(nb_megas, "MB");
    size -= nb_megas * NBYTES_PER_MBYTE;
  }
  if(flag & SIZE_KBYTE) {
    const size_t nb_kilos = size / NBYTES_PER_KBYTE;
    if(nb_kilos) DUMP(nb_kilos, "KB");
    size -= nb_kilos * NBYTES_PER_KBYTE;
  }
  if(flag & SIZE_BYTE) {
    if(size) DUMP(size, "B");
  }

  /* Remove last space */
  if(real_cstr_len) *real_cstr_len -= 1;

  if(sizeof_cstr > 0) {
    size_t cstr_len = strlen(cstr);

    if(!cstr_len && flag) {
      ASSERT(size_byte == 0);
      if(flag & SIZE_BYTE) { DUMP(0, "B");
      } else if(flag & SIZE_KBYTE) { DUMP(0, "KB");
      } else if(flag & SIZE_MBYTE) { DUMP(0, "MB");
      } else if(flag & SIZE_GBYTE) { DUMP(0, "GB");
      } else if(flag & SIZE_TBYTE) { DUMP(0, "TB");
      }
      cstr_len = strlen(cstr);
    }
    /* Remove last space */
    if(cstr[cstr_len-1] == ' ') {
      cstr[cstr_len-1] = '\0';
    }
  }
  #undef NBYTES_PER_KBYTE
  #undef NBYTES_PER_MBYTE
  #undef NBYTES_PER_GBYTE
  #undef NBYTES_PER_TBYTE
  #undef DUMP
}
