/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef S2D_H
#define S2D_H

#include <rsys/rsys.h>
#include <float.h>

/* Library symbol management */
#if defined(S2D_SHARED_BUILD) /* Build shared library */
  #define S2D_API extern EXPORT_SYM
#elif defined(S2D_STATIC) /* Use/build static library */
  #define S2D_API extern LOCAL_SYM
#else /* Use shared library */
  #define S2D_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the s2d function `Func'
 * returns an error. One should use this macro on s2d function calls for which
 * no explicit error checking is performed */
#ifndef NDEBUG
  #define S2D(Func) ASSERT(s2d_ ## Func == RES_OK)
#else
  #define S2D(Func) s2d_ ## Func
#endif

/* Syntactic sugar use during the setup of the shape. Setting a vertex data
 * functor to S2D_KEEP means that this vertex data will not be updated */
#define S2D_KEEP NULL

#define S2D_INVALID_ID ((unsigned)-1) /* Value of an invalid identifer */

enum s2d_attrib_usage {
  S2D_POSITION, /* World space position */
  S2D_ATTRIB_0, /* Generic attrib 0 */
  S2D_ATTRIB_1, /* Generic attrib 1 */
  S2D_ATTRIB_2, /* Generic attrib 2 */
  S2D_ATTRIB_3, /* Generic attrib 3 */
  S2D_ATTRIBS_COUNT__,
  /* Unormalized world space face normal. For line segments, the outward
   * orientation is defined with respect to the Clock Wise vertex ordering */
  S2D_GEOMETRY_NORMAL
};

enum s2d_type {
  S2D_FLOAT,
  S2D_FLOAT2,
  S2D_FLOAT3
};

/* Primitive descriptor. The geom indentifier covers a compact ranges of value.
 * They can be used in conjunction with a dynamic array to map from s2d
 * geometry to application geometry */
struct s2d_primitive {
  unsigned prim_id; /* Primitive identifier */
  unsigned geom_id; /* Geometry identifier */
  unsigned scene_prim_id; /* Identifier of the primitive in the scene */
  /* Internal data. Should not be accessed  */
  void* mesh__;
};

#define S2D_PRIMITIVE_NULL__ {                                                 \
  S2D_INVALID_ID, S2D_INVALID_ID, S2D_INVALID_ID, NULL                         \
}
static const struct s2d_primitive S2D_PRIMITIVE_NULL = S2D_PRIMITIVE_NULL__;

/* Helper macro that defines whether or not 2 primitives are equal */
#define S2D_PRIMITIVE_EQ(Prim0, Prim1)                                         \
  (  (Prim0)->prim_id == (Prim1)->prim_id                                      \
  && (Prim0)->geom_id == (Prim1)->geom_id)

/* Untyped vertex attribute */
struct s2d_attrib {
  float value[3];
  enum s2d_type type;
  enum s2d_attrib_usage usage;
};

/* Describe a per vertex data */
struct s2d_vertex_data {
  /* Semantic of the data. Note that the S2D_GEOMETRY_NORMAL is not a valid
   * vertex usage */
  enum s2d_attrib_usage usage;
  enum s2d_type type;
  /* Retreive the vertex data value of `ivert'. Set it to S2D_KEEP, to keep the
   * previously set data */
  void (*get)
    (const unsigned ivert, /* Index of the vertex */
     float* value, /* Retrieved attrib value */
     void* ctx); /* Pointer to user data */
};

/* Invalid vertex data */
#define S2D_VERTEX_DATA_NULL__ { S2D_ATTRIBS_COUNT__, S2D_FLOAT, NULL }
static const struct s2d_vertex_data S2D_VERTEX_DATA_NULL = S2D_VERTEX_DATA_NULL__;

/* Intersection point */
struct s2d_hit {
  struct s2d_primitive prim; /* Intersected primitive */
  float normal[2]; /* Unormalized geometry normal (left hand convention) */
  float u; /* Barycentric coordinates onto `prim'. pos = (1 - u)*v0 + u*v1 */
  float distance; /* Hit distance from the ray origin */
};

/* Constant defining a NULL intersection. Should be used to initialize a hit */
#define S2D_HIT_NULL__ {                                                       \
  S2D_PRIMITIVE_NULL__,                                                        \
  { 0.f, 0.f },                                                                \
  0.f,                                                                         \
  FLT_MAX                                                                      \
}
static const struct s2d_hit S2D_HIT_NULL = S2D_HIT_NULL__;

/* Helper macro that defines whether or not the hit is valid, i.e. the ray
 * intersects a shape or not */
#define S2D_HIT_NONE(Hit) ((Hit)->distance >= FLT_MAX)

enum s2d_scene_view_flag {
  S2D_TRACE = BIT(0),
  S2D_SAMPLE = BIT(1),
  S2D_GET_PRIMITIVE = BIT(2)
};

/* Filter function data type. One can define such function to discard
 * intersections along a ray or the result of a closest point query with
 * respect to user defined criteria, e.g.: masked/transparent primitive, etc.
 * Return 0 if the intersection is not discarded and a value not equal to zero
 * otherwise. */
typedef int
(*s2d_hit_filter_function_T)
  (const struct s2d_hit* hit,
   const float ray_org[2],
   const float ray_dir[],
   const float ray_range[2],
   void* ray_data, /* User data submitted on trace ray(s) invocation */
   void* filter_data); /* Data defined on the setup of the filter function */

/* Forward declaration of s2d opaque data types */
struct s2d_device;
struct s2d_scene;
struct s2d_scene_view;
struct s2d_shape;

/* Forward declaration of external data types */
struct logger;
struct mem_allocator;

BEGIN_DECLS

/*******************************************************************************
 * Device API - entry point of the s2d library
 ******************************************************************************/
S2D_API res_T
s2d_device_create
  (struct logger* logger, /* May be NULL <=> use default logger */
   struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const int verbose, /* Define the level of verbosity */
   struct s2d_device** dev);

S2D_API res_T
s2d_device_ref_get
  (struct s2d_device* dev);

S2D_API res_T
s2d_device_ref_put
  (struct s2d_device* dev);

/*******************************************************************************
 * Scene API - collection of shapes
 ******************************************************************************/
S2D_API res_T
s2d_scene_create
  (struct s2d_device* dev,
   struct s2d_scene** scn);

S2D_API res_T
s2d_scene_ref_get
  (struct s2d_scene* scn);

S2D_API res_T
s2d_scene_ref_put
  (struct s2d_scene* scn);

/* Attach the shape to the scene. On success, the scene gets a reference onto
 * the attached shape */
S2D_API res_T
s2d_scene_attach_shape
  (struct s2d_scene* scn,
   struct s2d_shape* shape);

/* Remove the shape from the scene. After its detachment, the scene
 * release its reference on the shape */
S2D_API res_T
s2d_scene_detach_shape
  (struct s2d_scene* scn,
   struct s2d_shape* shape);

/* Detach all the shapes from the scene and release the reference that the
 * scene takes onto them */
S2D_API res_T
s2d_scene_clear
  (struct s2d_scene* scn);

/* Retrieve the device from which the scene was created */
S2D_API res_T
s2d_scene_get_device
  (struct s2d_scene* scn,
   struct s2d_device** dev);

S2D_API res_T
s2d_scene_get_shapes_count
  (struct s2d_scene* scn,
   size_t* nshapes);

/*******************************************************************************
 * Scene view API - State of the scene geometry
 ******************************************************************************/
S2D_API res_T
s2d_scene_view_create
  (struct s2d_scene* scn,
   const int mask, /* Combination of s2d_scene_view_flag */
   struct s2d_scene_view** scnview);

S2D_API res_T
s2d_scene_view_ref_get
  (struct s2d_scene_view* scnview);

S2D_API res_T
s2d_scene_view_ref_put
  (struct s2d_scene_view* scnview);

S2D_API res_T
s2d_scene_view_get_mask
  (struct s2d_scene_view* scnview,
   int* mask);

/* Trace a ray into the `scn' and return the closest intersection. The ray is
 * defined by `origin' + t*`direction' = 0 with t in [`range[0]', `range[1]').
 * Note that if range is degenerated (i.e. `range[0]' >= `range[1]') then the
 * ray is not traced and `hit' is set to S2D_HIT_NULL. Can be called only if
 * the scnview was created with the S2D_TRACE flag. */
S2D_API res_T
s2d_scene_view_trace_ray
  (struct s2d_scene_view* scnview,
   const float origin[2], /* Ray origin */
   const float direction[2], /* Ray direction. Must be normalized */
   const float range[2], /* In [0, INF)^2 */
   void* ray_data, /* User ray data sent to the hit filter func. May be NULL */
   struct s2d_hit* hit);

/* Trace a 3D ray into `scn' and return the closest intersection. The third
 * dimension of the scene primitives is assumed to be infinite, i.e. the
 * contours are extruded to the infinity in Z. The ray is defined by `origin' +
 * t*`direction' = 0 with t in [`range[0]', `range[1]'). The ray range as well
 * as the potential hit distance are expressed with respect to the 3D
 * direction. Note that if range is degenerated (i.e.  `range[0]' >=
 * `range[1]') then the ray is not traced and `hit' is set to S2D_HIT_NULL. Can
 * be called only if te scnview was created with the S2D_TRACE flag. */
S2D_API res_T
s2d_scene_view_trace_ray_3d
  (struct s2d_scene_view* scnview,
   const float origin[2],
   const float dir[3],
   const float range[2],
   void* ray_data,
   struct s2d_hit* hit);

/* Return the point onto the scene segments that is the closest of the
 * submitted `pos'. Note that even though only one point is returned, several
 * position can have the same minimal distance to the queried position. The
 * `radius' parameter defines the maximum search distance around `pos'. Each
 * candidate position are internally filtered by the hit_filter_function
 * attached to the corresponding shape; the user can thus reject a candidate
 * position according to its own criteria.  This function can be called only if
 * the scnview was created with the S2D_TRACE flag which is actually the flag
 * used to tell Star-2D to internally build an acceleration structure on which
 * this function relies. */
S2D_API res_T
s2d_scene_view_closest_point
  (struct s2d_scene_view* scnview,
   const float pos[2], /* Position to query */
   const float radius, /* Search distance in [0, radius[ */
   void* query_data, /* User data sent to the hit filter func. May be NULL */
   struct s2d_hit* hit);

/* Uniformly sample the scene and returned the sampled primitive and its sample
 * position. Can be called only if the scnview was created with the
 * S2D_SAMPLE flag */
S2D_API res_T
s2d_scene_view_sample
  (struct s2d_scene_view* scnview,
   const float u, const float v, /* Random numbers in [0, 1) */
   struct s2d_primitive* primitive, /* Sampled primitive */
   float* s); /* Sampled parametric coordinates on the primitive */

/* Retrieve a primitive from the scene. Can be called only if the scnview was
 * created with the S2D_GET_PRIMITIVE flag */
S2D_API res_T
s2d_scene_view_get_primitive
  (struct s2d_scene_view* scnview,
   const unsigned iprim, /* in [0, #prims) */
   struct s2d_primitive* prim);

/* Retrieve the number of scene primitives. Can be called only if the scnview
 * was created with the S2D_GET_PRIMITIVE flag */
S2D_API res_T
s2d_scene_view_primitives_count
  (struct s2d_scene_view* scnview,
   size_t* primitives_count);

/* Compute the overall length of the shape contours */
S2D_API res_T
s2d_scene_view_compute_contour_length
  (struct s2d_scene_view* scnview,
   float* length);

/* This function assumes that the scene defines a closed polygon and that the
 * normals point into the polygon. */
S2D_API res_T
s2d_scene_view_compute_area
  (struct s2d_scene_view* scnview,
   float* area);

/* Retrieve the Axis Aligned Bounding Box of the scene. */
S2D_API res_T
s2d_scene_view_get_aabb
  (struct s2d_scene_view* scnview,
   float lower[2], /* AABB lower bound */
   float upper[2]); /* AABB upper bound */

/*******************************************************************************
 * Shape API - define a 2D contour that can be attached to a scene.
 ******************************************************************************/
S2D_API res_T
s2d_shape_create_line_segments
  (struct s2d_device* dev,
   struct s2d_shape** shape);

S2D_API res_T
s2d_shape_ref_get
  (struct s2d_shape* shape);

S2D_API res_T
s2d_shape_ref_put
  (struct s2d_shape* shape);

/* Retrieve the id of the shape. This id covers a compact range of value.
 * Consequently, it can be used to map from the s2d shapes to the geometry
 * representation of the caller with a simple dynamic array */
S2D_API res_T
s2d_shape_get_id
  (const struct s2d_shape* shape,
   unsigned* id);

/* Enable/disable the shape, i.e. it cannot be hit when its associated scene is
 * ray-traced or sampled */
S2D_API res_T
s2d_shape_enable
  (struct s2d_shape* shape,
   const char enable);

/* Return whether or not the shape is enabled, i.e. ray-traced or sampled.
 * Default is 1 */
S2D_API res_T
s2d_shape_is_enabled
  (const struct s2d_shape* shape,
   char* is_enabled);

/* Define whether the shape is attached or not */
S2D_API res_T
s2d_shape_is_attached
  (const struct s2d_shape* shape,
   char* is_attached);

/* Flip the contour orientation, i.e. flip the normal of the contour */
S2D_API res_T
s2d_shape_flip_contour
  (struct s2d_shape* shape);

/*******************************************************************************
 * Primitive API - Define a geometric primitive of a shape
 ******************************************************************************/
/* Retrieve the attribute of the shape primitive `iprim' at the barycentric
 * coordinates `uv' */
S2D_API res_T
s2d_primitive_get_attrib
  (const struct s2d_primitive* prim,
   const enum s2d_attrib_usage attr, /* Attribute to retrieve */
   const float s, /* Parametric coordinates of `attr' on `prim' */
   struct s2d_attrib* attrib); /* Resulting attrib */

/* Uniform sampling of the primitive */
S2D_API res_T
s2d_primitive_sample
  (const struct s2d_primitive* prim,
   const float u, /* Random numbers in [0, 1) */
   float* s); /* Sampled parametric coordinates on prim */

S2D_API res_T
s2d_primitive_compute_length
  (const struct s2d_primitive* prim,
   float* area);

S2D_API res_T
s2d_segment_get_vertex_attrib
  (const struct s2d_primitive* prim,
   const size_t ivertex, /* in [0..2[ */
   const enum s2d_attrib_usage usage,
   struct s2d_attrib* attrib); /* Resulting attrib */

/*******************************************************************************
 * Line Segments API - manage a list of segments. Normal segments point toward
 * the semi space whose orientation is clock wise.
 ******************************************************************************/
S2D_API res_T
s2d_line_segments_setup_indexed_vertices
  (struct s2d_shape* shape,
   const unsigned nsegments,
   void (*get_indices) /* May be S2D_KEEP, i.e. do not update the indices */
    (const unsigned isegment, unsigned ids[2], void* ctx),
   const unsigned nverts,
   /* List of the shape vertex data. Must have at least an attrib with the
    * S2D_POSITION usage. */
   struct s2d_vertex_data attribs[],
   const unsigned nattribs, /* # attributes in the attribs list */
   void* data); /* Client data set as the last param of the callbacks */

/* Copy the line segments data from `src' to `dst' */
S2D_API res_T
s2d_line_segments_copy
  (const struct s2d_shape* src,
   struct s2d_shape* dst);

S2D_API res_T
s2d_line_segments_get_vertices_count
  (const struct s2d_shape* shape,
   unsigned* nverts);

S2D_API res_T
s2d_line_segments_get_vertex_attrib
  (const struct s2d_shape* shape,
   const unsigned ivert,
   const enum s2d_attrib_usage usage,
   struct s2d_attrib* attrib);

S2D_API res_T
s2d_line_segments_get_segments_count
  (const struct s2d_shape* shape,
   unsigned* nsegments);

S2D_API res_T
s2d_line_segments_get_segment_indices
  (const struct s2d_shape* shape,
   const unsigned isegment,
   unsigned ids[2]);

/* Define a intersection filter function. The filter function is invoked at
 * each intersection found during the s2d_scene_trace_ray calls. If func does
 * not return 0, then the intersection is ignored and the ray pursues its
 * traversal. */
S2D_API res_T
s2d_line_segments_set_hit_filter_function
  (struct s2d_shape* shape,
   s2d_hit_filter_function_T func,
   void* filter_data);

S2D_API res_T
s2d_line_segments_get_hit_filter_data
  (struct s2d_shape* shape,
   void** data);

END_DECLS

#endif /* S2D_H */

