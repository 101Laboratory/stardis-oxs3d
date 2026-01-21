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

#include "dynamic_array_char.h"
#include "mem_allocator.h"
#include "ref_count.h"
#include "str.h"
#include "text_reader.h"

/* #chars to add to the line buffer when there is no sufficient space to store
 * the text */
#define CHUNK 32

struct txtrdr {
  FILE* stream; /* Stream of the text to read */
  struct str name; /* Stream name */
  size_t nlines; /* # read lines */
  struct darray_char line; /* Buffer storing the read line */

  /* String of chars from which the remaining line chars are skipped */
  char reject[4];

  /* Boolean defining if the stream is internally managed or not, i.e. if it
   * has to be closed on text_reader release or not */
  int manage_stream;

  struct mem_allocator* allocator;
  ref_T ref;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
release_txtrdr(ref_T* ref)
{
  struct txtrdr* txtrdr = NULL;
  ASSERT(ref);
  txtrdr = CONTAINER_OF(ref, struct txtrdr, ref);
  str_release(&txtrdr->name);
  darray_char_release(&txtrdr->line);
  if(txtrdr->stream && txtrdr->manage_stream) fclose(txtrdr->stream);
  MEM_RM(txtrdr->allocator, txtrdr);
}

/*******************************************************************************
 * Text reader API
 ******************************************************************************/
res_T
txtrdr_stream
  (struct mem_allocator* mem_allocator,
   FILE* stream,
   const char* name,
   const char comment,
   struct txtrdr** out_txtrdr)
{
  struct mem_allocator* allocator = NULL;
  struct txtrdr* txtrdr = NULL;
  res_T res = RES_OK;
  ASSERT(stream && out_txtrdr);

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  txtrdr = MEM_CALLOC(allocator, 1, sizeof(*txtrdr));
  if(!txtrdr) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&txtrdr->ref);
  str_init(allocator, &txtrdr->name);
  darray_char_init(allocator, &txtrdr->line);
  txtrdr->allocator = allocator;
  txtrdr->stream = stream;
  txtrdr->nlines = 0;
  txtrdr->reject[0] = '\n';
  txtrdr->reject[1] = '\r';
  txtrdr->reject[2] = comment;
  txtrdr->reject[3] = '\0'; /* Finalize the string */

  res = str_set(&txtrdr->name, name ? name : "<null>");
  if(res != RES_OK) goto error;

  res = darray_char_resize(&txtrdr->line, CHUNK);
  if(res != RES_OK) goto error;

exit:
  *out_txtrdr = txtrdr;
  return res;
error:
  if(txtrdr) {
    txtrdr_ref_put(txtrdr);
    txtrdr = NULL;
  }
  goto exit;
}

res_T
txtrdr_file
  (struct mem_allocator* mem_allocator,
   const char* filename,
   const char comment,
   struct txtrdr** out_txtrdr)
{
  FILE* fp = NULL;
  res_T res = RES_OK;
  ASSERT(filename);

  fp = fopen(filename, "r");
  if(!fp) { res = RES_IO_ERR; goto error; }

  res = txtrdr_stream(mem_allocator, fp, filename, comment, out_txtrdr);
  if(res != RES_OK) goto error;

  (*out_txtrdr)->manage_stream = 1;

exit:
  return res;
error:
  if(fp) fclose(fp);
  goto exit;
}

void
txtrdr_ref_get(struct txtrdr* txtrdr)
{
  ASSERT(txtrdr);
  ref_get(&txtrdr->ref);
}

void
txtrdr_ref_put(struct txtrdr* txtrdr)
{
  ASSERT(txtrdr);
  ref_put(&txtrdr->ref, release_txtrdr);
}

res_T
txtrdr_read_line(struct txtrdr* txtrdr)
{
  char* str = NULL;
  res_T res = RES_OK;

  if(!txtrdr) {
    res = RES_BAD_ARG;
    goto error;
  }

  do {
    /* Read the line */
    str = fgets
      (darray_char_data_get(&txtrdr->line),
       (int)darray_char_size_get(&txtrdr->line),
       txtrdr->stream);
    if(!str) {
      if(ferror(txtrdr->stream)) {
        res = RES_IO_ERR;
        goto error;
      } else {
        darray_char_clear(&txtrdr->line);
        break;
      }
    }

    /* Ensure tht the whole line is read */
    while(!strrchr(DARRAY_BUF(&txtrdr->line), '\n') && !feof(txtrdr->stream)) {
      const size_t sz = darray_char_size_get(&txtrdr->line);
      char* more = NULL;

      /* Resize the line buffer */
      res = darray_char_resize(&txtrdr->line, sz+CHUNK);
      if(res != RES_OK) goto error;

      /* Read the remaing chars */
      more = darray_char_data_get(&txtrdr->line) + sz-1/*null char*/;
      str = fgets(more, CHUNK+1/*previous null char*/, txtrdr->stream);
      if(!str && ferror(txtrdr->stream)) {
        res = RES_IO_ERR;
        goto error;
      }
    }

    /* Remove new line & comments */
    str = darray_char_data_get(&txtrdr->line);
    str[strcspn(str, txtrdr->reject)] = '\0';

    ++txtrdr->nlines; /* Increment the number of read lines */

  } while(strspn(str, " \t") == strlen(str));/*Keep going if the line is Empty*/

exit:
  return res;
error:
  goto exit;
}

char*
txtrdr_get_line(struct txtrdr* txtrdr)
{
  ASSERT(txtrdr);
  return darray_char_size_get(&txtrdr->line) == 0
    ? NULL : darray_char_data_get(&txtrdr->line);
}

const char*
txtrdr_get_cline(const struct txtrdr* txtrdr)
{
  ASSERT(txtrdr);
  return darray_char_size_get(&txtrdr->line) == 0
    ? NULL : darray_char_cdata_get(&txtrdr->line);
}

size_t
txtrdr_get_line_num(const struct txtrdr* txtrdr)
{
  ASSERT(txtrdr);
  return txtrdr->nlines;
}

void
txtrdr_set_line_num(struct txtrdr* txtrdr, const size_t nlines)
{
  ASSERT(txtrdr);
  txtrdr->nlines = nlines;
}

const char*
txtrdr_get_name(const struct txtrdr* txtrdr)
{
  ASSERT(txtrdr);
  return str_cget(&txtrdr->name);
}

FILE*
txtrdr_get_stream(const struct txtrdr* txtrdr)
{
  ASSERT(txtrdr);
  return txtrdr->stream;
}
