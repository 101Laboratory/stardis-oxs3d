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

#ifndef SSTL_H
#define SSTL_H

#include <rsys/rsys.h>

/* Library symbol management */
#if defined(SSTL_SHARED_BUILD)
  #define SSTL_API extern EXPORT_SYM /* Build shared library */
#elif defined(SSTL_STATUC) /* Use/build statuc library */
  #define SSTL_API extern LOCAL_SYM
#else /* Use shared library */
  #define SSTL_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the sstl function `Func'
 * returns an error. One should use this macro on sstl function calls for which
 * no explicit error checking is performed. */
#ifndef NDEBUG
  #define SSTL(Func) ASSERT(sstl_ ## Func == RES_OK)
#else
  #define SSTL(Func) sstl_ ## Func
#endif

/* The type of a read file */
enum sstl_type {
  SSTL_ASCII,
  SSTL_BINARY,
  SSTL_NONE__
};

/* Descriptor of a loaded STL */
struct sstl_desc {
  const char* filename;
  const char* solid_name; /* May be NULL <=> no name */
  enum sstl_type type; /* The type of the file */

  /* Front faces are CCW ordered and the normals follow the right handed rule */
  const float* vertices; /* triangles_count * 3 coordinates */
  const unsigned* indices; /* triangles_count * 3 indices */
  const float* normals; /* Per triangle normalized normal */

  size_t triangles_count;
  size_t vertices_count;
};
#define SSTL_DESC_NULL__ {0}
static const struct sstl_desc SSTL_DESC_NULL = SSTL_DESC_NULL__;

struct sstl_writer_create_args {
  const char* filename; /* Name of the file to write or of the provided stream */
  FILE* stream; /* NULL <=> write data to the file "name" */

  enum sstl_type type; /* Written data is either ASCII or binary */

  const char* solid_name; /* Can be NULL. Not used for binary StL */

  /* <0 <=> The number of triangles is calculated automatically.
   * Must be set when writing binary data to a non-searchable stream */
  long triangles_count;

  struct logger* logger; /* NULL <=> use default logger */
  struct mem_allocator* allocator; /* NULL <=> use default allocator */
  int verbose; /* verbosity level */
};
#define SSTL_WRITER_CREATE_ARGS_DEFAULT__ \
  {NULL, NULL, SSTL_ASCII, NULL, -1, NULL, NULL, 0}
static const struct sstl_writer_create_args SSTL_WRITER_CREATE_ARGS_DEFAULT =
  SSTL_WRITER_CREATE_ARGS_DEFAULT__;

struct sstl_facet {
  float normal[3];
  float vertices[3][3];
};
#define SSTL_FACET_NULL__ {0}
static const struct sstl_facet SSTL_FACET_NULL = SSTL_FACET_NULL__;

/* Forward declaration of external types */
struct logger;
struct mem_allocator;

/* Forward declaration of opaque data types */
struct sstl;
struct sstl_writer;

/*******************************************************************************
 * Star-STL API
 ******************************************************************************/
BEGIN_DECLS

SSTL_API res_T
sstl_create
  (struct logger* logger, /* NULL <=> use default logger */
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   struct sstl** sstl);

SSTL_API res_T
sstl_ref_get
  (struct sstl* sstl);

SSTL_API res_T
sstl_ref_put
  (struct sstl* sstl);

/* The type of StL (ASCII or binary) is defined from the contents of the file.
 * The file must therefore be seekable, i.e. it cannot be a pipe, a FIFO or a
 * socket */
SSTL_API res_T
sstl_load
  (struct sstl* sstl,
   const char* filename);

SSTL_API res_T
sstl_load_ascii
  (struct sstl* sstl,
   const char* filename);

SSTL_API res_T
sstl_load_binary
  (struct sstl* sstl,
   const char* filename);

/* The type of StL (Binary or ASCII) is defined from the contents of the file.
 * The file pointer must therefore be seekable, i.e. it cannot be a pipe, a FIFO
 * or a socket */
SSTL_API res_T
sstl_load_stream
  (struct sstl* sstl,
   FILE* stream,
   const char* stream_name);

SSTL_API res_T
sstl_load_stream_ascii
  (struct sstl* sstl,
   FILE* stream,
   const char* stream_name);

SSTL_API res_T
sstl_load_stream_binary
  (struct sstl* sstl,
   FILE* stream,
   const char* stream_name);

/* The returned descriptor is valid until a new load process */
SSTL_API res_T
sstl_get_desc
  (struct sstl* sstl,
   struct sstl_desc* desc);

/*******************************************************************************
 * Descriptor API. This is a set of help functions for retrieving mesh data from
 * their raw representation.
 ******************************************************************************/
static INLINE res_T
sstl_desc_get_triangle_ids
  (const struct sstl_desc* desc,
   const size_t itri,
   unsigned ids[3])
{
  if(!desc || !ids || itri >= desc->triangles_count) return RES_BAD_ARG;
  ids[0] = desc->indices[itri*3/*#ids per triangle*/ + 0];
  ids[1] = desc->indices[itri*3/*#ids per triangle*/ + 1];
  ids[2] = desc->indices[itri*3/*#ids per triangle*/ + 2];
  return RES_OK;
}

static INLINE res_T
sstl_desc_get_vertex_coords
  (const struct sstl_desc* desc,
   const size_t ivtx,
   float coords[3])
{
  if(!desc || !coords || ivtx >= desc->vertices_count) return RES_BAD_ARG;
  coords[0] = desc->vertices[ivtx*3/*#coords per vertex*/ + 0];
  coords[1] = desc->vertices[ivtx*3/*#coords per vertex*/ + 1];
  coords[2] = desc->vertices[ivtx*3/*#coords per vertex*/ + 2];
  return RES_OK;
}

static INLINE res_T
sstl_desc_get_facet
  (const struct sstl_desc* desc,
   const size_t itri,
   struct sstl_facet* facet)
{
  unsigned ids[3] = {0,0,0};

  if(!desc || !facet || itri >= desc->triangles_count) return RES_BAD_ARG;

  #define CALL(Func) {res_T res; if((res = Func) != RES_OK) return res;} (void)0
  CALL(sstl_desc_get_triangle_ids(desc, itri, ids));
  CALL(sstl_desc_get_vertex_coords(desc, ids[0], facet->vertices[0]));
  CALL(sstl_desc_get_vertex_coords(desc, ids[1], facet->vertices[1]));
  CALL(sstl_desc_get_vertex_coords(desc, ids[2], facet->vertices[2]));
  #undef CALL

  return RES_OK;
}

/*******************************************************************************
 * Writer API
 ******************************************************************************/
SSTL_API res_T
sstl_writer_create
  (const struct sstl_writer_create_args* args,
   struct sstl_writer** writer);

SSTL_API res_T
sstl_writer_ref_get
  (struct sstl_writer* writer);

SSTL_API res_T
sstl_writer_ref_put
  (struct sstl_writer* writer);

SSTL_API res_T
sstl_write_facet
  (struct sstl_writer* writer,
   const struct sstl_facet* facet);

END_DECLS

#endif /* SSTL_H */
