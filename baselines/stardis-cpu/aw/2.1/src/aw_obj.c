/* Copyright (C) 2014-2017, 2020-2023 Vincent Forest (vaplv@free.fr)
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

#include "aw_c.h"

#include <rsys/cstr.h>
#include <rsys/dynamic_array_double.h>
#include <rsys/logger.h>
#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>
#include <rsys/str.h>
#include <rsys/text_reader.h>

#include <float.h> /* DBL_MAX & DBL_MIN definition */
#include <stdarg.h>

#ifdef COMPILER_CL
  #pragma warning(push)
  #pragma warning(disable:4706) /* Assignment within a condition */
#endif

static const char* MSG_PREFIX_INFO = "load-obj:info: ";
static const char* MSG_PREFIX_ERROR = "load-obj:error: ";
static const char* MSG_PREFIX_WARNING = "load-obj:warning: ";

/* Generate the darray_vertex data structure */
#define DARRAY_NAME vertex
#define DARRAY_DATA struct aw_obj_vertex
#include <rsys/dynamic_array.h>

/* Generate the darray_face data structure */
#define DARRAY_NAME face
#define DARRAY_DATA struct aw_obj_face
#include <rsys/dynamic_array.h>

static const struct aw_obj_vertex VERTEX_NULL = {
  AW_ID_NONE, AW_ID_NONE, AW_ID_NONE
};

struct named_group {
  struct str name;
  size_t face_id; /* Index of the first group face */
  size_t faces_count;
};

static INLINE void
named_group_init(struct mem_allocator* allocator, struct named_group* grp)
{
  str_init(allocator, &grp->name);
}

static INLINE void
named_group_release(struct named_group* grp)
{
  ASSERT(grp);
  str_release(&grp->name);
}

static INLINE res_T
named_group_copy(struct named_group* dst, const struct named_group* src)
{
  ASSERT(dst && src);
  if(dst == src) return RES_OK;
  dst->face_id = src->face_id;
  dst->faces_count = src->faces_count;
  return str_copy(&dst->name, &src->name);
}

static INLINE res_T
named_group_copy_and_release(struct named_group* dst, struct named_group* src)
{
  ASSERT(dst && src);
  if(dst == src) return RES_OK;
  dst->face_id = src->face_id;
  dst->faces_count = src->faces_count;
  return str_copy_and_release(&dst->name, &src->name);
}

/* Generate the darray_named_group data structure */
#define DARRAY_NAME named_group
#define DARRAY_DATA struct named_group
#define DARRAY_FUNCTOR_INIT named_group_init
#define DARRAY_FUNCTOR_RELEASE named_group_release
#define DARRAY_FUNCTOR_COPY named_group_copy
#define DARRAY_FUNCTOR_COPY_AND_RELEASE named_group_copy_and_release
#include <rsys/dynamic_array.h>

/* Generate the darray_smooth_group data structure */
#define DARRAY_NAME smooth_group
#define DARRAY_DATA struct aw_obj_smooth_group
#include <rsys/dynamic_array.h>

/* Generate the darray_mtllib data structure */
#define DARRAY_NAME mtllib
#define DARRAY_DATA struct str
#define DARRAY_FUNCTOR_INIT str_init
#define DARRAY_FUNCTOR_RELEASE str_release
#define DARRAY_FUNCTOR_COPY str_copy
#define DARRAY_FUNCTOR_COPY_AND_RELEASE str_copy_and_release
#include <rsys/dynamic_array.h>

struct aw_obj {
  struct darray_double positions; /* double4 */
  struct darray_double normals; /* double3 */
  struct darray_double texcoords; /* double3 */
  struct darray_vertex vertices;
  struct darray_face faces;
  struct darray_named_group groups;
  struct darray_smooth_group smooth_groups;
  struct darray_named_group usemtls;
  struct darray_mtllib mtllibs;

  size_t igroups_active; /* Index toward the first active group */

  ref_T ref;
  struct mem_allocator* allocator;
  struct logger* logger;
  struct logger logger__; /* Default logger */
  int verbose;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE void
log_msg
  (const struct aw_obj* obj,
   const enum log_type stream,
   const char* msg,
   va_list vargs)
{
  ASSERT(obj && msg);
  if(obj->verbose) {
    res_T res; (void)res;
    res = logger_vprint(obj->logger, stream, msg, vargs);
    ASSERT(res == RES_OK);
  }
}

static INLINE void
log_err(const struct aw_obj* obj, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(obj && msg);

  va_start(vargs_list, msg);
  log_msg(obj, LOG_ERROR, msg, vargs_list);
  va_end(vargs_list);
}

static INLINE void
log_warn(const struct aw_obj* obj, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(obj && msg);

  va_start(vargs_list, msg);
  log_msg(obj, LOG_WARNING, msg, vargs_list);
  va_end(vargs_list);
}

static INLINE void
flush_groups(struct aw_obj* obj)
{
  size_t nfaces, ngrps;
  ASSERT(obj);

  nfaces = darray_face_size_get(&obj->faces);
  if(!nfaces) return; /* No face to flush */

  ngrps = darray_named_group_size_get(&obj->groups);
  if(!ngrps) return; /* No group */

  /* There should be an active group to flush if ngrps is not null */
  ASSERT(obj->igroups_active < ngrps);

  /* Setup the number of faces of each active group */
  FOR_EACH(obj->igroups_active, obj->igroups_active, ngrps) {
    struct named_group* grp;
    grp = darray_named_group_data_get(&obj->groups) + obj->igroups_active;
    ASSERT(grp->face_id <= nfaces);
    grp->faces_count = nfaces - grp->face_id;
  }
}

static INLINE void
flush_usemtl(struct aw_obj* obj)
{
  struct named_group* mtl;
  size_t nfaces, ngrps;
  ASSERT(obj);

  nfaces = darray_face_size_get(&obj->faces);
  if(!nfaces) return; /* No face to flush */

  ngrps = darray_named_group_size_get(&obj->usemtls);
  if(!ngrps) return; /* No group */

  /* Setup the number of faces of the current mtl */
  mtl = darray_named_group_data_get(&obj->usemtls) + (ngrps - 1);
  ASSERT(mtl->face_id <= nfaces);
  mtl->faces_count = nfaces - mtl->face_id;
}

static INLINE void
flush_smooth_group(struct aw_obj* obj)
{
  struct aw_obj_smooth_group* grp;
  size_t nfaces, ngrps;
  ASSERT(obj);

  nfaces = darray_face_size_get(&obj->faces);
  if(!nfaces) return; /* No face to flush */

  ngrps = darray_smooth_group_size_get(&obj->smooth_groups);
  if(!ngrps) return; /* No smooth group */

  /* Setup the number of faces of the current smoothed group */
  grp = darray_smooth_group_data_get(&obj->smooth_groups) + (ngrps - 1);
  ASSERT(grp->face_id <= nfaces);
  grp->faces_count = nfaces - grp->face_id;
}

static res_T
parse_doubleX_in_darray
  (struct darray_double* darray,
   const unsigned int count_min,
   const unsigned int count_max,
   const double default_value,
   char** tk_ctx)
{
  res_T res = RES_OK;
  size_t i;
  ASSERT(darray);

  i = darray_double_size_get(darray);

  res = darray_double_resize(darray, i + count_max);
  if(res != RES_OK) goto error;

  res = parse_doubleX(darray_double_data_get(darray) + i, count_min, count_max,
    -DBL_MAX, DBL_MAX, default_value, tk_ctx);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  darray_double_resize(darray, i);
  goto exit;
}

static res_T
string_to_vertex_id
  (const char* str, /* Input string */
   const size_t vertices_count, /* Current vertices count for a given attrib */
   size_t* id) /* Computed id */
{
  long id_long;
  res_T res;

  res = cstr_to_long(str, &id_long);
  if(res != RES_OK) return res;

  if(id_long > 0) {
    /* The obj indexation starts at 1 rather than 0 => subtract 1 to the vertex
     * attribute indices in order to match the C memory layout */
    *id = (size_t)(id_long - 1);
  } else if(id_long < 0) {
    /* One can count vertices back up the list from an element's position in
     * the file. In this case the index is negative. For instance, a reference
     * number of -1 indicates the vertex immediately above the element */
    *id = vertices_count - (size_t)labs(id_long);
  }
  if(*id >= vertices_count)
    return RES_BAD_ARG;
  return RES_OK;
}

static res_T
parse_face_vertex
  (struct aw_obj* obj,
   struct aw_obj_face* face,
   char* str)
{
  struct aw_obj_vertex vert = VERTEX_NULL;
  char* tk1 = NULL;
  char* tk2 = NULL;
  char* tk3 = NULL;
  char* tk_ctx = NULL;
  size_t npositions = 0;
  size_t nnormals = 0;
  size_t ntexcoords = 0;
  res_T res = RES_OK;
  ASSERT(obj && face && str);

  npositions = darray_double_size_get(&obj->positions) / 4;
  nnormals = darray_double_size_get(&obj->normals) / 3;
  ntexcoords = darray_double_size_get(&obj->texcoords) / 3;

  #define CALL(Func) if(RES_OK != (res = Func)) goto error

  /* Parse the position */
  tk1 = strtok_r(str, "/", &tk_ctx);
  ASSERT(tk1);
  CALL(string_to_vertex_id(tk1, npositions, &vert.position_id));

  tk2 = strtok_r(NULL, "/", &tk_ctx);
  if(tk2) {
    tk1 += strlen(tk1);
    if(tk2 > tk1 + 3) { /* Unexpected N `/' separators with N > 2 */
      res = RES_BAD_ARG;
      goto error;
    }

    if(tk2 == tk1 + 2) { /* `//' separator => No tex */
      /* Parse the normal */
      CALL(string_to_vertex_id(tk2, nnormals, &vert.normal_id));
    } else {
      /* Parse the texcoords and */
      CALL(string_to_vertex_id(tk2, ntexcoords, &vert.texcoord_id));

      tk3 = strtok_r(NULL, "", &tk_ctx);
      if(tk3) {
        /* Parse the normal */
        CALL(string_to_vertex_id(tk3, nnormals, &vert.normal_id));
      }
    }
  }

  /* Register the vertex */
  CALL(darray_vertex_push_back(&obj->vertices, &vert));
  ++face->vertices_count;

  #undef CALL
exit:
  return res;
error:
  goto exit;
}

static res_T
parse_face(struct aw_obj* obj, char** tk_ctx)
{
  struct aw_obj_face face;
  char* tk = NULL;
  res_T res = RES_OK;
  ASSERT(obj && tk_ctx);

  face.vertex_id = darray_vertex_size_get(&obj->vertices);
  face.vertices_count = 0;
  face.group_id = darray_named_group_size_get(&obj->groups) - 1;
  face.smooth_group_id = darray_smooth_group_size_get(&obj->smooth_groups) - 1;
  face.mtl_id = darray_named_group_size_get(&obj->usemtls) - 1;

  while((tk = strtok_r(NULL, " \t", tk_ctx))) {
    res = parse_face_vertex(obj, &face, tk);
    if(res != RES_OK) goto error;
  }

  /* Register the face */
  res = darray_face_push_back(&obj->faces, &face);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  /* Release the registered faces */
  CHK(darray_vertex_resize(&obj->vertices, face.vertex_id) == RES_OK);
  goto exit;
}

static res_T
parse_group(struct aw_obj* obj, char** tk_ctx)
{
  char* tk;
  size_t ngrps = 0;
  size_t igrp = 0;
  res_T res = RES_OK;
  ASSERT(obj && tk_ctx);

  flush_groups(obj);

  ngrps = igrp = darray_named_group_size_get(&obj->groups);
  obj->igroups_active = igrp;

  tk = strtok_r(NULL, " \t", tk_ctx); /* May be NULL */

  do {
    struct named_group* grp = NULL;

    /* Allocate a group */
    res = darray_named_group_resize(&obj->groups, igrp + 1);
    if(res != RES_OK) goto error;

    /* Fetch the group */
    grp = darray_named_group_data_get(&obj->groups) + igrp;
    ++igrp;

    /* Setup the group name */
    res = str_set(&grp->name, tk ? tk : "default");
    if(res != RES_OK) goto error;

    /* Initialize the group face indices */
    grp->face_id = darray_face_size_get(&obj->faces);
    grp->faces_count = 0;
  } while((tk = strtok_r(NULL, " \t", tk_ctx)));

exit:
  return res;
error:
  /* Release the created groups */
  CHK(darray_named_group_resize(&obj->groups, ngrps) == RES_OK);
  goto exit;
}

static res_T
parse_smooth_group(struct aw_obj* obj, char** tk_ctx)
{
  struct aw_obj_smooth_group grp;
  char* tk = NULL;
  res_T res;
  ASSERT(obj && tk_ctx);

  flush_smooth_group(obj);

  tk = strtok_r(NULL, " \t", tk_ctx);
  if(!tk) return RES_BAD_ARG;

  if(!strcmp(tk, "off")) {
    grp.is_smoothed = 0;
  } else if(!strcmp(tk, "on")) {
    grp.is_smoothed = 1;
  } else {
    int i;
    res = cstr_to_int(tk, &i);
    if(res != RES_OK) return res;
    grp.is_smoothed = i != 0;
  }

  /* Initialize the smoot group face indices */
  grp.face_id = darray_face_size_get(&obj->faces);
  grp.faces_count = 0;

  /* Register the smooth group */
  res = darray_smooth_group_push_back(&obj->smooth_groups, &grp);
  if(res != RES_OK) return res;

  return RES_OK;
}

static res_T
parse_mtllib(struct aw_obj* obj, char** tk_ctx)
{
  char* tk = NULL;
  size_t imtllib = 0;
  size_t nmtllibs = 0;
  res_T res = RES_OK;
  ASSERT(obj && tk_ctx);

  nmtllibs = imtllib = darray_mtllib_size_get(&obj->mtllibs);

  tk = strtok_r(NULL, " \t", tk_ctx);
  if(!tk) {
    res = RES_BAD_ARG;
    goto error;
  }

  do {
    struct str* str = NULL;

    /* Allocate the mtllib path */
    res = darray_mtllib_resize(&obj->mtllibs, imtllib + 1);
    if(res != RES_OK) goto error;

    /* Fetc the mtllib path */
    str = darray_mtllib_data_get(&obj->mtllibs) + imtllib;
    ++imtllib;

    /* Setup the mtllib name */
    res = str_set(str, tk);
    if(res != RES_OK) goto error;

  } while((tk = strtok_r(NULL, " \t", tk_ctx)));

exit:
  return res;
error:
  CHK(darray_mtllib_resize(&obj->mtllibs, nmtllibs) == RES_OK);
  goto exit;
}

static res_T
parse_usemtl(struct aw_obj* obj, char** tk_ctx)
{
  char* tk = NULL;
  struct named_group* mtl = NULL;
  size_t nmtls;
  res_T res = RES_OK;
  ASSERT(obj && tk_ctx);

  flush_usemtl(obj);

  tk = strtok_r(NULL, " \t", tk_ctx); /* Blanks are prohibited in mtl name */
  if(!tk) {
    res = RES_BAD_ARG;
    goto error;
  }

  nmtls = darray_named_group_size_get(&obj->usemtls);

  /* Allocate the material */
  res = darray_named_group_resize(&obj->usemtls, nmtls + 1);
  if(res != RES_OK) goto error;

  /* Fetch the material */
  mtl = darray_named_group_data_get(&obj->usemtls) + nmtls;

  /* Setup the material name */
  res = str_set(&mtl->name, tk);
  if(res != RES_OK) goto error;

  /* Initialize the material face indices */
  mtl->face_id = darray_face_size_get(&obj->faces);
  mtl->faces_count = 0;

exit:
  return res;
error:
  if(mtl) darray_named_group_pop_back(&obj->usemtls);
  goto exit;
}

static res_T
parse_obj_line(struct aw_obj* obj, struct txtrdr* txtrdr)
{
  char* tk = NULL;
  char* tk_ctx = NULL;
  res_T res = RES_OK;
  ASSERT(obj && txtrdr);

  tk = strtok_r(txtrdr_get_line(txtrdr), " \t", &tk_ctx);
  ASSERT(tk); /* A line should exist since it was parsed by txtrdr */

  if(!strcmp(tk, "v")) { /* Vertex position */
    res = parse_doubleX_in_darray(&obj->positions, 3, 4, 1.f, &tk_ctx);
  } else if(!strcmp(tk, "vn")) { /* Vertex normal */
    res = parse_doubleX_in_darray(&obj->normals, 3, 3, 0.f, &tk_ctx);
  } else if(!strcmp(tk, "vt")) { /* Vertex texture coordinates */
    res = parse_doubleX_in_darray(&obj->texcoords, 1, 3, 0.f, &tk_ctx);
  } else if(!strcmp(tk, "f") || !strcmp(tk, "fo")) { /* face element */
    res = parse_face(obj, &tk_ctx);
  } else if(!strcmp(tk, "g")) { /* Grouping */
    res = parse_group(obj, &tk_ctx);
  } else if(!strcmp(tk, "s")) { /* Smooth group */
    res = parse_smooth_group(obj, &tk_ctx);
  } else if(!strcmp(tk, "mtllib")) { /* Mtl library */
    res = parse_mtllib(obj, &tk_ctx);
  } else if(!strcmp(tk, "usemtl")) { /* Use the mtl library */
    res = parse_usemtl(obj, &tk_ctx);
  } else {
    log_warn(obj,
      "%s:%lu: warning: ignored or malformed directive `%s'.\n",
      txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr), tk);
    strtok_r(NULL, "", &tk_ctx); /* Discard remaining text */
  }
  if(res != RES_OK) {
    log_err(obj, "%s:%lu: parsing failed.\n",
      txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr));
    goto error;
  }

  tk = strtok_r(NULL, "", &tk_ctx);
  if(tk) {
    log_err(obj, "%s:%lu: unexpected text `%s'.\n",
      txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr), tk);
    res = RES_BAD_ARG;
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
load_stream(struct aw_obj* obj, FILE* stream, const char* stream_name)
{
  struct txtrdr* txtrdr = NULL;
  res_T res = RES_OK;
  ASSERT(obj && stream && stream_name);

  AW(obj_clear(obj));

  res = txtrdr_stream(obj->allocator, stream, stream_name, '#', &txtrdr);
  if(res != RES_OK) {
    log_err(obj, "Could not create the text reader.\n");
    goto error;
  }

  for(;;) {
    res = txtrdr_read_line(txtrdr);
    if(res != RES_OK) {
      log_err(obj, "%s: could not read the line `%lu'.\n",
        txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr));
      goto error;
    }

    if(!txtrdr_get_cline(txtrdr)) break; /* No more parsed line */

    res = parse_obj_line(obj, txtrdr);
    if(res != RES_OK) goto error;
  }

  flush_groups(obj);
  flush_smooth_group(obj);
  flush_usemtl(obj);
exit:
  if(txtrdr) txtrdr_ref_put(txtrdr);
  return res;
error:
  AW(obj_clear(obj));
  goto exit;
}

static void
obj_release(ref_T* ref)
{
  struct aw_obj* obj = CONTAINER_OF(ref, struct aw_obj, ref);
  ASSERT(ref);
  darray_double_release(&obj->positions);
  darray_double_release(&obj->normals);
  darray_double_release(&obj->texcoords);
  darray_vertex_release(&obj->vertices);
  darray_face_release(&obj->faces);
  darray_named_group_release(&obj->groups);
  darray_named_group_release(&obj->usemtls);
  darray_smooth_group_release(&obj->smooth_groups);
  darray_mtllib_release(&obj->mtllibs);
  if(obj->logger == &obj->logger__) logger_release(&obj->logger__);
  MEM_RM(obj->allocator, obj);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
aw_obj_create
  (struct logger* logger,
   struct mem_allocator* mem_allocator,
   const int verbose,
   struct aw_obj** obj_out)
{
  struct mem_allocator* allocator;
  struct aw_obj* obj = NULL;
  res_T res = RES_OK;

  if(!obj_out) {
    res = RES_BAD_ARG;
    goto error;
  }
  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  obj = MEM_CALLOC(allocator, 1, sizeof(struct aw_obj));
  if(!obj) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&obj->ref);
  obj->allocator = allocator;
  obj->verbose = verbose;
  darray_double_init(mem_allocator, &obj->positions);
  darray_double_init(mem_allocator, &obj->normals);
  darray_double_init(mem_allocator, &obj->texcoords);
  darray_vertex_init(mem_allocator, &obj->vertices);
  darray_face_init(mem_allocator, &obj->faces);
  darray_named_group_init(mem_allocator, &obj->groups);
  darray_named_group_init(mem_allocator, &obj->usemtls);
  darray_smooth_group_init(mem_allocator, &obj->smooth_groups);
  darray_mtllib_init(mem_allocator, &obj->mtllibs);

  if(logger) {
    obj->logger = logger;
  } else {
    res = setup_default_logger(obj->allocator, &obj->logger__,
      MSG_PREFIX_INFO, MSG_PREFIX_ERROR, MSG_PREFIX_WARNING);
    if(res != RES_OK) goto error;
    obj->logger = &obj->logger__;
  }

exit:
  if(obj_out)
    *obj_out = obj;
  return res;
error:
  if(obj) {
    AW(obj_ref_put(obj));
    obj = NULL;
  }
  goto exit;
}

res_T
aw_obj_ref_get(struct aw_obj* obj)
{
  if(!obj) return RES_BAD_ARG;
  ref_get(&obj->ref);
  return RES_OK;
}

res_T
aw_obj_ref_put(struct aw_obj* obj)
{
  if(!obj) return RES_BAD_ARG;
  ref_put(&obj->ref, obj_release);
  return RES_OK;
}

res_T
aw_obj_load(struct aw_obj* obj, const char* filename)
{
  FILE* fp = NULL;
  res_T res = RES_OK;

  if(!obj || !filename) {
    res = RES_BAD_ARG;
    goto error;
  }

  fp = fopen(filename, "r");
  if(!fp) {
    log_err(obj, "Error opening `%s'.\n", filename);
    res = RES_IO_ERR;
    goto error;
  }

  res = load_stream(obj, fp, filename);
  if(res != RES_OK) goto error;

exit:
  if(fp) fclose(fp);
  return res;
error:
  goto exit;
}

res_T
aw_obj_load_stream(struct aw_obj* obj, FILE* stream, const char* stream_name)
{
  res_T res = RES_OK;

  if(!obj || !stream) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = load_stream(obj, stream, stream_name ? stream_name : "stream");
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

res_T
aw_obj_clear(struct aw_obj* obj)
{
  if(!obj) return RES_BAD_ARG;
  darray_double_clear(&obj->positions);
  darray_double_clear(&obj->normals);
  darray_double_clear(&obj->texcoords);
  darray_vertex_clear(&obj->vertices);
  darray_face_clear(&obj->faces);
  darray_named_group_clear(&obj->groups);
  darray_named_group_clear(&obj->usemtls);
  darray_smooth_group_clear(&obj->smooth_groups);
  darray_mtllib_clear(&obj->mtllibs);
  obj->igroups_active = 0;
  return RES_OK;
}

res_T
aw_obj_purge(struct aw_obj* obj)
{
  if(!obj) return RES_BAD_ARG;
  darray_double_purge(&obj->positions);
  darray_double_purge(&obj->normals);
  darray_double_purge(&obj->texcoords);
  darray_vertex_purge(&obj->vertices);
  darray_face_purge(&obj->faces);
  darray_named_group_purge(&obj->groups);
  darray_named_group_purge(&obj->usemtls);
  darray_smooth_group_purge(&obj->smooth_groups);
  darray_mtllib_purge(&obj->mtllibs);
  obj->igroups_active = 0;
  return RES_OK;
}

res_T
aw_obj_get_desc(const struct aw_obj* obj, struct aw_obj_desc* desc)
{
  if(!obj || !desc)
    return RES_BAD_ARG;
  desc->faces_count = darray_face_size_get(&obj->faces);
  desc->positions_count = darray_double_size_get(&obj->positions) / 4;
  desc->normals_count = darray_double_size_get(&obj->normals) / 3;
  desc->texcoords_count = darray_double_size_get(&obj->texcoords) / 3;
  desc->groups_count = darray_named_group_size_get(&obj->groups);
  desc->smooth_groups_count = darray_smooth_group_size_get(&obj->smooth_groups);
  desc->usemtls_count = darray_named_group_size_get(&obj->usemtls);
  desc->mtllibs_count = darray_mtllib_size_get(&obj->mtllibs);
  return RES_OK;
}

res_T
aw_obj_get_face
  (const struct aw_obj* obj,
   const size_t iface,
   struct aw_obj_face* face)
{
  if(!obj || !face || iface >= darray_face_size_get(&obj->faces))
    return RES_BAD_ARG;
  *face = darray_face_cdata_get(&obj->faces)[iface];
  return RES_OK;
}

res_T
aw_obj_get_group
  (const struct aw_obj* obj,
   const size_t igroup,
   struct aw_obj_named_group* grp)
{
  const struct named_group* src = NULL;
  if(!obj || !grp || igroup >= darray_named_group_size_get(&obj->groups))
    return RES_BAD_ARG;
  src = darray_named_group_cdata_get(&obj->groups) + igroup;
  grp->name = str_cget(&src->name);
  grp->face_id = src->face_id;
  grp->faces_count = src->faces_count;
  return RES_OK;
}

res_T
aw_obj_get_smooth_group
  (const struct aw_obj* obj,
   const size_t ismooth_group,
   struct aw_obj_smooth_group* group)
{
  if(!obj || !group
  || ismooth_group >= darray_smooth_group_size_get(&obj->smooth_groups))
    return RES_BAD_ARG;
  *group = darray_smooth_group_cdata_get(&obj->smooth_groups)[ismooth_group];
  return RES_OK;
}

res_T
aw_obj_get_mtl
  (const struct aw_obj* obj,
   const size_t imtl,
   struct aw_obj_named_group* mtl)
{
  const struct named_group* src;
  if(!obj || !mtl || imtl >= darray_named_group_size_get(&obj->usemtls))
    return RES_BAD_ARG;
  src = darray_named_group_cdata_get(&obj->usemtls) + imtl;
  mtl->name = str_cget(&src->name);
  mtl->face_id = src->face_id;
  mtl->faces_count = src->faces_count;
  return RES_OK;
}

res_T
aw_obj_get_mtllib
  (const struct aw_obj* obj,
   const size_t imtllib,
   const char** mtllib)
{
  const struct str* str;
  if(!obj || !mtllib || imtllib >= darray_mtllib_size_get(&obj->mtllibs))
    return RES_BAD_ARG;
  str = darray_mtllib_cdata_get(&obj->mtllibs) + imtllib;
  *mtllib = str_cget(str);
  return RES_OK;
}

res_T
aw_obj_get_vertex
  (const struct aw_obj* obj,
   const size_t ivertex,
   struct aw_obj_vertex* vertex)
{
  if(!obj || !vertex || ivertex >= darray_vertex_size_get(&obj->vertices))
    return RES_BAD_ARG;
  *vertex = darray_vertex_cdata_get(&obj->vertices)[ivertex];
  return RES_OK;
}

res_T
aw_obj_get_vertex_data
  (const struct aw_obj* obj,
   const struct aw_obj_vertex* vertex,
   struct aw_obj_vertex_data* vertex_data)
{
  const double* data;
  res_T res = RES_OK;

  if(!obj || !vertex || !vertex_data) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(vertex->position_id >= darray_double_size_get(&obj->positions) / 4) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(vertex->texcoord_id != VERTEX_NULL.texcoord_id
  && vertex->texcoord_id >= darray_double_size_get(&obj->texcoords) / 3) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(vertex->normal_id != VERTEX_NULL.normal_id
  && vertex->normal_id >= darray_double_size_get(&obj->normals) / 3) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Fetch vertex position */
  data = darray_double_cdata_get(&obj->positions) + vertex->position_id * 4;
  vertex_data->position[0] = data[0];
  vertex_data->position[1] = data[1];
  vertex_data->position[2] = data[2];
  vertex_data->position[3] = data[3];

  /* Setup vertex texcoord */
  if(vertex->texcoord_id == VERTEX_NULL.texcoord_id) {
    vertex_data->texcoord[0] = 0;
    vertex_data->texcoord[1] = 0;
    vertex_data->texcoord[2] = 0;
  } else {
    data = darray_double_cdata_get(&obj->texcoords) + vertex->texcoord_id * 3;
    vertex_data->texcoord[0] = data[0];
    vertex_data->texcoord[1] = data[1];
    vertex_data->texcoord[2] = data[2];
  }
  /* Setup vertex normal */
  if(vertex->normal_id == VERTEX_NULL.normal_id) {
    vertex_data->normal[0] = 0;
    vertex_data->normal[1] = 0;
    vertex_data->normal[2] = 0;
  } else {
    data = darray_double_cdata_get(&obj->normals) + vertex->normal_id * 3;
    vertex_data->normal[0] = data[0];
    vertex_data->normal[1] = data[1];
    vertex_data->normal[2] = data[2];
  }

exit:
  return res;
error:
  goto exit;
}

res_T
aw_obj_get_positions(const struct aw_obj* obj, const double** positions)
{
  if(!obj || !positions) return RES_BAD_ARG;
  *positions = darray_double_cdata_get(&obj->positions);
  return RES_OK;
}

res_T
aw_obj_get_texcoords(const struct aw_obj* obj, const double** texcoords)
{
  if(!obj || !texcoords) return RES_BAD_ARG;
  *texcoords = darray_double_cdata_get(&obj->texcoords);
  return RES_OK;
}

res_T
aw_obj_get_normals(const struct aw_obj* obj, const double** normals)
{
  if(!obj || !normals) return RES_BAD_ARG;
  *normals = darray_double_cdata_get(&obj->normals);
  return RES_OK;
}

#ifdef COMPILER_CL
  #pragma warning(pop)
#endif

