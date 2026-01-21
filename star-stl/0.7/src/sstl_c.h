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

#ifndef SSTL_C_H
#define SSTL_C_H

#include "sstl.h"

#include <rsys/float3.h>
#include <rsys/hash_table.h>
#include <rsys/logger.h>
#include <rsys/ref_count.h>
#include <rsys/str.h>
#include <rsys/stretchy_array.h>

#include <errno.h>
#include <stdio.h>

/* Helper macros for logging */
#define LOG__(Dev, Lvl, Type, ...) { \
  if ((Dev)->verbose >= (Lvl)) \
    logger_print((Dev)->logger, Type, __VA_ARGS__); \
} (void)0
#define ERROR(Dev, ...) LOG__(Dev, 1, LOG_ERROR,   "error: "__VA_ARGS__)
#define WARN(Dev, ...) LOG__(Dev, 2, LOG_WARNING, "warning: "__VA_ARGS__)
#define INFO(Dev, ...) LOG__(Dev, 3, LOG_OUTPUT,  __VA_ARGS__)

struct vertex { float xyz[3]; };

static INLINE char
eq_vertex(const struct vertex* a, const struct vertex* b)
{
  return (char)
  (  a->xyz[0] == b->xyz[0]
  && a->xyz[1] == b->xyz[1]
  && a->xyz[2] == b->xyz[2]);
}

/* Declare the hash table that map a vertex to its index */
#define HTABLE_NAME vertex
#define HTABLE_DATA unsigned
#define HTABLE_KEY struct vertex
#define HTABLE_KEY_FUNCTOR_EQ eq_vertex
#include <rsys/hash_table.h>

/* Forward declarations */
struct logger;
struct mem_allocator;

struct sstl {
  struct str filename;
  struct str name;
  enum sstl_type type;

  /* Temporary structure used to map a vertex to its id */
  struct htable_vertex vertex2id;

  float* normals;
  float* vertices;
  unsigned* indices;

  struct logger* logger;
  struct mem_allocator* allocator;
  int verbose;
  ref_T ref;
};

static INLINE void
clear(struct sstl* sstl)
{
  ASSERT(sstl);
  str_clear(&sstl->name);
  sa_release(sstl->indices);
  sa_release(sstl->vertices);
  sa_release(sstl->normals);
  sstl->indices = NULL;
  sstl->vertices = NULL;
  sstl->normals = NULL;
  sstl->type = SSTL_NONE__;
  htable_vertex_clear(&sstl->vertex2id);
}

static INLINE int
file_is_seekable(FILE* fp)
{
  ASSERT(fp);
  if(fseek(fp, 0, SEEK_CUR) >= 0) {
    return 1; /* File is seekable */
  } else {
    CHK(errno == ESPIPE);
    return 0; /* File is not seekable */
  }
}

static INLINE float*
calculate_normal
  (float N[3],
   const float v0[3],
   const float v1[3],
   const float v2[3])
{
  float E0[3], E1[3];
  ASSERT(N && v0 && v1 && v2);

  /* Vertices are CCW and the normal follows the right handed rule */
  f3_sub(E0, v1, v0);
  f3_sub(E1, v2, v0);
  f3_cross(N, E0, E1);
  f3_normalize(N, N);

  return N;
}

extern LOCAL_SYM res_T
load_stream_ascii
  (struct sstl* sstl,
   FILE* stream,
   const char* stream_name);

extern LOCAL_SYM res_T
load_stream_binary
  (struct sstl* sstl,
   FILE* stream,
   const char* stream_name);

extern LOCAL_SYM res_T
register_vertex
  (struct sstl* sstl,
   const float v[3]);

#endif /* SSTL_C_H */
