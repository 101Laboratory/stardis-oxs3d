/* Copyright (C) 2015, 2016, 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#define _POSIX_C_SOURCE 200112L /* dirname support */

#include "s3daw.h"

#include <rsys/dynamic_array_uint.h>
#include <rsys/dynamic_array_float.h>
#include <rsys/float3.h>
#include <rsys/float4.h>
#include <rsys/hash_table.h>
#include <rsys/logger.h>
#include<rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#include <star/s3d.h>

#include <aw.h>
#include <polygon.h>
#include <string.h>

#define DARRAY_NAME shape
#define DARRAY_DATA struct s3d_shape*
#include <rsys/dynamic_array.h>

#define HTABLE_NAME vertex_id
#define HTABLE_DATA unsigned
#define HTABLE_KEY size_t
#include <rsys/hash_table.h>

#define HTABLE_NAME material
#define HTABLE_DATA struct aw_material
#define HTABLE_KEY struct str
#define HTABLE_KEY_FUNCTOR_EQ str_eq
#define HTABLE_KEY_FUNCTOR_HASH str_hash
#define HTABLE_KEY_FUNCTOR_INIT str_init
#define HTABLE_KEY_FUNCTOR_COPY str_copy
#define HTABLE_KEY_FUNCTOR_COPY_AND_RELEASE str_copy_and_release
#define HTABLE_KEY_FUNCTOR_RELEASE str_release
#include <rsys/hash_table.h>

struct s3daw {
  struct aw_obj* loader_obj;
  struct aw_mtl* loader_mtl;
  struct polygon* polygon;
  struct s3d_device* s3d;

  struct darray_shape shapes;

  int verbose;

  struct mem_allocator* allocator;
  struct logger* logger;
  ref_T ref;
};

struct context {
  const struct darray_float* positions;
  const struct darray_uint* indices;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
get_indices(const unsigned itri, unsigned ids[3], void* context)
{
  struct context* ctx = context;
  const unsigned* indices;
  ASSERT(ids && ctx && itri < darray_uint_size_get(ctx->indices)/3);
  indices = darray_uint_cdata_get(ctx->indices) + itri*3;
  ids[0] = indices[0];
  ids[1] = indices[1];
  ids[2] = indices[2];
}

static void
get_position(const unsigned ivert, float pos[3], void* context)
{
  struct context* ctx = context;
  const float* position;
  ASSERT(ctx && pos && ivert < darray_float_size_get(ctx->positions)/3);
  position = darray_float_cdata_get(ctx->positions) + ivert*3;
  pos[0] = position[0];
  pos[1] = position[1];
  pos[2] = position[2];
}

static INLINE void
shapes_clear(struct darray_shape* shapes)
{
  size_t i;
  ASSERT(shapes);
  FOR_EACH(i, 0, darray_shape_size_get(shapes)) {
    S3D(shape_ref_put(darray_shape_data_get(shapes)[i]));
  }
  darray_shape_clear(shapes);
}

static res_T
shape_register
  (struct s3daw* s3daw,
   const struct aw_obj_named_group* obj_mtl,
   struct darray_float* vertices,
   struct darray_uint* indices,
   struct htable_vertex_id* vertices_ids)
{
  struct context ctx;
  struct s3d_vertex_data vertex_data;
  struct s3d_shape* shape = NULL;
  size_t iface;
  size_t ntris, nverts;
  res_T res = RES_OK;
  ASSERT(s3daw && obj_mtl && vertices && indices && vertices_ids);

  /* Reset the scrach shape data */
  darray_float_clear(vertices);
  darray_uint_clear(indices);
  htable_vertex_id_clear(vertices_ids);

  FOR_EACH(iface, obj_mtl->face_id, obj_mtl->face_id + obj_mtl->faces_count) {
    struct aw_obj_face face;
    struct aw_obj_vertex vertex;
    struct aw_obj_vertex_data vdata;
    const uint32_t* tri_ids;
    uint32_t ntri_ids;
    size_t ivertex;
    size_t i;

    AW(obj_get_face(s3daw->loader_obj, iface, &face));

    /* Triangulate the face */
    POLYGON(clear(s3daw->polygon));
    FOR_EACH(ivertex, face.vertex_id, face.vertex_id + face.vertices_count) {
      float position[3];
      AW(obj_get_vertex(s3daw->loader_obj, ivertex, &vertex));
      AW(obj_get_vertex_data(s3daw->loader_obj, &vertex, &vdata));
      position[0] = (float)vdata.position[0];
      position[1] = (float)vdata.position[1];
      position[2] = (float)vdata.position[2];
      res = polygon_vertex_add(s3daw->polygon, position);
      if(res != RES_OK) goto error;
    }
    res = polygon_triangulate(s3daw->polygon, &tri_ids, &ntri_ids);
    if(res != RES_OK) goto error;

    FOR_EACH(i, 0, ntri_ids) {
      unsigned* ivertex_registered;

      /* Define if the obj_face vertex is already registered */
      ivertex = tri_ids[i] + face.vertex_id;
      AW(obj_get_vertex(s3daw->loader_obj, ivertex, &vertex));
      ivertex_registered = htable_vertex_id_find(vertices_ids, &vertex.position_id);

      if(ivertex_registered) {
        /* Vertex is registered. Simply add its id to the indices */
        res = darray_uint_push_back(indices, ivertex_registered);
        if(res != RES_OK) goto error;
      } else {
        /* Vertex is not registered. Register it and add its id to the indices */
        float position[3];
        const unsigned ivertex_new = (unsigned)darray_float_size_get(vertices)/3;

        AW(obj_get_vertex_data(s3daw->loader_obj, &vertex, &vdata));
        position[0] = (float)vdata.position[0];
        position[1] = (float)vdata.position[1];
        position[2] = (float)vdata.position[2];

        #define CALL(Func) if((res = Func) != RES_OK) goto error;
        CALL(darray_float_push_back(vertices, position+0));
        CALL(darray_float_push_back(vertices, position+1));
        CALL(darray_float_push_back(vertices, position+2));
        CALL(darray_uint_push_back(indices, &ivertex_new));
        CALL(htable_vertex_id_set(vertices_ids, &vertex.position_id, &ivertex_new));
        #undef CALL
      }
    }
  }

  nverts = darray_float_size_get(vertices) / 3;
  ntris = darray_uint_size_get(indices) / 3;
  if(!ntris) goto exit; /* Nothing to do */

  res = s3d_shape_create_mesh(s3daw->s3d, &shape);
  if(res != RES_OK) goto error;

  vertex_data.usage = S3D_POSITION;
  vertex_data.type = S3D_FLOAT3;
  vertex_data.get = get_position;
  ctx.indices = indices;
  ctx.positions = vertices;

  /* Setup the S3D mesh data */
  res = s3d_mesh_setup_indexed_vertices(shape, (unsigned)ntris, get_indices,
    (unsigned)nverts, &vertex_data, 1, &ctx);
  if(res != RES_OK) goto error;

  /* Register the shape */
  res = darray_shape_push_back(&s3daw->shapes, &shape);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  if(shape) S3D(shape_ref_put(shape));
  goto exit;
}

static res_T
shapes_create(struct s3daw* s3daw, const char* filename)
{
  struct darray_uint indices;
  struct darray_float vertices;
  struct htable_vertex_id vertices_ids;
  struct aw_obj_desc obj_desc;
  struct aw_obj_named_group grp;
  res_T res = RES_OK;
  ASSERT(s3daw && filename);

  /* Clean up the previously created shapes */
  shapes_clear(&s3daw->shapes);

  /* Init scratch data structures */
  darray_uint_init(s3daw->allocator, &indices);
  darray_float_init(s3daw->allocator, &vertices);
  htable_vertex_id_init(s3daw->allocator, &vertices_ids);

  AW(obj_get_desc(s3daw->loader_obj, &obj_desc));

  if(obj_desc.faces_count == 0) {
    if(s3daw->verbose) {
      logger_print(s3daw->logger, LOG_WARNING,
        "Empty file content `%s'. It is discarded.\n", filename);
    }
    goto exit;
  }

  if(!obj_desc.usemtls_count) { /* No material grouping => triangle soup */
    grp.name = NULL;
    grp.face_id = 0;
    grp.faces_count = obj_desc.faces_count;
    res = shape_register(s3daw, &grp, &vertices, &indices, &vertices_ids);
    if(res != RES_OK) goto error;
  } else { /* Create a S3D shape per material */
    size_t imtl;
    FOR_EACH(imtl, 0, obj_desc.usemtls_count) {
      AW(obj_get_mtl(s3daw->loader_obj, imtl, &grp));
      res = shape_register(s3daw, &grp, &vertices, &indices, &vertices_ids);
      if(res != RES_OK) goto error;
    }
  }

exit:
  darray_uint_release(&indices);
  darray_float_release(&vertices);
  htable_vertex_id_release(&vertices_ids);
  return res;
error:
  shapes_clear(&s3daw->shapes);
  goto exit;
}

static void
release_s3daw(ref_T* ref)
{
  struct s3daw* s3daw = CONTAINER_OF(ref, struct s3daw, ref);
  ASSERT(ref);
  if(s3daw->loader_obj) AW(obj_ref_put(s3daw->loader_obj));
  if(s3daw->loader_mtl) AW(mtl_ref_put(s3daw->loader_mtl));
  if(s3daw->polygon) POLYGON(ref_put(s3daw->polygon));
  if(s3daw->s3d) S3D(device_ref_put(s3daw->s3d));
  shapes_clear(&s3daw->shapes);
  darray_shape_release(&s3daw->shapes);
  MEM_RM(s3daw->allocator, s3daw);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s3daw_create
  (struct logger* logger,
   struct mem_allocator* allocator,
   struct aw_obj* obj,
   struct aw_mtl* mtl,
   struct s3d_device* s3d,
   const int verbose,
   struct s3daw** out_s3daw)
{
  struct s3daw* s3daw = NULL;
  struct mem_allocator* mem_allocator;
  res_T res = RES_OK;

  if(!s3d || !out_s3daw) {
    res = RES_BAD_ARG;
    goto error;
  }
  mem_allocator = allocator ? allocator : &mem_default_allocator;
  s3daw = MEM_CALLOC(mem_allocator, 1, sizeof(struct s3daw));
  if(!s3daw) {
    res = RES_MEM_ERR;
    goto error;
  }

  ref_init(&s3daw->ref);
  s3daw->allocator = mem_allocator;
  s3daw->logger = logger ? logger : LOGGER_DEFAULT;
  s3daw->verbose = verbose;
  S3D(device_ref_get(s3d));
  s3daw->s3d = s3d;
  darray_shape_init(s3daw->allocator, &s3daw->shapes);

  res = polygon_create(s3daw->allocator, &s3daw->polygon);
  if(res != RES_OK) goto error;

  if(obj) {
    AW(obj_ref_get(obj));
    s3daw->loader_obj = obj;
  } else {
    res = aw_obj_create(logger, allocator, verbose, &s3daw->loader_obj);
    if(res != RES_OK) goto error;
  }

  if(mtl) {
    AW(mtl_ref_get(mtl));
    s3daw->loader_mtl = mtl;
  } else {
    res = aw_mtl_create(logger, allocator, verbose, &s3daw->loader_mtl);
    if(res != RES_OK) goto error;
  }

exit:
  if(out_s3daw) *out_s3daw = s3daw;
  return res;
error:
  if(s3daw) {
    S3DAW(ref_put(s3daw));
    s3daw = NULL;
  }
  goto exit;
}

res_T
s3daw_ref_get(struct s3daw* s3daw)
{
  if(!s3daw) return RES_BAD_ARG;
  ref_get(&s3daw->ref);
  return RES_OK;
}

res_T
s3daw_ref_put(struct s3daw* s3daw)
{
  if(!s3daw) return RES_BAD_ARG;
  ref_put(&s3daw->ref, release_s3daw);
  return RES_OK;
}

res_T
s3daw_get_loaders(struct s3daw* s3daw, struct aw_obj** obj, struct aw_mtl** mtl)
{
  if(!s3daw) return RES_BAD_ARG;
  if(obj) *obj = s3daw->loader_obj;
  if(mtl) *mtl = s3daw->loader_mtl;
  return RES_OK;
}

res_T
s3daw_get_s3d_device(struct s3daw* s3daw, struct s3d_device** s3d)
{
  if(!s3daw || !s3d) return RES_BAD_ARG;
  *s3d = s3daw->s3d;
  return RES_OK;
}

res_T
s3daw_load(struct s3daw* s3daw, const char* filename)
{
  res_T res;

  if(!s3daw || !filename) return RES_BAD_ARG;

  res = aw_obj_load(s3daw->loader_obj, filename);
  if(res != RES_OK) return res;
  return shapes_create(s3daw, filename);
}

res_T
s3daw_load_stream(struct s3daw* s3daw, FILE* stream)
{
  res_T res = RES_OK;

  if(!s3daw || !stream) return RES_BAD_ARG;

  res = aw_obj_load_stream(s3daw->loader_obj, stream, "stream");
  if(res != RES_OK) return res;
  return shapes_create(s3daw, "./");
}

res_T
s3daw_clear(struct s3daw* s3daw)
{
  if(!s3daw) return RES_BAD_ARG;

  /* Clear intermediary data structures */
  POLYGON(clear(s3daw->polygon));

  /* Clear shape */
  shapes_clear(&s3daw->shapes);

  AW(obj_clear(s3daw->loader_obj));
  AW(mtl_clear(s3daw->loader_mtl));

  return RES_OK;
}

res_T
s3daw_get_shapes_count(const struct s3daw* s3daw, size_t* nshapes)
{
  if(!s3daw || !nshapes) return RES_BAD_ARG;
  *nshapes = darray_shape_size_get(&s3daw->shapes);
  return RES_OK;
}

res_T
s3daw_get_shape
  (struct s3daw* s3daw,
   const size_t ishape,
   struct s3d_shape** shape)
{
  if(!s3daw || !shape || ishape >= darray_shape_size_get(&s3daw->shapes))
    return RES_BAD_ARG;
  *shape = darray_shape_data_get(&s3daw->shapes)[ishape];
  return RES_OK;
}

res_T
s3daw_attach_to_scene(struct s3daw* s3daw, struct s3d_scene* scene)
{
  struct s3d_shape* shape = NULL;
  size_t i=0, n=0;
  res_T res = RES_OK;

  if(!s3daw) {
    res = RES_BAD_ARG;
    goto error;
  }

  S3DAW(get_shapes_count(s3daw, &n));
  FOR_EACH(i, 0, n) {
    S3DAW(get_shape(s3daw, i, &shape));
    res = s3d_scene_attach_shape(scene, shape);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  /* Rollback the attachments */
  n = i;
  FOR_EACH(i, 0, n) {
    S3DAW(get_shape(s3daw, i, &shape));
    S3D(scene_detach_shape(scene, shape));
  }
  goto exit;
}

res_T
s3daw_detach_from_scene(struct s3daw* s3daw, struct s3d_scene* scene)
{
  struct s3d_shape* shape = NULL;
  size_t i=0, n=0;
  res_T res = RES_OK;

  if(!s3daw) {
    res = RES_BAD_ARG;
    goto error;
  }

  S3DAW(get_shapes_count(s3daw, &n));
  FOR_EACH(i, 0, n) {
    S3DAW(get_shape(s3daw, i, &shape));
    res = s3d_scene_detach_shape(scene, shape);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  /* Rollback the detachments */
  n = i;
  FOR_EACH(i, 0, n) {
    S3DAW(get_shape(s3daw, i, &shape));
    S3D(scene_attach_shape(scene, shape));
  }
  goto exit;
}

