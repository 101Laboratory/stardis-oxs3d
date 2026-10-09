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

#ifndef AW_H
#define AW_H

#include <rsys/rsys.h>
#include <rsys/str.h>

#ifdef AW_SHARED_BUILD
  #define AW_API extern EXPORT_SYM
#elif defined(AW_STATIC_BUILD)
  #define AW_API extern LOCAL_SYM
#else
  #define AW_API extern IMPORT_SYM
#endif

#ifndef NDEBUG
  #define AW(Func) ASSERT(aw_##Func == RES_OK)
#else
  #define AW(Func) aw_##Func
#endif

#define AW_ID_NONE ((size_t)(-1))

enum aw_color_space {
  AW_COLOR_RGB,
  AW_COLOR_XYZ
};

enum aw_map_flag {
  AW_MAP_BLEND_U = BIT(0),
  AW_MAP_BLEND_V = BIT(1),
  AW_MAP_COLOR_CORRECTION = BIT(2),
  AW_MAP_CLAMP = BIT(3)
};

enum aw_map_channel {
  AW_MAP_CHANNEL_RED,
  AW_MAP_CHANNEL_GREEN,
  AW_MAP_CHANNEL_BLUE,
  AW_MAP_CHANNEL_MATTE,
  AW_MAP_CHANNEL_LUMINANCE,
  AW_MAP_CHANNEL_DEPTH
};

struct aw_obj_desc {
  size_t faces_count;
  size_t positions_count;
  size_t normals_count;
  size_t texcoords_count;
  size_t groups_count;
  size_t smooth_groups_count;
  size_t usemtls_count;
  size_t mtllibs_count;
};

struct aw_obj_face {
  size_t vertex_id; /* Index of the first face vertex */
  size_t vertices_count;
  size_t group_id; /* Index of the face group */
  size_t smooth_group_id; /* Index of the face smooth group */
  size_t mtl_id; /* Index of the face material */
};

struct aw_obj_named_group {
  const char* name;
  size_t face_id;
  size_t faces_count;
};

struct aw_obj_smooth_group {
  size_t face_id; /* Index of the first smooth group face */
  size_t faces_count;
  char is_smoothed;
};

struct aw_obj_vertex {
  size_t position_id;
  size_t texcoord_id;
  size_t normal_id;
};

struct aw_obj_vertex_data {
  double position[4];
  double normal[3];
  double texcoord[3];
};

struct aw_color {
  double value[3];
  enum aw_color_space color_space;
};

struct aw_map {
  const char* filename; /* NULL <=> Not defined */
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

struct aw_material {
  const char* name;
  struct aw_color ambient;
  struct aw_color diffuse;
  struct aw_color specular;
  struct aw_color transmission;
  double specular_exponent;
  double refraction_index;
  size_t illumination_model; /* In [0, 10] */
  struct aw_map ambient_map;
  struct aw_map diffuse_map;
  struct aw_map specular_map;
  struct aw_map specular_exponent_map; /* Scalar texture */
  struct aw_map bump_map; /* Scalar texture with valid bump multiplier */
};

struct aw_obj;
struct aw_mtl;
struct logger;
struct mem_allocator;

BEGIN_DECLS

/*******************************************************************************
 * Obj functions
 ******************************************************************************/
AW_API res_T
aw_obj_create
  (struct logger* logger, /* NULL <=> use default logger*/
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   struct aw_obj** obj);

AW_API res_T
aw_obj_ref_get
  (struct aw_obj* obj);

AW_API res_T
aw_obj_ref_put
  (struct aw_obj* obj);

AW_API res_T
aw_obj_load
  (struct aw_obj* obj,
   const char* filename);

AW_API res_T
aw_obj_load_stream
  (struct aw_obj* obj,
   FILE* stream,
   const char* stream_name); /* May be NULL <=> default name, i.e. "stream" */

AW_API res_T
aw_obj_clear
  (struct aw_obj* obj);

/* Clear and release the internal memory */
AW_API res_T
aw_obj_purge
  (struct aw_obj* obj);

AW_API res_T
aw_obj_get_desc
  (const struct aw_obj* obj,
   struct aw_obj_desc* desc);

AW_API res_T
aw_obj_get_face
  (const struct aw_obj* obj,
   const size_t face_id,
   struct aw_obj_face* face);

AW_API res_T
aw_obj_get_group
  (const struct aw_obj* obj,
   const size_t group_id,
   struct aw_obj_named_group* group);

AW_API res_T
aw_obj_get_smooth_group
  (const struct aw_obj* obj,
   const size_t smooth_group_id,
   struct aw_obj_smooth_group* smooth_group);

AW_API res_T
aw_obj_get_mtl
  (const struct aw_obj* obj,
   const size_t mtl_id,
   struct aw_obj_named_group* mtl);

AW_API res_T
aw_obj_get_mtllib
  (const struct aw_obj* obj,
   const size_t mtllib_id,
   const char** mtllib);

AW_API res_T
aw_obj_get_vertex
  (const struct aw_obj* obj,
   const size_t vertex_id,
   struct aw_obj_vertex* vertex);

AW_API res_T
aw_obj_get_vertex_data
  (const struct aw_obj* obj,
   const struct aw_obj_vertex* vertex,
   struct aw_obj_vertex_data* vertex_data);

AW_API res_T
aw_obj_get_positions
  (const struct aw_obj* obj,
   const double** positions); /* List of double 4 */

AW_API res_T
aw_obj_get_texcoords
  (const struct aw_obj* obj,
   const double** texcoords); /* List of double 3 */

AW_API res_T
aw_obj_get_normals
  (const struct aw_obj* obj,
   const double** normals); /* List of double 3 */

/*******************************************************************************
 * Mtl functions
 ******************************************************************************/
AW_API res_T
aw_mtl_create
  (struct logger* logger, /* NULL <=> use default logger */
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   struct aw_mtl** mtl);

AW_API res_T
aw_mtl_ref_get
  (struct aw_mtl* mtl);

AW_API res_T
aw_mtl_ref_put
  (struct aw_mtl* mtl);

AW_API res_T
aw_mtl_load
  (struct aw_mtl* mtl,
   const char* filename);

AW_API res_T
aw_mtl_load_stream
  (struct aw_mtl* mtl,
   FILE* stream,
   const char* stream_name); /* May be NULL <=> default name, i.e. "stream" */

AW_API res_T
aw_mtl_clear
  (struct aw_mtl* mtl);

/* Clear and release the internal memory */
AW_API res_T
aw_mtl_purge
  (struct aw_mtl* mtl);

AW_API res_T
aw_mtl_get_materials_count
  (struct aw_mtl* mtl,
   size_t* materials_count);

/* The filled material must be released by the aw_material_release function */
AW_API res_T
aw_mtl_get_material
  (struct aw_mtl* mtl,
   const size_t imaterial,
   struct aw_material* material);

END_DECLS

#endif /* AW_H */

