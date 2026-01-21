/* Copyright (C) 2015, 2016, 2019, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#define _POSIX_C_SOURCE 200112L /* strtok_r support */

#include "sstl_c.h"

#include <rsys/cstr.h>
#include <rsys/stretchy_array.h>
#include <rsys/text_reader.h>

#include <string.h>

#ifdef _MSC_VER
#define strtok_r strtok_s
#endif

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
parse_float3
  (struct sstl* sstl,
   const struct txtrdr* txtrdr,
   char* str,
   char** ctx,
   float vec[3])
{
  char* x = NULL;
  char* y = NULL;
  char* z = NULL;
  res_T res = RES_OK;
  ASSERT(sstl && txtrdr && vec);

  if(!(x = strtok_r(str,  " \t", ctx))
  || !(y = strtok_r(NULL, " \t", ctx))
  || !(z = strtok_r(NULL, " \t", ctx))) {
    res = RES_BAD_ARG;
    goto error;
  }

  if((res = cstr_to_float(x, vec+0)) != RES_OK) goto error;
  if((res = cstr_to_float(y, vec+1)) != RES_OK) goto error;
  if((res = cstr_to_float(z, vec+2)) != RES_OK) goto error;

exit:
  return res;
error:
  ERROR(sstl, "%s:%lu: invalid vector component\n",
    txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr));
  goto exit;
}

static res_T
parse_word
  (struct sstl* sstl,
   const struct txtrdr* txtrdr,
   const char* word,
   char* str,
   char** ctx)
{
  char* tk = NULL;
  ASSERT(sstl && txtrdr && ctx);

  tk = strtok_r(str, " \t", ctx);
  if(!tk || strcmp(tk, word) != 0) {
    ERROR(sstl, "%s:%lu: expect the \"%s\" keyword\n",
      txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr),
      word);
    return RES_BAD_ARG;
  }

  return RES_OK;
}

static INLINE res_T
parse_nothing
  (struct sstl* sstl,
   const struct txtrdr* txtrdr,
   char* str,
   char** ctx)
{
  char* tk = NULL;

  ASSERT(txtrdr && ctx);
  (void)sstl; /* Avoid "unused variable" warning */

  tk = strtok_r(str, " \t", ctx);
  if(tk != NULL) {
    WARN(sstl, "%s:%lu: unexpected text \"%s\"\n",
      txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr), tk);
  }
  return RES_OK;
}

static res_T
parse_vec3
  (struct sstl* sstl,
   struct txtrdr* txtrdr,
   const char* name,
   char* str,
   char** ctx,
   float vec[3])
{
  res_T res = RES_BAD_ARG;
  ASSERT(sstl && txtrdr && name && ctx);

  if((res = parse_word(sstl, txtrdr, name, str, ctx)) != RES_OK) goto error;
  if((res = parse_float3(sstl, txtrdr, NULL, ctx, vec)) != RES_OK) goto error;
  if((res = parse_nothing(sstl, txtrdr, NULL, ctx)) != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

static res_T
parse_facet(struct sstl* sstl, struct txtrdr* txtrdr, char* normal)
{
  float v[3][3] = {0};
  float* N = NULL;
  char* line = NULL;
  char* ctx = NULL;
  int i = 0;
  res_T res = RES_OK;
  ASSERT(sstl && txtrdr && normal);

  N = sa_add(sstl->normals, 3);
  if((res = parse_vec3(sstl, txtrdr, "normal", normal, &ctx, N)) != RES_OK) goto error;

  #define READ_LINE { \
    if((res = txtrdr_read_line(txtrdr)) != RES_OK) { \
      ERROR(sstl, "%s: error reading line -- %s\n", \
        txtrdr_get_name(txtrdr), res_to_cstr(res)); \
      goto error; \
    }  \
    if(!(line = txtrdr_get_line(txtrdr))) { \
      ERROR(sstl, "%s: unexpected end of file\n", txtrdr_get_name(txtrdr)); \
      res = RES_BAD_ARG; \
      goto error; \
    } \
  } (void)0

  READ_LINE;
  if((res = parse_word(sstl, txtrdr, "outer", line, &ctx)) != RES_OK) goto error;
  if((res = parse_word(sstl, txtrdr, "loop",  NULL, &ctx)) != RES_OK) goto error;
  if((res = parse_nothing(sstl, txtrdr, NULL, &ctx)) != RES_OK) goto error;

  FOR_EACH(i, 0, 3) {
    READ_LINE;
    res = parse_vec3(sstl, txtrdr, "vertex", line, &ctx, v[i]);
    if(res != RES_OK) goto error;

    res = register_vertex(sstl, v[i]);
    if(res != RES_OK) {
      ERROR(sstl, "%s:%lu: vertex registration error -- %s\n",
        txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr),
        res_to_cstr(res));
      goto error;
    }
  }

  READ_LINE;
  if((res = parse_word(sstl, txtrdr, "endloop", line, &ctx)) != RES_OK) goto error;
  if((res = parse_nothing(sstl, txtrdr, NULL, &ctx)) != RES_OK) goto error;

  READ_LINE;
  if((res = parse_word(sstl, txtrdr, "endfacet", line, &ctx)) != RES_OK) goto error;
  if((res = parse_nothing(sstl, txtrdr, NULL, &ctx)) != RES_OK) goto error;

  #undef READ_LINE

  /* If necessary, automatically calculate the surface normal. */
  if(!f3_is_normalized(N)) calculate_normal(N, v[0], v[1], v[2]);

exit:
  return res;
error:
  goto exit;
}

static res_T
parse_solid(struct sstl* sstl, struct txtrdr* txtrdr)
{
  char* line = NULL;
  char* tk = NULL;
  char* tk_ctx = NULL;
  res_T res = RES_OK;
  ASSERT(sstl && txtrdr);

  line = txtrdr_get_line(txtrdr);
  ASSERT(line != NULL);

  tk = strtok_r(line, " \t", &tk_ctx);
  ASSERT(tk);
  if(strcmp(tk, "solid")) {
    ERROR(sstl, "%s:%lu: the \"solid [name]\" directive is missing\n",
      txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr));
    res = RES_BAD_ARG;
    goto error;
  }

  tk = strtok_r(NULL, "", &tk_ctx);
  if(tk != NULL && (res = str_set(&sstl->name, tk)) != RES_OK) {
    ERROR(sstl, "%s:%lu: error duplicating solid name -- %s\n",
      txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr),
      res_to_cstr(res));
    goto error;
  }

  for(;;) {
    if((res = txtrdr_read_line(txtrdr)) != RES_OK) {
      ERROR(sstl, "%s: error reading line -- %s\n",
        str_cget(&sstl->name), res_to_cstr(res));
      goto error;
    }

    if((line = txtrdr_get_line(txtrdr)) == NULL) {
      ERROR(sstl, "%s: the \"endsolid [name]\" directive is missing\n",
        txtrdr_get_name(txtrdr));
      res = RES_BAD_ARG;
      goto error;
    }

    tk = strtok_r(line, " \t", &tk_ctx);

    if(!strcmp(tk, "facet")) {
      res = parse_facet(sstl, txtrdr, strtok_r(NULL, "", &tk_ctx));
      if(res != RES_OK) goto error;

    } else if(!strcmp(tk, "endsolid")) {
      break; /* Stop on "endsolid" directive */

    } else {
      ERROR(sstl, "%s:%lu: invalid directive \"%s\"\n",
        txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr), tk);
      res = RES_BAD_ARG;
      goto error;
    }
  }

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
load_stream_ascii
  (struct sstl* sstl,
   FILE* stream,
   const char* stream_name)
{
  struct txtrdr* txtrdr = NULL;
  res_T res = RES_OK;

  ASSERT(sstl && stream && stream_name);

  clear(sstl);

  res = txtrdr_stream(sstl->allocator, stream, stream_name, '#', &txtrdr);
  if(res != RES_OK) {
    ERROR(sstl, "%s: error creating text reader -- %s\n",
      stream_name, res_to_cstr(res));
    goto error;
  }

  if((res = txtrdr_read_line(txtrdr)) != RES_OK) {
    ERROR(sstl, "%s: error reading line -- %s\n", stream_name, res_to_cstr(res));
    goto error;
  }

  if(txtrdr_get_cline(txtrdr) != NULL) {  /* File is not empty */
    if((res = parse_solid(sstl, txtrdr)) != RES_OK) goto error;
  }

  sstl->type = SSTL_ASCII;

exit:
  htable_vertex_purge(&sstl->vertex2id); /* Purge the helper structure */
  if(txtrdr) txtrdr_ref_put(txtrdr);
  return res;
error:
  clear(sstl);
  goto exit;
}
#undef strtok_r
