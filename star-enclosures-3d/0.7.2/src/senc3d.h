/* Copyright (C) 2018-2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SENC3D_H
#define SENC3D_H

#include <rsys/rsys.h>

#include <limits.h>

/* Library symbol management */
#if defined(SENC3D_SHARED_BUILD)
  #define SENC3D_API extern EXPORT_SYM
#elif defined(SENC3D_STATIC_BUILD)
  #define SENC3D_API extern LOCAL_SYM
#else /* Use shared library */
  #define SENC3D_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the StarEnc function `Func'
 * returns an error. One should use this macro on StarEnc function calls for
 * which no explicit error checking is performed. */
#ifndef NDEBUG
  #define SENC3D(Func) ASSERT(senc3d_ ## Func == RES_OK)
#else
  #define SENC3D(Func) senc3d_ ## Func
#endif

/* Syntactic sugar used to inform the library that it can use as many threads
 * as CPU cores */
#define SENC3D_NTHREADS_DEFAULT (~0u)

/* A constant to denote an unspecified medium */
#define SENC3D_UNSPECIFIED_MEDIUM UINT_MAX

/* Forward declaration of external opaque data types */
struct logger;
struct mem_allocator;

/* Forward declaration of star_enclosures-3d opaque data types. These data
 * types are ref counted. Once created with the appropriated
 * `senc3d__<TYPE>_create' function, the caller implicitly owns the created
 * data, i.e. its reference counter is set to 1.
 * The senc3d__<TYPE>_ref_<get|put> functions get or release a reference on the
 * data, i.e. they increment or decrement the reference counter, respectively.
 * When this counter reaches 0, the object is silently destroyed and cannot be
 * used anymore. */
struct senc3d_device;
struct senc3d_scene;
struct senc3d_enclosure;

/******************************************************************************
 * The dimension of the geometry used in the library.
 *****************************************************************************/
#define SENC3D_GEOMETRY_DIMENSION 3

/* A type to discriminate triangle sides */
enum senc3d_side {
  SENC3D_FRONT,
  SENC3D_BACK
};

/* Enclosure header type */
struct senc3d_enclosure_header {
  /* The ID of the enclosure; 0, 1, ... */
  unsigned enclosure_id;
  /* Number of triangles; a triangle can be counted twice, once by side*/
  unsigned primitives_count;
  /* Number of unique triangles; a triangle cannot be counted twice */
  unsigned unique_primitives_count;
  /* Number of vertices */
  unsigned vertices_count;
  /* The number of media inside the enclosure,
   * SENC3D_UNSPECIFIED_MEDIUM included */
  unsigned enclosed_media_count;
  /* Is the enclosure open/infinite?
   * Only the outermost enclosure is infinite. */
  int is_infinite;
  /* The volume of the enclosure, in m^3.
   * For the outermost enclosure, that goes to infinity, the volume is negative
   * and is the volume substracted from an hypothetical surrounding object.
   * If the two sides of a triangle are part of the enclosure, the triangle is
   * counted as 0. */
  double volume;
  /* The area of the enclosure, in m^2.
   * If the two sides of a triangle are part of the enclosure, the triangle is
   * counted twice. */
  double area;
};

/* We consider the geometrical normal Ng to a triangle V0 V1 V2
 * that verifies "(V0, V0V1, V0V2, Ng) is a direct system"
 * (right-handed system).
 *
 * The user can set the convention used to determine which side of
 * a triangle is to be considered front/back by using the flags :
 * SENC3D_CONVENTION_NORMAL_FRONT => Ng points toward the front side,
 * SENC3D_CONVENTION_NORMAL_BACK => Ng points toward the back side.
 *
 * Additionally the user can set the convention used to output enclosures
 * so that Ng points toward the enclosure or on the opposite direction
 * (for a closed enclosure Ng points toward the inside or toward the outside)
 * by using the flags :
 * SENC3D_CONVENTION_NORMAL_INSIDE => Ng points toward the enclosure,
 * SENC3D_CONVENTION_NORMAL_OUTSIDE => Ng points toward the opposite of the
 * enclosure.
 *
 * Note that normals in output data can be opposite to normals in input data
 * (vertices are then given in reverse order).
 *
 * Also note that both sides of a triangle can be part of the same enclosure;
 * in this case the 2 sides will be given with opposite Ng (meaning they
 * will be given with opposite vertices order).
 */
enum senc3d_convention {
  /*
   * Convention regarding FRONT/BACK sides in input data
   */

  /* Geometrical normals point toward the front side */
  SENC3D_CONVENTION_NORMAL_FRONT = BIT(0),
  /* Geometrical normals point toward the back side */
  SENC3D_CONVENTION_NORMAL_BACK = BIT(1),

  /*
   * Convention regarding geometrical normals in output data
   */

  /* Geometrical normals point toward the enclosure */
  SENC3D_CONVENTION_NORMAL_INSIDE = BIT(2),
  /* Geometrical normals point to the opposite of the enclosure */
  SENC3D_CONVENTION_NORMAL_OUTSIDE = BIT(3),

  /*
   * Additional bits used for debugging purposes
   */

  /* Dump identified connex components before grouping in STL files */
  SENC3D_DUMP_COMPONENTS_STL = BIT(4),
  /* Extensive logs on grouping algorithm */
  SENC3D_LOG_COMPONENTS_INFORMATION = BIT(5)
};

BEGIN_DECLS

/******************************************************************************
 * star_enclosures-3d device. It is an handle toward the StarEnc library.
 * It manages the lib resources.
 * If provided, the allocator has to be suitable for parallel high frequency
 * allocations. As a consequence, a rsys proxy allocator should be avoided.
 *****************************************************************************/
SENC3D_API res_T
senc3d_device_create
  (struct logger* logger, /* May be NULL <=> use default logger */
   struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const unsigned nthreads_hint,
   const int verbose,
   struct senc3d_device** device);

SENC3D_API res_T
senc3d_device_ref_get
  (struct senc3d_device* device);

SENC3D_API res_T
senc3d_device_ref_put
  (struct senc3d_device* device);

/******************************************************************************
 * star_enclosures-3d scene. A scene is a collection of triangles. Each
 * triangle is defined with a medium on each side.
 * Scenes with overlapping triangles are considered ill-formed and any
 * enclosure-related API call on such a scene will return RES_BAD_OP.
 *****************************************************************************/
/* Creates a scene from some vertices and triangles.
 * Neither vertices nor triangles can include duplicates.
 * Triangles cannot be degenerated. */
SENC3D_API res_T
senc3d_scene_create
  (struct senc3d_device* device,
   const int convention,
   /* Number of triangles */
   const unsigned triangles_count,
   /* User function that provides vertices ids for triangles */
   void(*get_indices)(
     const unsigned itri,
     unsigned ids[SENC3D_GEOMETRY_DIMENSION],
     void* context),
   /* User function that provides medium ids for triangles */
   void(*get_media) /* Can be NULL <=> SENC3D_UNSPECIFIED_MEDIUM medium used */
     (const unsigned itri, unsigned med[2], void* context),
   /* Number of vertices */
   const unsigned vertices_count,
   /* User function that provides coordinates for vertices */
   void(*get_position)(
     const unsigned ivert,
     double pos[SENC3D_GEOMETRY_DIMENSION],
     void* context),
   /* Context provided to user callbacks; can be NULL */
   void* context,
    /* The created scene */
   struct senc3d_scene** scene);

/* Returns the convention flags in use with the scene. */
SENC3D_API res_T
senc3d_scene_get_convention
  (const struct senc3d_scene* scene,
   int* convention);

/* Returns the number of triangles in the scene. */
SENC3D_API res_T
senc3d_scene_get_triangles_count
  (const struct senc3d_scene* scene,
   unsigned* count);

/* Returns the itri_th triangle vertices' indices. */
SENC3D_API res_T
senc3d_scene_get_triangle
  (const struct senc3d_scene* scene,
   const unsigned itri,
   unsigned indices[SENC3D_GEOMETRY_DIMENSION]);

/* Returns the media for the itri_th triangle. */
SENC3D_API res_T
senc3d_scene_get_triangle_media
  (const struct senc3d_scene* scene,
   const unsigned itri,
   unsigned media[2]);

/* Returns the number of vertices in the scene. */
SENC3D_API res_T
senc3d_scene_get_vertices_count
  (const struct senc3d_scene* scene,
   unsigned* count);

/* Returns the coordinates of the ivert_th vertex. */
SENC3D_API res_T
senc3d_scene_get_vertex
  (const struct senc3d_scene* scene,
   const unsigned ivert,
   double coord[SENC3D_GEOMETRY_DIMENSION]);

/* Returns the greater medium id found in geometry or SENC3D_UNSPECIFIED_MEDIUM
 * if the geometry only used SENC3D_UNSPECIFIED_MEDIUM. Any value in range
 * [0 max_medium_id[ as well as SENC3D_UNSPECIFIED_MEDIUM is valid in API calls
 * requiring a medium (even if this medium id is unused). */
SENC3D_API res_T
senc3d_scene_get_max_medium
  (const struct senc3d_scene* scene,
   unsigned* max_medium_id);

/* Returns the number of enclosures. */
SENC3D_API res_T
senc3d_scene_get_enclosure_count
  (const struct senc3d_scene* scene,
   unsigned* count);

/* Returns the number of enclosures that have some geometry refering to the
 * medium med (including SENC3D_UNSPECIFIED_MEDIUM). */
SENC3D_API res_T
senc3d_scene_get_enclosure_count_by_medium
  (const struct senc3d_scene* scene,
   const unsigned med,
   unsigned* count);

/* Returns the idx_th enclosure. */
SENC3D_API res_T
senc3d_scene_get_enclosure
  (struct senc3d_scene* scene,
   const unsigned idx,
   struct senc3d_enclosure** enclosure);

/* Returns the idx_th enclosure using the medium med (including
 * SENC3D_UNSPECIFIED_MEDIUM). */
SENC3D_API res_T
senc3d_scene_get_enclosure_by_medium
  (struct senc3d_scene* scene,
   const unsigned med,
   const unsigned idx,
   struct senc3d_enclosure** enclosure);

/* Returns the enclosures the itri_th triangle front and back sides are member
 * of. */
SENC3D_API res_T
senc3d_scene_get_triangle_enclosures
  (const struct senc3d_scene* scene,
   const unsigned itri,
   unsigned enclosures[2]);

/* Returns the number of segments that are frontier segments:
 * - that have arity 1 (single triangle using the segment)
 * - that connect 2 different media */
SENC3D_API res_T
senc3d_scene_get_frontier_segments_count
  (const struct senc3d_scene* scene,
   unsigned* count);

/* Returns the iseg_th frontier segment (triangle and vertices global IDs). */
SENC3D_API res_T
senc3d_scene_get_frontier_segment
  (const struct senc3d_scene* scene,
   const unsigned iseg,
   unsigned vrtx_id[SENC3D_GEOMETRY_DIMENSION-1],
   unsigned* trg_id);

/* Returns the number of overlapping triangles.
 * A model including overlapping triangles cannot be split into enclosures
 * unequivocally and will probably be ruled invalid by most softwares.
 * As a consequence, one cannot query enclosure-related information on a model
 * with overlapping triangles.
 * The library currently only detects overlapping triangles that share an
 * edge. */
SENC3D_API res_T
senc3d_scene_get_overlapping_triangles_count
  (const struct senc3d_scene* scene,
   unsigned* count);

/* Returns the idx_th overlapping triangle id. */
SENC3D_API res_T
senc3d_scene_get_overlapping_triangle
  (const struct senc3d_scene* scene,
   const unsigned idx,
   unsigned* id);

/* Dump a given enclosure in an OBJ file */
SENC3D_API res_T
senc3d_scene_dump_enclosure_obj
  (struct senc3d_scene* scn,
   const unsigned enc,
   const char* filename);

SENC3D_API res_T
senc3d_scene_ref_get
  (struct senc3d_scene* scene);

SENC3D_API res_T
senc3d_scene_ref_put
  (struct senc3d_scene* scene);

/******************************************************************************
 * star_enclosures-3d enclosure. It is an handle toward an enclosure.
 * Counts and other information on enclosures are not individually accessible,
 * but as a whole through header access.
 * An enclosure can list the "same" triangle twice if both sides are in. In
 * this case the 2 occurences of the triangle have reversed vertices order and
 * unique_triangle_count and triangle_count differ.
 * Vertices and triangles numbering schemes are specific to each enclosure:
 * the "same" item appearing in 2 different enclosures has no reason to get the
 * same index twice or to have the same index in the global numbering scheme.
 * By-index API accesses of triangles (or properties) visit unique triangles
 * for indices in the [0 unique_triangle_count[ range and back-faces of the
 * doubly-included triangles in the [unique_triangle_count triangle_count[
 * range.
 *****************************************************************************/
/* Returns the header of an enclosure. */
SENC3D_API res_T
senc3d_enclosure_get_header
  (const struct senc3d_enclosure* enclosure,
   struct senc3d_enclosure_header* header);

/* Returns the itri_th triangle of an enclosure.
 * Indices are local to the enclosure. */
SENC3D_API res_T
senc3d_enclosure_get_triangle
  (const struct senc3d_enclosure* enclosure,
   const unsigned itri,
   unsigned indices[SENC3D_GEOMETRY_DIMENSION]);

/* Returns the coordinates of the ivert_th vertex of an enclosure. */
SENC3D_API res_T
senc3d_enclosure_get_vertex
  (const struct senc3d_enclosure* enclosure,
   const unsigned ivert,
   double coord[SENC3D_GEOMETRY_DIMENSION]);

/* Returns the global id of the itri_th triangle of an enclosure
 * and the involved side. */
SENC3D_API res_T
senc3d_enclosure_get_triangle_id
  (const struct senc3d_enclosure* enclosure,
   const unsigned itri,
   unsigned* gid,
   enum senc3d_side* side);

/* Returns the the imed_th medium id of an enclosure. */
SENC3D_API res_T
senc3d_enclosure_get_medium
  (const struct senc3d_enclosure* enclosure,
   const unsigned imed,
   unsigned* medium);

SENC3D_API res_T
senc3d_enclosure_ref_get
  (struct senc3d_enclosure* enclosure);

SENC3D_API res_T
senc3d_enclosure_ref_put
  (struct senc3d_enclosure* enclosure);

END_DECLS

#endif /* SENC3D_H */
