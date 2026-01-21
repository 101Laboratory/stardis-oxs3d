/* Copyright (C) 2019, 2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SG3D_GEOMETRY_H__
#define SG3D_GEOMETRY_H__

#include "sg3d.h"
#include "sg3d_misc.h"

#include <rsys/ref_count.h>
#include <rsys/dynamic_array.h>
#include <rsys/dynamic_array_uint.h>
#include <rsys/hash_table.h>

/* Forward declaration of external opaque data types */

/******************************************************************************
 * A type to store triangles
 *****************************************************************************/
struct triangle {
  vrtx_id_t vertex_ids[3];
  /* FRONT/BACK/INTERFACE property */
  prop_id_t properties[SG3D_PROP_TYPES_COUNT__];
  /* ID of the triangle in user world, i.e. without deduplication */
  trg_id_t user_id;
};
#define TRG_UNDEF__ {\
  { SG3D_UNSPECIFIED_PROPERTY, SG3D_UNSPECIFIED_PROPERTY, SG3D_UNSPECIFIED_PROPERTY },\
  { SG3D_UNSPECIFIED_PROPERTY, SG3D_UNSPECIFIED_PROPERTY, SG3D_UNSPECIFIED_PROPERTY },\
  SG3D_UNSPECIFIED_PROPERTY\
}
#define DARRAY_NAME triangle
#define DARRAY_DATA struct triangle
#include <rsys/dynamic_array.h>

/******************************************************************************
 * A type to store vertices
 *****************************************************************************/
struct vertex {
  double coord[3];
};
#define DARRAY_NAME vertex
#define DARRAY_DATA struct vertex
#include <rsys/dynamic_array.h>

/******************************************************************************
 * A type to map triangle vertices to IDs in unique_triangles
 *****************************************************************************/
struct vrtx_id3 { vrtx_id_t x[3]; };

static FINLINE int
trg_key_eq(const struct vrtx_id3* k1, const struct vrtx_id3* k2)
{
  ASSERT(k1 && k2);
  ASSERT(k1->x[0] < k1->x[1] && k1->x[1] < k1->x[2]);
  ASSERT(k2->x[0] < k2->x[1] && k2->x[1] < k2->x[2]);
  return (k1->x[0] == k2->x[0])
    && (k1->x[1] == k2->x[1])
    && (k1->x[2] == k2->x[2]);
}

#define HTABLE_NAME trg
#define HTABLE_KEY struct vrtx_id3
#define HTABLE_DATA trg_id_t
#define HTABLE_KEY_FUNCTOR_EQ trg_key_eq
#include <rsys/hash_table.h>

/******************************************************************************
 * A type to map vertex coordinates to IDs in unique_vertices
 *****************************************************************************/
static FINLINE int
vrtx_eq(const struct vertex* v1, const struct vertex* v2)
{
  int i;
  ASSERT(v1 && v2);
  FOR_EACH(i, 0, 3) if(v1->coord[i] != v2->coord[i]) return 0;
  return 1;
}

#define HTABLE_NAME vrtx
#define HTABLE_KEY struct vertex
#define HTABLE_DATA vrtx_id_t
#define HTABLE_KEY_FUNCTOR_EQ vrtx_eq
#include <rsys/hash_table.h>

/******************************************************************************
 * Types to record sources and values of triangle descriptions.
 *****************************************************************************/

/* A type to store a value and the files defining this value
 * (usualy a single file) */
struct definition {
  /* The value */
  prop_id_t property_value;
  /* The IDs of the geometry sets that defined the value */
  struct darray_uint set_ids;
};

static FINLINE void
init_definition
  (struct mem_allocator* alloc,
   struct definition* data)
{
  ASSERT(alloc && data);
  data->property_value = SG3D_UNSPECIFIED_PROPERTY;
  darray_uint_init(alloc, &data->set_ids);
}

static INLINE res_T
copy_definition
  (struct definition* dst,
   const struct definition* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  dst->property_value = src->property_value;
  ERR(darray_uint_copy(&dst->set_ids, &src->set_ids));
exit:
  return res;
error:
  goto exit;
}

static FINLINE void
release_definition
  (struct definition* data)
{
  ASSERT(data);
  darray_uint_release(&data->set_ids);
}

#define DARRAY_NAME definition
#define DARRAY_DATA struct definition
#define DARRAY_FUNCTOR_INIT init_definition
#define DARRAY_FUNCTOR_COPY copy_definition
#define DARRAY_FUNCTOR_RELEASE release_definition
#include <rsys/dynamic_array.h>

/* A type to accumulate information for a triangle.
 * If there is more than 1 definition / field, it is a conflict */
struct trg_descriptions {
  struct darray_definition defs[SG3D_PROP_TYPES_COUNT__];
  int merge_conflict;
  int properties_conflict;
  char defs_include_unspecified;
  char property_defined[SG3D_PROP_TYPES_COUNT__];
};

static FINLINE void
init_trg_descriptions
  (struct mem_allocator* alloc,
   struct trg_descriptions* data)
{
  int i;
  ASSERT(alloc && data);
  FOR_EACH(i, 0, SG3D_PROP_TYPES_COUNT__)
    darray_definition_init(alloc, data->defs + i);
  data->merge_conflict = 0;
  data->properties_conflict = 0;
  data->defs_include_unspecified = 0;
  FOR_EACH(i, 0, SG3D_PROP_TYPES_COUNT__)
    data->property_defined[i] = 0;
}

static INLINE res_T
copy_trg_descriptions
  (struct trg_descriptions* dst,
   const struct trg_descriptions* src)
{
  res_T res = RES_OK;
  int i;
  ASSERT(dst && src);
  FOR_EACH(i, 0, SG3D_PROP_TYPES_COUNT__)
    ERR(darray_definition_copy(&dst->defs[i], &src->defs[i]));
  dst->merge_conflict = src->merge_conflict;
  dst->properties_conflict = src->properties_conflict;
  dst->defs_include_unspecified = src->defs_include_unspecified;
  FOR_EACH(i, 0, SG3D_PROP_TYPES_COUNT__)
    dst->property_defined[i] = src->property_defined[i];
exit:
  return res;
error:
  goto exit;
}

static FINLINE void
release_trg_descriptions
  (struct trg_descriptions* data)
{
  int i;
  ASSERT(data);
  FOR_EACH(i, 0, SG3D_PROP_TYPES_COUNT__)
    darray_definition_release(data->defs + i);
}

#define DARRAY_NAME trg_descriptions
#define DARRAY_DATA struct trg_descriptions
#define DARRAY_FUNCTOR_INIT init_trg_descriptions
#define DARRAY_FUNCTOR_COPY copy_trg_descriptions
#define DARRAY_FUNCTOR_RELEASE release_trg_descriptions
#include <rsys/dynamic_array.h>

/******************************************************************************
 * Types to store geometry amid sg3d_geometry_add calls.
 *****************************************************************************/
struct sg3d_geometry {
  /* Record unique (i.e. deduplicated) triangles */
  struct darray_triangle unique_triangles;
  /* Record coordinates for unique (i.e. deduplicated) vertices */
  struct darray_vertex unique_vertices;

  /* A table to map triangle vertices to IDs in unique_triangles */
  struct htable_trg unique_triangles_ids;
  /* A table to map vertex coordinates to IDs in unique_vertices */
  struct htable_vrtx unique_vertices_ids;

  /* Record which set defined what */
  struct darray_trg_descriptions trg_descriptions;

  /* Counts */
  unsigned set_id;
  trg_id_t triangle_count_including_duplicates;
  side_id_t sides_with_defined_medium_count;
  trg_id_t trg_with_unspecified_sides_count;
  trg_id_t trg_with_unspecified_intface_count;
  trg_id_t merge_conflict_count;
  trg_id_t properties_conflict_count;

  struct sg3d_device* dev;
  ref_T ref;
};

/******************************************************************************
 * Local functions
 *****************************************************************************/

extern LOCAL_SYM res_T
geometry_register_triangle
  (struct sg3d_geometry* geometry,
   const struct triangle* triangle,
   const trg_id_t triangle_unique_id,
   const unsigned set_id,
   const int merge_conflict);

/* Add new undefined triangle descriptions to a geometry */
extern LOCAL_SYM res_T
geometry_enlarge_trg_descriptions
  (struct sg3d_geometry* geom,
   const trg_id_t sz);

#endif /* SG3D_GEOMETRY_H__ */
