/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SENC2D_H
#define SENC2D_H

#include <rsys/rsys.h>

#include <limits.h>

/* Library symbol management */
#if defined(SENC2D_SHARED_BUILD)
  #define SENC2D_API extern EXPORT_SYM
#elif defined(SENC2D_STATIC_BUILD)
  #define SENC2D_API extern LOCAL_SYM
#else /* Use shared library */
  #define SENC2D_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the StarEnc function `Func'
 * returns an error. One should use this macro on StarEnc function calls for
 * which no explicit error checking is performed. */
#ifndef NDEBUG
  #define SENC2D(Func) ASSERT(senc2d_ ## Func == RES_OK)
#else
  #define SENC2D(Func) senc2d_ ## Func
#endif

/* Syntactic sugar used to inform the library that it can use as many threads
 * as CPU cores */
#define SENC2D_NTHREADS_DEFAULT (~0u)

/* A constant to denote an unspecified medium */
#define SENC2D_UNSPECIFIED_MEDIUM UINT_MAX

/* Forward declaration of external opaque data types */
struct logger;
struct mem_allocator;

/* Forward declaration of star-enclosures-2d opaque data types. These data
 * types are ref counted. Once created with the appropriated
 * `senc2d_<TYPE>_create' function, the caller implicitly owns the created
 * data, i.e. its reference counter is set to 1.
 * The senc2d_<TYPE>_ref_<get|put> functions get or release a reference on the
 * data, i.e. they increment or decrement the reference counter, respectively.
 * When this counter reaches 0, the object is silently destroyed and cannot be
 * used anymore. */
struct senc2d_device;
struct senc2d_scene;
struct senc2d_enclosure;

/******************************************************************************
 * The dimension of the geometry used in the library.
 *****************************************************************************/
#define SENC2D_GEOMETRY_DIMENSION 2

/* A type to discriminate segment sides */
enum senc2d_side {
  SENC2D_FRONT,
  SENC2D_BACK
};

/* Enclosure header type */
struct senc2d_enclosure_header {
  /* The ID of the enclosure; 0, 1, ... */
  unsigned enclosure_id;
  /* Number of segments; a segment can be counted twice, once by side */
  unsigned primitives_count;
  /* Number of segments; a segment cannot be counted twice */
  unsigned unique_primitives_count;
  /* Number of vertices */
  unsigned vertices_count;
  /* The number of media inside the enclosure,
   * SENC2D_UNSPECIFIED_MEDIUM included */
  unsigned enclosed_media_count;
  /* Is the enclosure open/infinite?
   * Only the outermost enclosure is infinite. */
  int is_infinite;
  /* The volume of the enclosure, in m^2 (2D volume is in m^2!).
   * For the outermost enclosure, that goes to infinity, the volume is negative
   * and is the volume substracted from an hypothetical surrounding object.
   * If the two sides of a segment are part of the enclosure, the segment is
   * counted as 0. */
  double volume;
  /* The area of the enclosure, in m (2D area is in m!).
   * If the two sides of a segment are part of the enclosure, the segment is
   * counted twice. */
  double area;
};

/* We consider the geometrical normal Ng to a segment V0 V1 that verifies
 * "(V0, V0V1, Ng) is a direct system" (right-handed system).
 *
 * The user can set the convention used to determine which side of
 * a segment is to be considered front/back by using the flags :
 * SENC2D_CONVENTION_NORMAL_FRONT => Ng points toward the front side,
 * SENC2D_CONVENTION_NORMAL_BACK => Ng points toward the back side.
 *
 * Additionally the user can set the convention used to output enclosures
 * so that Ng points toward the enclosure or on the opposite direction
 * (for a closed enclosure Ng points toward the inside or toward the outside)
 * by using the flags :
 * SENC2D_CONVENTION_NORMAL_INSIDE => Ng points toward the enclosure,
 * SENC2D_CONVENTION_NORMAL_OUTSIDE => Ng points toward the opposite of the
 * enclosure.
 *
 * Note that normals in output data can be opposite to normals in input data
 * (vertices are then given in reverse order).
 *
 * Also note that both sides of a segment can be part of the same enclosure;
 * in this case the 2 sides will be given with opposite Ng (meaning they
 * will be given with opposite vertices order).
 */
enum senc2d_convention {
  /*
   * Convention regarding FRONT/BACK sides in input data
   */

  /* Geometrical normals point toward the front side */
  SENC2D_CONVENTION_NORMAL_FRONT = BIT(0),
  /* Geometrical normals point toward the back side */
  SENC2D_CONVENTION_NORMAL_BACK = BIT(1),

  /*
   * Convention regarding geometrical normals in output data
   */

  /* Geometrical normals point toward the enclosure */
  SENC2D_CONVENTION_NORMAL_INSIDE = BIT(2),
  /* Geometrical normals point to the opposite of the enclosure */
  SENC2D_CONVENTION_NORMAL_OUTSIDE = BIT(3)
};

BEGIN_DECLS

/******************************************************************************
 * star-enclosures-2d device. It is an handle toward the StarEnc library.
 * It manages the lib resources.
 * If provided, the allocator has to be suitable for parallel high frequency
 * allocations. As a consequence, a rsys proxy allocator should be avoided.
 *****************************************************************************/
SENC2D_API res_T
senc2d_device_create
  (struct logger* logger, /* May be NULL <=> use default logger */
   struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const unsigned nthreads_hint,
   const int verbose,
   struct senc2d_device** device);

SENC2D_API res_T
senc2d_device_ref_get
  (struct senc2d_device* device);

SENC2D_API res_T
senc2d_device_ref_put
  (struct senc2d_device* device);

/******************************************************************************
 * star-enclosures-2d scene. A scene is a collection of segments. Each segment
 * is defined with a medium on each side.
 * Scenes with overlapping segments are considered ill-formed and any
 * enclosure-related API call on such a scene will return RES_BAD_OP.
 *****************************************************************************/
/* Creates a scene from some vertices and segments.
 * Neither vertices nor segments can include duplicates.
 * Segments cannot be degenerated. */
SENC2D_API res_T
senc2d_scene_create
  (struct senc2d_device* device,
   const int convention,
   /* Number of segments */
   const unsigned segments_count,
   /* User function that provides vertices ids for segments */
   void(*get_indices)(
     const unsigned iseg,
     unsigned ids[SENC2D_GEOMETRY_DIMENSION],
     void* context),
   /* User function that provides media ids for segments */
   void(*get_media) /* Can be NULL <=> SENC2D_UNSPECIFIED_MEDIUM medium used */
     (const unsigned iseg, unsigned med[2], void* context),
   /* Number of vertices */
   const unsigned vertices_count,
   /* User function that provides coordinates for vertices */
   void(*get_position)(
     const unsigned ivert,
     double pos[SENC2D_GEOMETRY_DIMENSION],
     void* context),
   /* Context provided to user callbacks; can be NULL */
   void* context,
    /* The created scene */
   struct senc2d_scene** scene);

/* Returns the convention flags in use with the scene. */
SENC2D_API res_T
senc2d_scene_get_convention
  (const struct senc2d_scene* scene,
   int* convention);

/* Returns the number of segments in the scene. */
SENC2D_API res_T
senc2d_scene_get_segments_count
  (const struct senc2d_scene* scene,
   unsigned* count);

/* Returns the iseg_th segment vertices' indices. */
SENC2D_API res_T
senc2d_scene_get_segment
  (const struct senc2d_scene* scene,
   const unsigned iseg,
   unsigned indices[SENC2D_GEOMETRY_DIMENSION]);

/* Returns the media for the iseg_th segment. */
SENC2D_API res_T
senc2d_scene_get_segment_media
  (const struct senc2d_scene* scene,
   const unsigned iseg,
   unsigned media[2]);

/* Returns the number of vertices in the scene. */
SENC2D_API res_T
senc2d_scene_get_vertices_count
  (const struct senc2d_scene* scene,
   unsigned* count);

/* Returns the coordinates of the ivert_th vertex. */
SENC2D_API res_T
senc2d_scene_get_vertex
  (const struct senc2d_scene* scene,
   const unsigned ivert,
   double coord[SENC2D_GEOMETRY_DIMENSION]);

/* Returns the greater medium id found in added geometry. In API calls using a
 * medium, any value in the [0 max_medium_id[ range is valid. However there can
 * be unused ids (no geometry refered to this medium id).  */
SENC2D_API res_T
senc2d_scene_get_max_medium
  (const struct senc2d_scene* scene,
   unsigned* max_medium_id);

/* Returns the number of enclosures. */
SENC2D_API res_T
senc2d_scene_get_enclosure_count
  (const struct senc2d_scene* scene,
   unsigned* count);

/* Returns the number of enclosures that have some geometry refering to the
 * imed_th medium or SENC2D_UNSPECIFIED_MEDIUM. */
SENC2D_API res_T
senc2d_scene_get_enclosure_count_by_medium
  (const struct senc2d_scene* scene,
   const unsigned imed,
   unsigned* count);

/* Returns the idx_th enclosure. */
SENC2D_API res_T
senc2d_scene_get_enclosure
  (struct senc2d_scene* scene,
   const unsigned idx,
   struct senc2d_enclosure** enclosure);

/* Returns the idx_th enclosure using the imed_th medium or
 * SENC2D_UNSPECIFIED_MEDIUM. */
SENC2D_API res_T
senc2d_scene_get_enclosure_by_medium
  (struct senc2d_scene* scene,
   const unsigned imed,
   const unsigned idx,
   struct senc2d_enclosure** enclosure);

/* Returns the enclosures the iseg_th segment front and back sides are member
 * of. */
SENC2D_API res_T
senc2d_scene_get_segment_enclosures
  (const struct senc2d_scene* scene,
   const unsigned iseg,
   unsigned enclosures[2]);

/* Returns the number of vertices that are frontier vertices:
 * - that have arity 1 (single segment using the vertex)
 * - that connect 2 different media */
SENC2D_API res_T
senc2d_scene_get_frontier_vertice_count
  (const struct senc2d_scene* scene,
   unsigned* count);

/* Returns the iver_th frontier vertex (segment and vertex global IDs). */
SENC2D_API res_T
senc2d_scene_get_frontier_vertex
  (const struct senc2d_scene* scene,
   const unsigned iver,
   unsigned vrtx_id[SENC2D_GEOMETRY_DIMENSION-1],
   unsigned* seg_id);

/* Returns the number of overlapping segments.
 * A model including overlapping segments cannot be split into enclosures
 * unequivocally and will probably be ruled invalid by most softwares.
 * As a consequence, one cannot query enclosure-related information on a model
 * with overlapping segments.
 * The library currently only detects overlapping segments that share a
 * vertex. */
SENC2D_API res_T
senc2d_scene_get_overlapping_segments_count
  (const struct senc2d_scene* scene,
   unsigned* count);

/* Returns the idx_th overlapping triangle id. */
SENC2D_API res_T
senc2d_scene_get_overlapping_segment
  (const struct senc2d_scene* scene,
   const unsigned idx,
   unsigned* id);

SENC2D_API res_T
senc2d_scene_ref_get
  (struct senc2d_scene* scene);

SENC2D_API res_T
senc2d_scene_ref_put
  (struct senc2d_scene* scene);

/******************************************************************************
 * star-enclosures-2d enclosure. It is an handle toward an enclosure.
 * Counts and other information on enclosures are not individually accessible,
 * but as a whole through header access.
 * An enclosure can list the "same" segment twice if both sides are in. In this
 * case the 2 occurences of the segment have reversed vertices order and
 * unique_primitives_count and primitives_count differ.
 * Vertices and segments numbering schemes are specific to each enclosure:
 * the "same" item appearing in 2 different enclosures has no reason to get the
 * same index twice or to have the same index in the global numbering scheme.
 * By-index API accesses of segments (or properties) visit unique segments
 * for indices in the [0 unique_primitives_count[ range and back-faces of the
 * doubly-included segments in the [unique_primitives_count primitives_count[
 * range.
 *****************************************************************************/
/* Returns the header of an enclosure. */
SENC2D_API res_T
senc2d_enclosure_get_header
  (const struct senc2d_enclosure* enclosure,
   struct senc2d_enclosure_header* header);

/* Returns the iseg_th segment of an enclosure.
 * Indices are local to the enclosure. */
SENC2D_API res_T
senc2d_enclosure_get_segment
  (const struct senc2d_enclosure* enclosure,
   const unsigned iseg,
   unsigned indices[SENC2D_GEOMETRY_DIMENSION]);

/* Returns the coordinates of the ivert_th vertex of an enclosure. */
SENC2D_API res_T
senc2d_enclosure_get_vertex
  (const struct senc2d_enclosure* enclosure,
   const unsigned ivert,
   double coord[SENC2D_GEOMETRY_DIMENSION]);

/* Returns the global id of the iseg_th segment of an enclosure
 * and the involved side. */
SENC2D_API res_T
senc2d_enclosure_get_segment_id
  (const struct senc2d_enclosure* enclosure,
   const unsigned iseg,
   unsigned* gid,
   enum senc2d_side* side);

/* Returns the id of the imed_th medium of an enclosure. */
SENC2D_API res_T
senc2d_enclosure_get_medium
  (const struct senc2d_enclosure* enclosure,
   const unsigned imed,
   unsigned* medium);

SENC2D_API res_T
senc2d_enclosure_ref_get
  (struct senc2d_enclosure* enclosure);

SENC2D_API res_T
senc2d_enclosure_ref_put
  (struct senc2d_enclosure* enclosure);

END_DECLS

#endif /* SENC2D_H */
