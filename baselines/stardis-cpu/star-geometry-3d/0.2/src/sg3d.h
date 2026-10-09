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

#ifndef STAR_GEOMETRY3D_H__
#define STAR_GEOMETRY3D_H__

#include <rsys/rsys.h>
#include <rsys/dynamic_array_uint.h>

#include <limits.h>

/* Library symbol management */
#if defined(SG3D_SHARED_BUILD)
  #define SG3D_API extern EXPORT_SYM
#elif defined(SG3D_STATIC_BUILD)
  #define SG3D_API extern LOCAL_SYM
#else /* Use shared library */
  #define SG3D_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the star-geometry-3d function
 * `Func' returns an error. One should use this macro on star-geometry-3d
 * function calls for which no explicit error checking is performed. */
#ifndef NDEBUG
#define SG3D(Func) ASSERT(sg3d_ ## Func == RES_OK)
#else
#define SG3D(Func) sg3d_ ## Func
#endif

/* Forward declaration of external opaque data types */
struct logger;
struct mem_allocator;
struct senc_scene;

/* Forward declaration of the star-geometry-3d opaque data types. These data
 * types are ref counted. Once created the caller implicitly owns the created
 * data, i.e. its reference counter is set to 1. The sg3d_<TYPE>_ref_<get|put>
 * functions get or release a reference on the data, i.e. they increment or
 * decrement the reference counter, respectively. When this counter reaches 0,
 * the object is silently destroyed and cannot be used anymore. */
struct sg3d_device;
struct sg3d_geometry;

/******************************************************************************
 * The dimension of the geometry used in the library.
 *****************************************************************************/
#define SG3D_GEOMETRY_DIMENSION 3

/******************************************************************************
 * A type to list the different user properties attached to triangles.
 *****************************************************************************/
enum sg3d_property_type {
  SG3D_FRONT,
  SG3D_BACK,
  SG3D_INTFACE,
  SG3D_PROP_TYPES_COUNT__
};

/******************************************************************************
 * A type to list the different possible partitions of triangles.
 *****************************************************************************/
enum sg3d_obj_dump_content {
  SG3D_OBJ_DUMP_MERGE_CONFLICTS = BIT(0),
  SG3D_OBJ_DUMP_PROPERTY_CONFLICTS = BIT(1),
  SG3D_OBJ_DUMP_VALID_PRIMITIVE = BIT(2),
  SG3D_OBJ_DUMP_ALL =
     SG3D_OBJ_DUMP_MERGE_CONFLICTS
   | SG3D_OBJ_DUMP_PROPERTY_CONFLICTS
   | SG3D_OBJ_DUMP_VALID_PRIMITIVE
};

/******************************************************************************
 * A type to list the different qualifiers of C code variable output.
 *****************************************************************************/
enum sg3d_c_dump_qualifiers {
  SG3D_C_DUMP_CONST = BIT(0),
  SG3D_C_DUMP_STATIC = BIT(1)
};

/******************************************************************************
 * The value that should be used for properties attached to triangles (i.e.
 * media or interface) when let unspecified.
 * SG3D_UNSPECIFIED_PROPERTY can be used for a property that has already been
 * defined; in this case the previous value will remain.
 *****************************************************************************/
#define SG3D_UNSPECIFIED_PROPERTY UINT_MAX

/*****************************************************************************
 * A type to hold callbacks for sg3d_geometry_add.
 ****************************************************************************/
struct sg3d_geometry_add_callbacks {
  /* User function that provides vertices ids for added triangles */
  void(*get_indices)
    (const unsigned itri,
     unsigned ids[SG3D_GEOMETRY_DIMENSION],
     void* context);
  /* User function that provides properties for added triangles */
  void(*get_properties) /* Can be NULL <=> SG3D_UNSPECIFIED_PROPERTY used */
    (const unsigned itri,
     /* It is OK to have side and interface properties sharing the same IDs,
      * i.e. side and interface IDs both starting from 0 */
     unsigned properties[SG3D_PROP_TYPES_COUNT__],
     void* context);
  /* User function that provides coordinates for added vertices */
  void(*get_position)
    (const unsigned ivert, double pos[SG3D_GEOMETRY_DIMENSION], void* context);
  /* Called if the itri_th triangle of the current sg3d_geometry_add is a new
   * triangle (i.e. not a duplicate) so that the client app can manage its own
   * triangle data/properties/attributes.
   * If return is not RES_OK, sg3d_geometry_add stops immediately and returns
   * whatever value add_triangle returned. */
  res_T(*add_triangle) /* Can be NULL */
    (const unsigned unique_id, const unsigned itri, void* context);
  /* Called if the itri_th triangle of the current sg3d_geometry_add is a
   * duplicate of the unique_id_th unique triangle so that the client app can
   * merge its own triangle data, rule merge validity, and possibly change the
   * recorded properties.
   * The reversed_triangle arg indicates if the triangle vertices' order is
   * the same it was when the triangle was first added.
   * The merge_conflict_status argument can be set to any value. Any non-0
   * value is accounted for a conflict and is kept as-is in dumps, allowing
   * different shades of conflict.
   * The triangle_properties and merged_properties args contain the involved
   * properties. */
  res_T(*merge_triangle) /* Can be NULL */
    (const unsigned unique_id,
     const unsigned itri,
     const int reversed_triangle,
     unsigned triangle_properties[SG3D_PROP_TYPES_COUNT__],
     const unsigned merged_properties[SG3D_PROP_TYPES_COUNT__],
     void* context,
     int* merge_conflict_status);
  /* Called if the itri_th triangle is degenerated. According to the value
   * of abort, sg3d_geometry_add will stop and return RES_BAD_ARG or continue
   * silently. */
  res_T(*degenerated_triangle) /* Can be NULL <=> Drop triangle, don't abort */
    (const unsigned itri, void* context, int* abort);
};
#define SG3D_ADD_CALLBACKS_NULL__ { NULL, NULL, NULL, NULL, NULL, NULL }

BEGIN_DECLS

/******************************************************************************
 * A helper function on properties compatibility.
 *****************************************************************************/
static INLINE int
sg3d_compatible_property
  (const unsigned p1,
   const unsigned p2)
{
  if(p1 == SG3D_UNSPECIFIED_PROPERTY || p2 == SG3D_UNSPECIFIED_PROPERTY)
    return 1;
  return (p1 == p2);
}

/******************************************************************************
 * star-geometry-3d device. It is an handle toward the star-geometry-3d
 * library. It manages the star-geometry-3d resources.
 *****************************************************************************/
SG3D_API res_T
sg3d_device_create
  (struct logger* logger, /* Can be NULL <=> use default logger */
   struct mem_allocator* allocator, /* Can be NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   struct sg3d_device** dev);

SG3D_API res_T
sg3d_device_ref_get
  (struct sg3d_device* dev);

SG3D_API res_T
sg3d_device_ref_put
  (struct sg3d_device* dev);

/******************************************************************************
 * star-geometry-3d geometry.
 * It stores decorated geometry accumulated through calls to sg3d_geometry_add,
 * information related to this geometry and its creation process, including
 * merge conflicts.
 *****************************************************************************/
/* Create a geometry that can be used to accumulate vertices and triangles
 * decorated with properties. */
SG3D_API res_T
sg3d_geometry_create
  (struct sg3d_device* dev,
   struct sg3d_geometry** geometry);

/* Reserve memory according to anticipated geometry size. */
SG3D_API res_T
sg3d_geometry_reserve
  (struct sg3d_geometry* geometry,
   const unsigned vertices_count,
   const unsigned triangles_count,
   const unsigned properties_count);

/* Add a new set of 3D vertices and triangles to the geometry; triangles can
 * be decorated with 3 properties (front and back media and interface) that can
 * be let unspecified using the SG3D_UNSPECIFIED_PROPERTY special value.
 * Vertices can be duplicates and are silently deduplicated, always valid.
 * Triangles can be duplicates, but this can be ruled invalid due to property
 * inconsistency. Triangle duplicates are silently deduplicated, even if
 * invalid. Consistency is to be understood as the consistency of the
 * successive values for the same property across calls of sg3d_geometry_add,
 * not as the consistency of the values of the 3 properties of a triangle at
 * some given time (this question has its own callback (validate) in the
 * sg3d_geometry_validate_properties API call).
 * Duplicate triangles validity is either ruled by the user-provided
 * merge_triangle callback, or is just a matter of properties consistency if no
 * callback is provided. In either case, sg3d_geometry_add never stops
 * prematurely nor returns an error code due to a merge conflict.
 * - if provided, the callback must return the consistency status using the
 *   merge_conflict_status int* paramater (0 for consistent; any other value
 *   for inconsistent, this value being recorded); regardless of
 *   merge_conflict_status, the callback can change the properties of the
 *   triangle before the SG3D_UNSPECIFIED_PROPERTY overwriting step;
 * - if not, a non-SG3D_UNSPECIFIED_PROPERTY is only consistent with itself and
 *   with SG3D_UNSPECIFIED_PROPERTY (if inconsistent, merge_conflict_status is
 *   set to 1 and recorded) ; regardless of merge_conflict_status, a
 *   non-SG3D_UNSPECIFIED_PROPERTY property is never overridden.
 * When deduplicating triangles, the first occurence remains (with its
 * original index in user world). After consistency being computed, a final
 * step consists in rewriting SG3D_UNSPECIFIED_PROPERTY properties if the
 * merged property is defined. */
SG3D_API res_T
sg3d_geometry_add
  (struct sg3d_geometry* geometry,
   const unsigned vertices_count,
   const unsigned triangles_count,
   const struct sg3d_geometry_add_callbacks* callbacks,
   void* context); /* Can be NULL */

/* Apply a validation callback on each triangle included in this geometry that
 * is not already flagged with a merge error. If validate returns a non-RES_OK
 * value, the validation stops and returns the same error value; on the other
 * hand, validation goes to the end regardless of properties conflicts.
 * The properties_conflict_status argument can be set to any value. Any non-0
 * value is accounted for a conflict and is kept as-is in dumps, allowing
 * different shades of conflict.
 * If validation is processed again, the properties conflict count is reset,
 * as well as the properties_conflict_status status of the triangles. */
SG3D_API res_T
sg3d_geometry_validate_properties
  (struct sg3d_geometry* geometry,
   res_T(*validate)
     (const unsigned itri,
      const unsigned properties[SG3D_PROP_TYPES_COUNT__],
      void* context,
      int* properties_conflict_status),
   void* context); /* Can be NULL */

/* Get the number of unique vertices. */
SG3D_API res_T
sg3d_geometry_get_unique_vertices_count
  (const struct sg3d_geometry* geometry,
   unsigned* count);

/* Get the ivtx_th vertex. */
SG3D_API res_T
sg3d_geometry_get_unique_vertex
  (const struct sg3d_geometry* geometry,
   const unsigned ivtx,
   double coord[SG3D_GEOMETRY_DIMENSION]);

/* Get the number of triangles added to the geometry, regardless of unicity. */
SG3D_API res_T
sg3d_geometry_get_added_triangles_count
  (const struct sg3d_geometry* geometry,
   unsigned* count);

/* Get the number of unique triangles. */
SG3D_API res_T
sg3d_geometry_get_unique_triangles_count
  (const struct sg3d_geometry* geometry,
   unsigned* count);

/* Get the vertex indices of the itri_th unique triangle. */
SG3D_API res_T
sg3d_geometry_get_unique_triangle_vertices
  (const struct sg3d_geometry* geometry,
   const unsigned itri,
   unsigned indices[SG3D_GEOMETRY_DIMENSION]);

/* Get the properties of the itri_th unique triangle. */
SG3D_API res_T
sg3d_geometry_get_unique_triangle_properties
  (const struct sg3d_geometry* geometry,
   const unsigned itri,
   unsigned properties[SG3D_PROP_TYPES_COUNT__]);

/* Get the user ID of the itri_th unique triangle, that is the user world's
 * index of the triangle that first created this unique triangle.
 * User world index starts at 0 and increases for every triangle that is
 * submitted to sg3d_geometry_add calls, regardless of duplication or
 * sg3d_geometry_add failures (non-RES_OK return value). */
SG3D_API res_T
sg3d_geometry_get_unique_triangle_user_id
  (const struct sg3d_geometry* geometry,
   const unsigned itri,
   unsigned* user_id);

/* Get the number of triangles with (at least) one unspecified side. */
SG3D_API res_T
sg3d_geometry_get_unique_triangles_with_unspecified_side_count
  (const struct sg3d_geometry* geometry,
   unsigned* count);

/* Get the number of triangles with unspecified interface. */
SG3D_API res_T
sg3d_geometry_get_unique_triangles_with_unspecified_interface_count
  (const struct sg3d_geometry* geometry,
   unsigned* count);

/* Get the number of triangles flagged with a merge conflict. */
SG3D_API res_T
sg3d_geometry_get_unique_triangles_with_merge_conflict_count
  (const struct sg3d_geometry* geometry,
   unsigned* count);

/* Get the number of triangles flagged with a property conflict. Only
 * meaningful after sg3d_geometry_validate_properties has been called. */
SG3D_API res_T
sg3d_geometry_get_unique_triangles_with_properties_conflict_count
  (const struct sg3d_geometry* geometry,
   unsigned* count);

/* Dump a geometry in the provided stream in the OBJ format.
 * The geometry can include conflicts, but cannot be empty.
 * The dump is made of the vertices and some triangles, without their
 * properties. The dumped triangles are defined by the flags argument, that
 * must be ORed enum sg3d_obj_dump_content values.
 * The dumped triangles are partitioned in the following groups:
 * - Valid_triangles
 * - Merge_conflicts
 * - Property_conflict */
SG3D_API res_T
sg3d_geometry_dump_as_obj
  (const struct sg3d_geometry* geometry,
   FILE* stream,
   int flags);

/* Dump a geometry in the provided stream in the VTK ascii format.
 * The geometry can include conflicts, but cannot be empty.
 * The dump is made of the vertices and triangles, with most of their
 * properties:
 * - Front_medium (medium ID of the front side, INT_MAX for both unspecified
 *   and conflicted)
 * - Back_medium (medium ID of the back side, INT_MAX for both unspecified and
 *   conflicted)
 * - Interface (interface ID, INT_MAX for both unspecified and conflicted)
 * - Merge_conflict (merge conflict status)
 * - Property_conflict (property conflict status)
 * - Created_at_sg3d_geometry_add (rank of the sg3d_geometry_add that created
 *   the triangle) */
SG3D_API res_T
sg3d_geometry_dump_as_vtk
  (const struct sg3d_geometry* geometry,
   FILE* stream);

/* Dump the geometry as C code.
 * The geometry cannot be empty and must be conflict-free.
 * The C code defines the 3 variables:
 * - [static] [const] unsigned <name_prefix>_vertices_count = N1;
 * - [static] [const] double <name_prefix>_vertices[<N1>] = { .... };
 * - [static] [const] unsigned <name_prefix>_triangles_count = N2;
 * - [static] [const] unsigned <name_prefix>_triangles[<N2>] = { .... };
 * - [static] [const] unsigned <name_prefix>_properties[<N2>] = { .... };
 * Where <N1> is 3 * vertices_count and <N2> is 3 * triangles_count.
 * The two qualifiers static and const are output or not according to flags;
 * flags must be ORed enum sg3d_c_dump_qualifiers values. */
SG3D_API res_T
sg3d_geometry_dump_as_c_code
  (const struct sg3d_geometry* geometry,
   FILE* stream,
   const char* name_prefix, /* Can be NULL or "" */
   const int flags);

SG3D_API res_T
sg3d_geometry_ref_get
  (struct sg3d_geometry* geometry);

SG3D_API res_T
sg3d_geometry_ref_put
  (struct sg3d_geometry* geometry);

END_DECLS

#endif /* STAR_GEOMETRY3D_H__ */
