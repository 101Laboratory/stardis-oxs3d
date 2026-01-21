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

#include "sstl.h"
#include "sstl_c.h"

#include <rsys/cstr.h>
#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>
#include <rsys/str.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define WRITING_ERROR(Writer) \
 ERROR((Writer), "%s: writing error -- %s\n", \
  str_cget(&(Writer)->filename), strerror(errno));

struct sstl_writer {
  struct str filename;
  FILE* fp;

  int is_fp_intern; /* Define whether fp is internally opened or not */
  int is_init; /* Define whether the writer should be finalised or not */

  long ntris; /* Number of triangles. <0 <=> not defined a priori */
  long ntris_written; /* Number of effectively written triangles */
  long ntris_offset; /* >= 0 <=> file offset in binary StL for #triangles */

  enum sstl_type type;

  struct mem_allocator* allocator;
  struct logger* logger;
  int verbose; /* Verbosity level */
  ref_T ref;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
check_sstl_writer_create_args(const struct sstl_writer_create_args* args)
{
  if(!args) return RES_BAD_ARG;

  if(args->type != SSTL_ASCII && args->type != SSTL_BINARY)
    return RES_BAD_ARG;

  if(!args->filename)
    return RES_BAD_ARG;

  if(args->triangles_count > 0 && args->triangles_count > UINT32_MAX)
    return RES_BAD_ARG;

  return RES_OK;
}

static res_T
setup_filename
  (struct sstl_writer* writer,
   const struct sstl_writer_create_args* args)
{
  res_T res = RES_OK;
  ASSERT(writer && args);

  if((res = str_set(&writer->filename, args->filename)) != RES_OK) {
    ERROR(writer, "Error copying filen name '%s' -- %s\n",
      args->filename, res_to_cstr(res));
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
setup_stream
  (struct sstl_writer* writer,
   const struct sstl_writer_create_args* args)
{
  res_T res = RES_OK;
  ASSERT(writer && args);

  if(args->stream) {
    writer->fp = args->stream;

  } else if((writer->fp = fopen(args->filename, "w")) == NULL) {
    ERROR(writer, "Error opening file %s -- %s\n",
      args->filename, strerror(errno));
    res = RES_IO_ERR;
    goto error;
  }

  writer->is_fp_intern = args->stream != writer->fp;

  /* if the data written is binary and the definition of the number of triangles
   * is left to the author, check that the file is seekable. This is because the
   * number of triangles must be written at the beginning of the file, whereas
   * this number will be known after all the triangles have been written. One
   * therefore need to be able to position the file at the correct offset once
   * the total number of triangles is known */
  if(args->type == SSTL_BINARY
  && args->triangles_count < 0
  && !file_is_seekable(writer->fp)) {
    ERROR(writer,
      "%s: invalid file. A binary StL can only be written to a pipe, FIFO or "
      "socket if the total number of triangles to be written is known in "
      "advance.\n", args->filename);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  if(writer->fp && writer->is_fp_intern) {
    CHK(fclose(writer->fp) == 0);
    writer->fp = NULL;
  }
  goto exit;
}

static res_T
write_header_ascii
  (struct sstl_writer* writer,
   const struct sstl_writer_create_args* args)
{
  res_T res = RES_OK;
  int n = 0;
  ASSERT(writer && args);

  if(args->solid_name) {
    n = fprintf(writer->fp, "solid %s\n", args->solid_name);
  } else {
    n = fprintf(writer->fp, "solid\n");
  }

  if(n < 0) {
    WRITING_ERROR(writer);
    res = RES_IO_ERR;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
write_ntriangles(const struct sstl_writer* writer, const uint32_t ntris)
{
  uint8_t bytes[4] = {0};
  res_T res = RES_OK;
  ASSERT(writer);

  bytes[0] = (uint8_t)((ntris >> 0) & 0xFF);
  bytes[1] = (uint8_t)((ntris >> 8) & 0xFF);
  bytes[2] = (uint8_t)((ntris >> 16) & 0xFF);
  bytes[3] = (uint8_t)((ntris >> 24) & 0xFF);

  if(fwrite(bytes, 1, 4, writer->fp) != 4) {
    res = RES_IO_ERR;
    goto error;
  }

exit:
  return res;
error:
  WRITING_ERROR(writer);
  goto exit;
}

static res_T
write_header_binary
  (struct sstl_writer* writer,
   const struct sstl_writer_create_args* args)
{
  uint8_t bytes[80] = {0};
  uint32_t ntris = 0;
  res_T res = RES_OK;
  ASSERT(writer && args);

  if(fwrite(bytes, 1, 80, writer->fp) != 80) {
    res = RES_IO_ERR;
    goto error;
  }

  writer->ntris = args->triangles_count;

  if(args->triangles_count < 0) {
    ASSERT(file_is_seekable(writer->fp));
    writer->ntris_offset = ftell(writer->fp);
  } else {
    ntris = (uint32_t)args->triangles_count;
  }

  res = write_ntriangles(writer, ntris);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  WRITING_ERROR(writer);
  goto exit;
}

static res_T
write_header
  (struct sstl_writer* writer,
   const struct sstl_writer_create_args* args)
{
  res_T res = RES_OK;
  ASSERT(writer);

  switch(writer->type) {
    case SSTL_ASCII: res = write_header_ascii(writer, args); break;
    case SSTL_BINARY: res = write_header_binary(writer, args); break;
    default: FATAL("Unreachable code\n"); break;
  }
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

static res_T
write_facet_ascii(struct sstl_writer* writer, const struct sstl_facet* facet)
{
  float N[3] = {0,0,0};
  res_T res = RES_OK;
  ASSERT(writer && facet);

  /* If necessary, automatically calculate the surface normal. */
  if(!f3_is_normalized(f3_set(N, facet->normal))) {
    calculate_normal
      (N, facet->vertices[0], facet->vertices[1], facet->vertices[2]);
  }

  #define PRINTF(...) { \
    if(fprintf(writer->fp, __VA_ARGS__) < 0) { \
      WRITING_ERROR(writer); \
      res = RES_IO_ERR; \
      goto error; \
    } \
  } (void)0

  PRINTF("\tfacet normal %f %f %f\n", SPLIT3(N));
  PRINTF("\t\touter loop\n");
  PRINTF("\t\t\tvertex %f %f %f\n", SPLIT3(facet->vertices[0]));
  PRINTF("\t\t\tvertex %f %f %f\n", SPLIT3(facet->vertices[1]));
  PRINTF("\t\t\tvertex %f %f %f\n", SPLIT3(facet->vertices[2]));
  PRINTF("\t\tendloop\n");
  PRINTF("\tendfacet\n");

  #undef PRINTF

exit:
  return res;
error:
  goto exit;
}

static res_T
write_facet_binary(struct sstl_writer* writer, const struct sstl_facet* facet)
{
  float N[3] = {0,0,0};
  uint8_t bytes[4] = {0};
  int i = 0;
  res_T res = RES_OK;
  ASSERT(writer && facet);

  /* If necessary, automatically calculate the surface normal. */
  if(!f3_is_normalized(f3_set(N, facet->normal))) {
    calculate_normal
      (N, facet->vertices[0], facet->vertices[1], facet->vertices[2]);
  }

  #define WRITE(Float) { \
    union { uint32_t ui32; float f; } ucast = { .f = Float }; \
    bytes[0] = (uint8_t)((ucast.ui32 >> 0) & 0xFF); \
    bytes[1] = (uint8_t)((ucast.ui32 >> 8) & 0xFF); \
    bytes[2] = (uint8_t)((ucast.ui32 >> 16) & 0xFF); \
    bytes[3] = (uint8_t)((ucast.ui32 >> 24) & 0xFF); \
    if(fwrite(bytes, 1, 4, writer->fp) != 4) { \
      res = RES_IO_ERR; \
      goto error; \
    } \
  } (void)0

  WRITE(N[0]);
  WRITE(N[1]);
  WRITE(N[2]);

  FOR_EACH(i, 0, 3) {
    WRITE(facet->vertices[i][0]);
    WRITE(facet->vertices[i][1]);
    WRITE(facet->vertices[i][2]);
  }

  #undef WRITE

  /* #attribs (not used) */
  bytes[0] = 0;
  bytes[1] = 0;
  if(fwrite(bytes, 1, 2, writer->fp) != 2) {
    res = RES_IO_ERR;
    goto error;
  }

exit:
  return res;
error:
  WRITING_ERROR(writer);
  goto exit;
}

static res_T
finalize_ascii(struct sstl_writer* writer)
{
  res_T res = RES_OK;
  ASSERT(writer);

  if(fprintf(writer->fp, "endsolid\n") < 0) {
    res = RES_IO_ERR;
    goto error;
  }

  if(fflush(writer->fp) != 0) {
    res = RES_IO_ERR;
    goto error;
  }

exit:
  return res;
error:
  WRITING_ERROR(writer);
  goto exit;
}

static res_T
finalize_binary(struct sstl_writer* writer)
{
  res_T res = RES_OK;
  ASSERT(writer);

  /* Check that the number of triangles written is as expected.  Note that it
   * cannot be greater than the number supplied by the user when the writer was
   * created; an error must have been detected before writing a facet that
   * should not exist */
  ASSERT(writer->ntris < 0 || writer->ntris_written <= writer->ntris);
  if(writer->ntris >= 0 && writer->ntris_written < writer->ntris) {
    WARN(writer, "%s: triangles are missing\n", str_cget(&writer->filename));
  }

  if(writer->ntris_offset >= 0) {
    if(fseek(writer->fp, writer->ntris_offset, SEEK_SET) != 0) {
      WRITING_ERROR(writer);
      res = RES_IO_ERR;
      goto error;
    }

    res = write_ntriangles(writer, (uint32_t)writer->ntris_written);
    if(res != RES_OK) goto error;
  }

  if(fflush(writer->fp) != 0) {
    WRITING_ERROR(writer);
    res = RES_IO_ERR;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
finalize(struct sstl_writer* writer)
{
  res_T res = RES_OK;
  ASSERT(writer);

  switch(writer->type) {
    case SSTL_ASCII: res = finalize_ascii(writer); break;
    case SSTL_BINARY: res = finalize_binary(writer); break;
    default: FATAL("Unreachable code\n"); break;
  }
  return res;
}

static void
release_writer(ref_T* ref)
{
  struct sstl_writer* writer = CONTAINER_OF(ref, struct sstl_writer, ref);
  ASSERT(ref);

  if(writer->is_init) CHK(finalize(writer) == RES_OK);
  if(writer->is_fp_intern) CHK(fclose(writer->fp) == 0);
  str_release(&writer->filename);
  MEM_RM(writer->allocator, writer);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
res_T
sstl_writer_create
  (const struct sstl_writer_create_args* args,
   struct sstl_writer** out_writer)
{
  struct sstl_writer* writer = NULL;
  struct mem_allocator* allocator = NULL;
  struct logger* logger = NULL;
  res_T res = RES_OK;

  if(!out_writer) { res = RES_BAD_ARG; goto error; }
  if((res = check_sstl_writer_create_args(args)) != RES_OK) goto error;

  allocator = args->allocator ? args->allocator : &mem_default_allocator;
  logger = args->logger ? args->logger : LOGGER_DEFAULT;

  writer = MEM_CALLOC(allocator, 1, sizeof(*writer));
  if(!writer) {
    if(args->verbose) {
      logger_print(logger, LOG_ERROR, "Couldn't allocate the Star-StL writer\n");
    }
    res = RES_MEM_ERR;
    goto error;
  }

  ref_init(&writer->ref);
  writer->allocator = allocator;
  writer->logger = logger;
  writer->verbose = args->verbose;
  writer->type = args->type;
  writer->ntris = args->triangles_count;
  writer->ntris_offset = -1;
  str_init(writer->allocator, &writer->filename);

  if((res = setup_filename(writer, args)) != RES_OK) goto error;
  if((res = setup_stream(writer, args)) != RES_OK) goto error;
  if((res = write_header(writer, args)) != RES_OK) goto error;

  writer->is_init = 1;

exit:
  if(out_writer) *out_writer = writer;
  return res;
error:
  if(writer) { SSTL(writer_ref_put(writer)); writer = NULL; }
  goto exit;
}

res_T
sstl_writer_ref_get(struct sstl_writer* writer)
{
  if(!writer) return RES_BAD_ARG;
  ref_get(&writer->ref);
  return RES_OK;
}

res_T
sstl_writer_ref_put(struct sstl_writer* writer)
{
  if(!writer) return RES_BAD_ARG;
  ref_put(&writer->ref, release_writer);
  return RES_OK;
}

res_T
sstl_write_facet(struct sstl_writer* writer, const struct sstl_facet* facet)
{
  res_T res = RES_OK;

  if(!writer || !facet) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(writer->ntris == writer->ntris_written
  || writer->ntris_written == UINT32_MAX) {
    ERROR(writer, "%s: the number of facets is greater than expected\n",
      str_cget(&writer->filename));
    res = RES_BAD_OP;
    goto error;
  }

  switch(writer->type) {
    case SSTL_ASCII: res = write_facet_ascii(writer, facet); break;
    case SSTL_BINARY: res = write_facet_binary(writer, facet); break;
    default: FATAL("Unreachable code\n"); break;
  }
  if(res != RES_OK) goto error;

  ++writer->ntris_written;

exit:
  return res;
error:
  goto exit;
}
