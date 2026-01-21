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
#include <rsys/double3.h>
#include <rsys/logger.h>
#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>
#include <rsys/str.h>
#include <rsys/text_reader.h>

#include <float.h>

#ifdef COMPILER_CL
  #pragma warning(push)
  #pragma warning(disable:4706) /* Assignment within a condition */
#endif

static const char* MSG_PREFIX_INFO = "load-mtl:info: ";
static const char* MSG_PREFIX_ERROR = "load-mtl:error: ";
static const char* MSG_PREFIX_WARNING = "load-mtl:warning: ";

enum map_type {
  MAP_COMMON = BIT(0),
  MAP_SCALAR = BIT(1),
  MAP_BUMP = MAP_SCALAR | BIT(2)
};

/*******************************************************************************
 * Map data structure
 ******************************************************************************/
struct map {
  struct str filename; /* str_len == 0 <=> Not defined */
  int options_mask;
  double image_bias; /* Scalar to add to the image pixels */
  double image_scale; /* Scalar to multiply to the image pixels */
  double texcoord_bias[3];
  double texcoord_scale[3];
  double texcoord_turbulence[3];
  size_t resolution; /* image size = resolution x resolution */
  enum aw_map_channel scalar; /* Channel used to create a scalar texture */
  double bump_multiplier; /* Only available on bump maps */
};

static INLINE void
map_init(struct mem_allocator* allocator, struct map* map)
{
  ASSERT(map);
  str_init(allocator, &map->filename);
  map->options_mask = 0;
  map->image_bias = 0.f;
  map->image_scale = 1.f;
  d3_splat(map->texcoord_bias, 0.f);
  d3_splat(map->texcoord_scale, 1.f);
  d3_splat(map->texcoord_turbulence, 0.f);
  map->resolution = 0;
  map->scalar = AW_MAP_CHANNEL_LUMINANCE;
  map->bump_multiplier = 1.0f;
}

static INLINE void
map_release(struct map* map)
{
  ASSERT(map);
  str_release(&map->filename);
}

static INLINE void
map_copy_pod(struct map* dst, const struct map* src)
{
  ASSERT(dst && src);
  dst->options_mask = src->options_mask;
  dst->image_bias = src->image_bias;
  dst->image_scale = src->image_scale;
  d3_set(dst->texcoord_bias, src->texcoord_bias);
  d3_set(dst->texcoord_scale, src->texcoord_scale);
  d3_set(dst->texcoord_turbulence, src->texcoord_turbulence);
  dst->resolution = src->resolution;
  dst->scalar = src->scalar;
  dst->bump_multiplier = src->bump_multiplier;
}

static INLINE res_T
map_copy(struct map* dst, const struct map* src)
{
  map_copy_pod(dst, src);
  return str_copy(&dst->filename, &src->filename);
}

static INLINE res_T
map_copy_and_release(struct map* dst, struct map* src)
{
  map_copy_pod(dst, src);
  return str_copy_and_release(&dst->filename, &src->filename);
}

static INLINE void
map_to_aw_map(const struct map* map, struct aw_map* aw_map)
{
  ASSERT(map && aw_map);
  aw_map->filename = str_is_empty(&map->filename) ? NULL : str_cget(&map->filename);
  aw_map->options_mask = map->options_mask;
  aw_map->image_bias = map->image_bias;
  aw_map->image_scale = map->image_scale;
  d3_set(aw_map->texcoord_bias, map->texcoord_bias);
  d3_set(aw_map->texcoord_scale, map->texcoord_scale);
  d3_set(aw_map->texcoord_turbulence, map->texcoord_turbulence);
  aw_map->resolution = map->resolution;
  aw_map->scalar = map->scalar;
  aw_map->bump_multiplier = map->bump_multiplier;
}

/*******************************************************************************
 * Material API
 ******************************************************************************/
struct material {
  struct str name;
  struct aw_color ambient;
  struct aw_color diffuse;
  struct aw_color specular;
  struct aw_color transmission;
  double specular_exponent;
  double refraction_index;
  size_t illumination_model; /* In [0, 10] */
  struct map ambient_map;
  struct map diffuse_map;
  struct map specular_map;
  struct map specular_exponent_map; /* Scalar texture */
  struct map bump_map; /* Scalar texture with valid bump multiplier */
};

static INLINE void
material_init(struct mem_allocator* allocator, struct material* mtl)
{
  ASSERT(mtl);
  memset(mtl, 0, sizeof(struct aw_material));
  str_init(allocator, &mtl->name);
  map_init(allocator, &mtl->ambient_map);
  map_init(allocator, &mtl->diffuse_map);
  map_init(allocator, &mtl->specular_map);
  map_init(allocator, &mtl->specular_exponent_map);
  map_init(allocator, &mtl->bump_map);
}

static INLINE void
material_release(struct material* mtl)
{
  ASSERT(mtl);
  str_release(&mtl->name);
  map_release(&mtl->ambient_map);
  map_release(&mtl->diffuse_map);
  map_release(&mtl->specular_map);
  map_release(&mtl->specular_exponent_map);
  map_release(&mtl->bump_map);
}

static INLINE void
material_copy_pod(struct material* dst, const struct material* src)
{
  ASSERT(dst && src);
  dst->ambient = src->ambient;
  dst->diffuse = src->diffuse;
  dst->specular = src->specular;
  dst->transmission = src->transmission;
  dst->specular_exponent = src->specular_exponent;
  dst->refraction_index = src->refraction_index;
  dst->illumination_model = src->illumination_model;
}

static INLINE res_T
material_copy(struct material* dst, const struct material* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  material_copy_pod(dst, src);
  #define CALL(Func) if(RES_OK != (res = Func)) return res
  CALL(str_copy(&dst->name, &src->name));
  CALL(map_copy(&dst->ambient_map, &src->ambient_map));
  CALL(map_copy(&dst->diffuse_map, &src->diffuse_map));
  CALL(map_copy(&dst->specular_map, &src->specular_map));
  CALL(map_copy(&dst->specular_exponent_map, &src->specular_exponent_map));
  CALL(map_copy(&dst->bump_map, &src->bump_map));
  #undef CALL
  return RES_OK;
}

static INLINE res_T
material_copy_and_release(struct material* dst, struct material* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  material_copy_pod(dst, src);
  #define CALL(Func) if(RES_OK != (res = Func)) return res
  CALL(str_copy_and_release(&dst->name, &src->name));
  CALL(map_copy_and_release(&dst->ambient_map, &src->ambient_map));
  CALL(map_copy_and_release(&dst->diffuse_map, &src->diffuse_map));
  CALL(map_copy_and_release(&dst->specular_map, &src->specular_map));
  CALL(map_copy_and_release
    (&dst->specular_exponent_map, &src->specular_exponent_map));
  CALL(map_copy_and_release(&dst->bump_map, &src->bump_map));
  #undef CALL
  return RES_OK;
}

/* Generate the darray_mtl data structure */
#define DARRAY_NAME material
#define DARRAY_DATA struct material
#define DARRAY_FUNCTOR_INIT material_init
#define DARRAY_FUNCTOR_RELEASE material_release
#define DARRAY_FUNCTOR_COPY material_copy
#define DARRAY_FUNCTOR_COPY_AND_RELEASE material_copy_and_release
#include <rsys/dynamic_array.h>

/*******************************************************************************
 * Definition of the aw_mtl opaque data structure
 ******************************************************************************/
struct aw_mtl {
  struct darray_material materials;
  struct material* newmtl; /* Pointer toward the current material */

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
  (const struct aw_mtl* mtl,
   const enum log_type stream,
   const char* msg,
   va_list vargs)
{
  ASSERT(mtl && msg);
  if(mtl->verbose) {
    res_T res; (void)res;
    res = logger_vprint(mtl->logger, stream, msg, vargs);
    ASSERT(res == RES_OK);
  }
}

static INLINE void
log_err(const struct aw_mtl* mtl, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(mtl && msg);

  va_start(vargs_list, msg);
  log_msg(mtl, LOG_ERROR, msg, vargs_list);
  va_end(vargs_list);
}

static INLINE void
log_warn(const struct aw_mtl* mtl, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(mtl && msg);

  va_start(vargs_list, msg);
  log_msg(mtl, LOG_WARNING, msg, vargs_list);
  va_end(vargs_list);
}

static res_T
parse_newmtl(struct aw_mtl* mtl, char** tk_ctx)
{
  struct material* newmtl = NULL;
  char* tk = NULL;
  size_t nmtls;
  res_T res = RES_OK;
  ASSERT(mtl && tk_ctx);

  tk = strtok_r(NULL, " \t", tk_ctx); /* Blanks are prohibited in mtl name */
  if(!tk) {
    res = RES_BAD_ARG;
    goto error;
  }

  nmtls = darray_material_size_get(&mtl->materials);

  /* Allocate the new material */
  res = darray_material_resize(&mtl->materials, nmtls + 1);
  if(res != RES_OK) goto error;

  /* Fetch the new material */
  newmtl = darray_material_data_get(&mtl->materials) + nmtls;

  /* Setup the material name */
  res = str_set(&newmtl->name, tk);
  if(res != RES_OK) goto error;

  /* Set the new material as the material to parse */
  mtl->newmtl = newmtl;

exit:
  return RES_OK;
error:
  if(newmtl) darray_material_pop_back(&mtl->materials);
  goto exit;
}

static res_T
parse_color(struct aw_mtl* mtl, struct aw_color* col, char** tk_ctx)
{
  char* tk = NULL;
  res_T res = RES_OK;
  ASSERT(mtl && col && tk_ctx);

  tk = strtok_r(NULL, " \t", tk_ctx);
  if(!tk) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(!strcmp(tk, "spectral")) {
    log_err(mtl, "spectral colors are not supported\n");
    res = RES_BAD_ARG;
    goto error;
  }

  if(strcmp(tk, "xyz")) {
    col->color_space = AW_COLOR_RGB;
  } else {
    col->color_space = AW_COLOR_XYZ;
    tk = strtok_r(NULL, " \t", tk_ctx);
    if(!tk) {
      res = RES_BAD_ARG;
      goto error;
    }
  }

  res = cstr_to_double(tk, &col->value[0]);
  if(res != RES_OK) goto error;

  /* If only the first component is defined the second and third components are
   * assumed to be equal to the first one */
  tk = strtok_r(NULL, " \t", tk_ctx);
  if(!tk) {
    col->value[1] = col->value[0];
    col->value[2] = col->value[0];
  } else {
    res = cstr_to_double(tk, &col->value[1]);
    if(res != RES_OK) goto error;

    tk = strtok_r(NULL, " \t", tk_ctx);
    if(!tk) { res = RES_BAD_ARG; goto error; }

    res = cstr_to_double(tk, &col->value[2]);
    if(res == RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

static INLINE res_T
parse_size_t
  (size_t* s, const size_t range_min, const size_t range_max, char** tk_ctx)
{
  unsigned long ul;
  char* tk = NULL;
  res_T res = RES_OK;
  ASSERT(s && tk_ctx && range_min < range_max);

  tk = strtok_r(NULL, "\n", tk_ctx);
  if(!tk) return res;

  res = cstr_to_ulong(tk, &ul);
  if(res != RES_OK) return res;
  if((unsigned long)range_min > ul || (unsigned long)range_max < ul) return res;
  *s = ul;
  return RES_OK;
}

static res_T
parse_bool_option
  (int* options_mask,
   const enum aw_map_flag option,
   char** tk_ctx)
{
  char* tk;
  ASSERT(options_mask && tk_ctx);

  tk = strtok_r(NULL, " \t", tk_ctx);
  if(!tk) return RES_BAD_ARG;

  if(!strcmp(tk, "on")) {
    *options_mask |= (int)option;
  } else if(!strcmp(tk, "off")) {
    *options_mask &=  ~((int)option);
  } else {
    return RES_BAD_ARG;
  }
  return RES_OK;
}

static FINLINE res_T
parse_imfchan_option(enum aw_map_channel* channel, char** tk_ctx)
{
  char* tk = NULL;
  ASSERT(channel && tk_ctx);

  tk = strtok_r(NULL, " \t", tk_ctx);
  if(!tk) return RES_BAD_ARG;

  if(!strcmp(tk, "r")) {
    *channel = AW_MAP_CHANNEL_RED;
  } else if(!strcmp(tk, "g")) {
    *channel = AW_MAP_CHANNEL_GREEN;
  } else if(!strcmp(tk, "b")) {
    *channel = AW_MAP_CHANNEL_BLUE;
  } else if(!strcmp(tk, "m")) {
    *channel = AW_MAP_CHANNEL_MATTE;
  } else if(!strcmp(tk, "l")) {
    *channel = AW_MAP_CHANNEL_LUMINANCE;
  } else if(!strcmp(tk, "z")) {
    *channel = AW_MAP_CHANNEL_DEPTH;
  } else {
    return RES_BAD_ARG;
  }
  return RES_OK;
}

static res_T
parse_map(struct map* map, const enum map_type type, char** tk_ctx)
{
  char* tk = NULL;
  res_T res = RES_OK;
  ASSERT(map && tk_ctx);

  for(;;) {
    tk = strtok_r(NULL, " \t", tk_ctx);
    if(!tk) {
      res = RES_BAD_ARG;
      goto error;
    }

    if(!strcmp(tk, "-blendu")) {
      res = parse_bool_option(&map->options_mask, AW_MAP_BLEND_U, tk_ctx);
      if(res != RES_OK) goto error;
    } else if(!strcmp(tk, "-blendv")) {
      res = parse_bool_option(&map->options_mask, AW_MAP_BLEND_V, tk_ctx);
      if(res != RES_OK) goto error;
    } else if(!strcmp(tk, "-cc")) {
      res = parse_bool_option
        (&map->options_mask, AW_MAP_COLOR_CORRECTION, tk_ctx);
      if(res != RES_OK) goto error;
    } else if(!strcmp(tk, "-clamp")) {
      res = parse_bool_option(&map->options_mask, AW_MAP_CLAMP, tk_ctx);
      if(res != RES_OK) goto error;
    } else if(!strcmp(tk, "-imfchan") && (type & MAP_SCALAR)) {
      res = parse_imfchan_option(&map->scalar, tk_ctx);
      if(res != RES_OK) goto error;
    } else if(!strcmp(tk, "-mm")) { /* Image bias and scale */
      res = parse_doubleX
        (&map->image_bias, 1, 1, -DBL_MAX, DBL_MAX, 0.f, tk_ctx);
      if(res != RES_OK) goto error;
      res = parse_doubleX
        (&map->image_scale, 1, 1, -DBL_MAX, DBL_MAX, 0.f, tk_ctx);
      if(res != RES_OK) goto error;
    } else if(!strcmp(tk, "-o")) { /* Texcoord offset */
      res = parse_doubleX
        (map->texcoord_bias, 1, 3, -DBL_MAX, DBL_MAX, 0.f, tk_ctx);
      if(res != RES_OK) goto error;
    } else if(!strcmp(tk, "-s")) { /* Texcoord scale */
      res = parse_doubleX
        (map->texcoord_scale, 1, 3, -DBL_MAX, DBL_MAX, 1.f, tk_ctx);
      if(res != RES_OK) goto error;
    } else if(!strcmp(tk, "-t")) { /* Texcoord turbulence */
      res = parse_doubleX
        (map->texcoord_turbulence, 1, 3, -DBL_MAX, DBL_MAX, 0.f, tk_ctx);
      if(res != RES_OK) goto error;
    } else if(!strcmp(tk, "-texres")) { /* Texture resolution */
      res = parse_size_t(&map->resolution, 1, SIZE_MAX, tk_ctx);
      if(res != RES_OK) goto error;
    } else if(!strcmp(tk, "-bm") && (type == MAP_BUMP)) {/* Bump multiplier */
      res = parse_doubleX
        (&map->bump_multiplier, 1, 1, -DBL_MAX, DBL_MAX, 0.f, tk_ctx);
      if(res != RES_OK) goto error;
    } else { /* Map filename */
      res = str_set(&map->filename, tk);
      if(res != RES_OK) goto error;
      tk = strtok_r(NULL, "", tk_ctx);
      if(tk) {
        res = str_append_char(&map->filename, ' ');
        if(res != RES_OK) goto error;
        res = str_append(&map->filename, strtok_r(NULL, "", tk_ctx));
        if(res != RES_OK) goto error;
      }
      break; /* The map is fully parsed */
    }
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
parse_mtl_line(struct aw_mtl* mtl, struct txtrdr* txtrdr)
{
  char* tk = NULL;
  char* tk_ctx = NULL;
  res_T res = RES_OK;
  ASSERT(mtl && txtrdr);

  tk = strtok_r(txtrdr_get_line(txtrdr), " \t", &tk_ctx);
  ASSERT(tk); /* A line should exist since it was parsed by txtrdr */

  if(!strcmp(tk, "newmtl")) { /* Material name declaration */
    res = parse_newmtl(mtl, &tk_ctx);
  } else if(mtl->newmtl == NULL) {
    res = RES_BAD_ARG;
  } else if(!strcmp(tk, "Ka")) { /* Ambient reflectivity */
    res = parse_color(mtl, &mtl->newmtl->ambient, &tk_ctx);
  } else if(!strcmp(tk, "Kd")) { /* Diffuse reflectivity */
    res = parse_color(mtl, &mtl->newmtl->diffuse, &tk_ctx);
  } else if(!strcmp(tk, "Ks")) { /* Specular reflectivity */
    res = parse_color(mtl, &mtl->newmtl->specular, &tk_ctx);
  } else if(!strcmp(tk, "Tf")) { /* Transimission filter */
    res = parse_color(mtl, &mtl->newmtl->transmission, &tk_ctx);
  } else if(!strcmp(tk, "Ns")) { /* Specular exponent */
    res = parse_doubleX
      (&mtl->newmtl->specular_exponent, 1, 1, 0, DBL_MAX, 0, &tk_ctx);
  } else if(!strcmp(tk, "Ni")) { /* Refraction index */
    res = parse_doubleX
      (&mtl->newmtl->refraction_index, 1, 1, 0.001, 10, 0.001, &tk_ctx);
  } else if(!strcmp(tk, "illum")) { /* Illumination model */
    res = parse_size_t(&mtl->newmtl->illumination_model, 0, 10, &tk_ctx);
  } else if(!strcmp(tk, "map_Ka")) { /* Ambient texture */
    res = parse_map(&mtl->newmtl->ambient_map, MAP_COMMON, &tk_ctx);
  } else if(!strcmp(tk, "map_Kd")) { /* Diffuse texture */
    res = parse_map(&mtl->newmtl->diffuse_map, MAP_COMMON, &tk_ctx);
  } else if(!strcmp(tk, "map_Ks")) { /* Specular texture */
    res = parse_map(&mtl->newmtl->specular_map, MAP_COMMON, &tk_ctx);
  } else if(!strcmp(tk, "map_Ns")) { /* Specular exponent texture */
    res = parse_map
      (&mtl->newmtl->specular_exponent_map, MAP_SCALAR, &tk_ctx);
  } else if(!strcmp(tk, "bump") || !strcmp(tk, "map_bump")) { /* Bump map */
    res = parse_map(&mtl->newmtl->bump_map, MAP_BUMP, &tk_ctx);
  } else {
    log_warn(mtl,
      "%s:%lu: warning: ignored or malformed directive `%s'\n",
      txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr), tk);
    strtok_r(NULL, "", &tk_ctx); /* Discard remaining text */
  }
  if(res != RES_OK) {
    log_err(mtl, "%s:%lu: parsing failed.\n",
      txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr), tk);
    goto error;
  }

  tk = strtok_r(NULL, "", &tk_ctx);
  if(tk) {
    log_err(mtl, "%s:%lu: unexpected text `%s'.\n",
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
load_stream(struct aw_mtl* mtl, FILE* stream, const char* stream_name)
{
  struct txtrdr* txtrdr = NULL;
  res_T res = RES_OK;
  ASSERT(mtl && stream && stream_name);

  res = txtrdr_stream(mtl->allocator, stream, stream_name, '#', &txtrdr);
  if(res != RES_OK) {
    log_err(mtl, "Could not create the text reader.\n");
    goto error;
  }

  for(;;) {
    res = txtrdr_read_line(txtrdr);
    if(res != RES_OK) {
      log_err(mtl, "%s: could not read the line `%lu'.\n",
        txtrdr_get_name(txtrdr), (unsigned long)txtrdr_get_line_num(txtrdr));
      goto error;
    }

    if(!txtrdr_get_cline(txtrdr)) break; /* No more parsed line */

    res = parse_mtl_line(mtl, txtrdr);
    if(res != RES_OK) goto error;
  }

exit:
  if(txtrdr) txtrdr_ref_put(txtrdr);
  return res;
error:
  AW(mtl_clear(mtl));
  goto exit;
}

static void
mtl_release(ref_T* ref)
{
  struct aw_mtl* mtl = CONTAINER_OF(ref, struct aw_mtl, ref);
  ASSERT(ref);
  darray_material_release(&mtl->materials);
  if(mtl->logger == &mtl->logger__) logger_release(&mtl->logger__);
  MEM_RM(mtl->allocator, mtl);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
aw_mtl_create
  (struct logger* logger,
   struct mem_allocator* mem_allocator,
   const int verbose,
   struct aw_mtl** mtl_out)
{
  struct mem_allocator* allocator;
  struct aw_mtl* mtl = NULL;
  res_T res = RES_OK;

  if(!mtl_out) {
    res = RES_BAD_ARG;
    goto error;
  }
  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  mtl = MEM_CALLOC(allocator, 1, sizeof(struct aw_mtl));
  if(!mtl) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&mtl->ref);
  mtl->allocator = allocator;
  mtl->verbose = verbose;
  darray_material_init(allocator, &mtl->materials);

  if(logger) {
    mtl->logger = logger;
  } else {
    res = setup_default_logger(mtl->allocator, &mtl->logger__,
      MSG_PREFIX_INFO, MSG_PREFIX_ERROR, MSG_PREFIX_WARNING);
    if(res != RES_OK) goto error;
    mtl->logger = &mtl->logger__;
  }

exit:
  if(mtl_out) *mtl_out = mtl;
  return res;
error:
  if(mtl) {
    AW(mtl_ref_put(mtl));
    mtl = NULL;
  }
  goto exit;
}

res_T
aw_mtl_ref_get(struct aw_mtl* mtl)
{
  if(!mtl) return RES_BAD_ARG;
  ref_get(&mtl->ref);
  return RES_OK;
}

res_T
aw_mtl_ref_put(struct aw_mtl* mtl)
{
  if(!mtl) return RES_BAD_ARG;
  ref_put(&mtl->ref, mtl_release);
  return RES_OK;
}

res_T
aw_mtl_load(struct aw_mtl* mtl, const char* filename)
{
  FILE* file = NULL;
  res_T res = RES_OK;

  if(!mtl || !filename) {
    res = RES_BAD_ARG;
    goto error;
  }

  file = fopen(filename, "r");
  if(!file) {
    log_err(mtl, "Error opening `%s'\n", filename);
    res = RES_IO_ERR;
    goto error;
  }

  res = load_stream(mtl, file, filename);
  if(res != RES_OK) goto error;

exit:
  if(file) fclose(file);
  return res;
error:
  goto exit;
}

res_T
aw_mtl_load_stream(struct aw_mtl* mtl, FILE* stream, const char* stream_name)
{
  res_T res = RES_OK;

  if(!mtl || !stream) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = load_stream(mtl, stream, stream_name ? stream_name : "stream");
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

res_T
aw_mtl_clear(struct aw_mtl* mtl)
{
  if(!mtl) return RES_BAD_ARG;
  darray_material_clear(&mtl->materials);
  mtl->newmtl = NULL;
  return RES_OK;
}

res_T
aw_mtl_purge(struct aw_mtl* mtl)
{
  if(!mtl) return RES_BAD_ARG;
  darray_material_purge(&mtl->materials);
  mtl->newmtl = NULL;
  return RES_OK;
}

res_T
aw_mtl_get_materials_count(struct aw_mtl* mtl, size_t* nmtls)
{
  if(!mtl || !nmtls)
    return RES_BAD_ARG;
  *nmtls = darray_material_size_get(&mtl->materials);
  return RES_OK;
}

res_T
aw_mtl_get_material
  (struct aw_mtl* mtl,
   const size_t imaterial,
   struct aw_material* material)
{
  const struct material* mat = NULL;

  if(!mtl || !material || imaterial>=darray_material_size_get(&mtl->materials))
    return RES_BAD_ARG;

  mat = darray_material_cdata_get(&mtl->materials) + imaterial;
  material->name = str_cget(&mat->name);
  material->ambient = mat->ambient;
  material->diffuse = mat->diffuse;
  material->specular = mat->specular;
  material->transmission = mat->transmission;
  material->specular_exponent = mat->specular_exponent;
  material->refraction_index = mat->refraction_index;
  material->illumination_model = mat->illumination_model;
  map_to_aw_map(&mat->ambient_map, &material->ambient_map);
  map_to_aw_map(&mat->diffuse_map, &material->diffuse_map);
  map_to_aw_map(&mat->specular_map, &material->specular_map);
  map_to_aw_map(&mat->specular_exponent_map, &material->specular_exponent_map);
  map_to_aw_map(&mat->bump_map, &material->bump_map);
  return RES_OK;
}

#ifdef COMPILER_CL
  #pragma warning(pop)
#endif

