/* Copyright (C) 2016, 2018, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "s3dstl.h"

#include <rsys/float3.h>
#include <rsys/logger.h>
#include<rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#include <star/s3d.h>
#include <star/sstl.h>

struct s3dstl {
  struct s3d_device* s3d;
  struct s3d_shape* shape;

  struct sstl* sstl;
  struct logger* logger;
  struct mem_allocator* allocator;
  int verbose;
  ref_T ref;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
get_position(const unsigned ivert, float pos[3], void* ctx)
{
  const struct sstl_desc* desc = ctx;
  ASSERT(ctx && pos && ivert < desc->vertices_count);
  f3_set(pos, desc->vertices + ivert * 3);
}

static void
get_indices(const unsigned itri, unsigned ids[3], void* ctx)
{
  const struct sstl_desc* desc = ctx;
  const unsigned* indices;
  ASSERT(ctx && ids && itri < desc->triangles_count);
  indices = desc->indices + itri*3;
  /* Note that in the Star-STL loader, front faces are CCW ordered while in to
   * the Star-3D library front faces are CW ordered. Flip the vertex index in
   * order to ensure that the 3D and the STL front faces are the same */
  ids[0] = indices[2];
  ids[1] = indices[1];
  ids[2] = indices[0];
}

static void
log_error(const struct s3dstl* s3dstl, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(s3dstl && msg);
  if(s3dstl->verbose) {
    res_T res; (void)res;
    va_start(vargs_list, msg);
    res = logger_vprint(s3dstl->logger, LOG_ERROR, msg, vargs_list);
    ASSERT(res == RES_OK);
    va_end(vargs_list);
  }
}

static res_T
shape_create(struct s3dstl* s3dstl, const char* filename)
{
  struct s3d_vertex_data vertex_data;
  struct s3d_shape* shape = NULL;
  struct sstl_desc desc;
  res_T res = RES_OK;
  ASSERT(s3dstl && filename);

  res = sstl_get_desc(s3dstl->sstl, &desc);
  if(res != RES_OK) {
    log_error(s3dstl, "%s: couldn't retrieve the STL descriptor.\n", filename);
    goto error;
  }

  if(!desc.triangles_count) goto exit;

  res = s3d_shape_create_mesh(s3dstl->s3d, &shape);
  if(res != RES_OK) {
    log_error(s3dstl, "%s: couldn't create the Star-3D shape.\n", filename);
    goto error;
  }

  vertex_data.usage = S3D_POSITION;
  vertex_data.type = S3D_FLOAT3;
  vertex_data.get = get_position;

  res = s3d_mesh_setup_indexed_vertices(shape, (unsigned)desc.triangles_count,
    get_indices, (unsigned)desc.vertices_count, &vertex_data, 1, &desc);
  if(res != RES_OK) {
    log_error(s3dstl, "%s: couldn't setup the data of the Star-3D shape.\n",
      filename);
    goto error;
  }

  if(s3dstl->shape) S3D(shape_ref_put(s3dstl->shape));
  s3dstl->shape = shape;

exit:
  return res;
error:
  if(shape) S3D(shape_ref_put(shape));
  goto exit;
}

static void
release_s3dstl(ref_T* ref)
{
  struct s3dstl* s3dstl;
  ASSERT(ref);

  s3dstl = CONTAINER_OF(ref, struct s3dstl, ref);
  if(s3dstl->sstl) SSTL(ref_put(s3dstl->sstl));
  if(s3dstl->s3d) S3D(device_ref_put(s3dstl->s3d));
  if(s3dstl->shape) S3D(shape_ref_put(s3dstl->shape));

  MEM_RM(s3dstl->allocator, s3dstl);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s3dstl_create
  (struct logger* log,
   struct mem_allocator* allocator,
   struct sstl* sstl,
   struct s3d_device* s3d,
   const int verbose,
   struct s3dstl** out_s3dstl)
{
  struct s3dstl* s3dstl = NULL;
  struct mem_allocator* mem_allocator;
  struct logger* logger = NULL;
  res_T res = RES_OK;

  if(!s3d || !out_s3dstl) {
    res = RES_BAD_ARG;
    goto error;
  }

  mem_allocator = allocator ? allocator :  &mem_default_allocator;
  logger = log ? log : LOGGER_DEFAULT;

  s3dstl = MEM_CALLOC(mem_allocator, 1, sizeof(struct s3dstl));
  if(!s3dstl) {
    if(verbose) {
      logger_print(logger, LOG_ERROR,
        "Couldn't allocate the Star-3DSTL device.\n");
    }
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&s3dstl->ref);
  s3dstl->allocator = mem_allocator;
  s3dstl->logger = logger;
  s3dstl->verbose = verbose;
  S3D(device_ref_get(s3d));
  s3dstl->s3d = s3d;

  if(sstl) {
    SSTL(ref_get(sstl));
    s3dstl->sstl = sstl;
  } else {
    res = sstl_create(logger, allocator, verbose, &s3dstl->sstl);
    if(res != RES_OK) {
      if(verbose) {
        logger_print(logger, LOG_ERROR,
          "Couldn't create the loader of the STL fileformat.\n");
      }
      goto error;
    }
  }

exit:
  if(out_s3dstl) *out_s3dstl = s3dstl;
  return res;
error:
  if(s3dstl) {
    S3DSTL(ref_put(s3dstl));
    s3dstl = NULL;
  }
  goto exit;
}

res_T
s3dstl_ref_get(struct s3dstl* s3dstl)
{
  if(!s3dstl) return RES_BAD_ARG;
  ref_get(&s3dstl->ref);
  return RES_OK;
}

res_T
s3dstl_ref_put(struct s3dstl* s3dstl)
{
  if(!s3dstl) return RES_BAD_ARG;
  ref_put(&s3dstl->ref, release_s3dstl);
  return RES_OK;
}

res_T
s3dstl_get_sstl(struct s3dstl* s3dstl, struct sstl** sstl)
{
  if(!s3dstl || !sstl) return RES_BAD_ARG;
  *sstl = s3dstl->sstl;
  return RES_OK;
}

res_T
s3dstl_load(struct s3dstl* s3dstl, const char* filename)
{
  res_T res;
  if(!s3dstl || !filename) return RES_BAD_ARG;
  res = sstl_load(s3dstl->sstl, filename);
  if(res != RES_OK) return res;
  return shape_create(s3dstl, filename);
}

res_T
s3dstl_load_stream(struct s3dstl* s3dstl, FILE* stream)
{
  res_T res;
  if(!s3dstl || !stream) return RES_BAD_ARG;
  res = sstl_load_stream_ascii(s3dstl->sstl, stream, "STREAM");
  if(res != RES_OK) return res;
  return shape_create(s3dstl, "STREAM");
}

res_T
s3dstl_clear(struct s3dstl* s3dstl)
{
  if(!s3dstl) return RES_BAD_ARG;
  if(s3dstl->shape) {
    S3D(shape_ref_put(s3dstl->shape));
    s3dstl->shape = NULL;
  }
  return RES_OK;
}

res_T
s3dstl_get_shape(struct s3dstl* s3dstl, struct s3d_shape** shape)
{
  if(!s3dstl || !shape) return RES_BAD_ARG;
  *shape = s3dstl->shape;
  return RES_OK;
}

