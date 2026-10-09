/* Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef S3D_H
#define S3D_H

#include <rsys/rsys.h>
#include <float.h>

/* Library symbol management */
#if defined(S3D_SHARED_BUILD) /* Build shared library */
  #define S3D_API extern EXPORT_SYM
#elif defined(S3D_STATIC) /* Use/build static library */
  #define S3D_API extern LOCAL_SYM
#else /* Use shared library */
  #define S3D_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the s3d function `Func'
 * returns an error. One should use this macro on s3d function calls for which
 * no explicit error checking is performed */
#ifndef NDEBUG
  #define S3D(Func) ASSERT(s3d_ ## Func == RES_OK)
#else
  #define S3D(Func) s3d_ ## Func
#endif

/* Syntactic sugar use during the setup of the shape. Setting a vertex data
 * functor to S3D_KEEP means that this vertex data will not be updated */
#define S3D_KEEP NULL

#define S3D_INVALID_ID ((unsigned)-1) /* Value of an invalid identifer */

enum s3d_rays_flag {
  S3D_RAYS_SINGLE_ORIGIN = BIT(0), /* The rays have the same origin */
  S3D_RAYS_SINGLE_DIRECTION = BIT(1), /* The rays have the same direction */
  S3D_RAYS_SINGLE_RANGE = BIT(2), /* The rays have the same range */
  S3D_RAYS_SINGLE_DATA = BIT(3) /* The rays shared the same user defined data */
};

/* Attributes of a shape */
enum s3d_attrib_usage {
  S3D_POSITION, /* World space position */
  S3D_ATTRIB_0, /* Generic attrib 0 */
  S3D_ATTRIB_1, /* Generic attrib 1 */
  S3D_ATTRIB_2, /* Generic attrib 2 */
  S3D_ATTRIB_3, /* Generic attrib 3 */
  S3D_ATTRIBS_COUNT__,
  /* Unormalized world space face normal. For triangular meshes, the outward
   * orientation is defined with respect to the Clock Wise vertex ordering */
  S3D_GEOMETRY_NORMAL
};

enum s3d_type {
  S3D_FLOAT,
  S3D_FLOAT2,
  S3D_FLOAT3,
  S3D_FLOAT4
};

enum s3d_transform_space {
  S3D_LOCAL_TRANSFORM, /* The transformation is local to the shape space */
  S3D_WORLD_TRANSFORM  /* The transformation is expressed in world space */
};

/* Primitive descriptor. The <geom|inst> indentifiers cover a compact ranges of
 * value. They can be used in conjunction with a dynamic array to map from s3d
 * geometry to application geometry */
struct s3d_primitive {
  unsigned prim_id; /* Primitive identifier */
  unsigned geom_id; /* Geometry identifier */
  unsigned inst_id; /* Instance identifier */
  unsigned scene_prim_id; /* Identifier of the primitive in the scene */
  /* Internal data. Should not be accessed  */
  void* shape__;
  void* inst__;
};

#define S3D_PRIMITIVE_NULL__ {                                                 \
  S3D_INVALID_ID, S3D_INVALID_ID, S3D_INVALID_ID, S3D_INVALID_ID, NULL, NULL   \
}
static const struct s3d_primitive S3D_PRIMITIVE_NULL = S3D_PRIMITIVE_NULL__;

/* Helper macro that defines whether or not 2 primites are equal */
#define S3D_PRIMITIVE_EQ(Prim0, Prim1)                                         \
  (  (Prim0)->prim_id == (Prim1)->prim_id                                      \
  && (Prim0)->geom_id == (Prim1)->geom_id                                      \
  && (Prim0)->inst_id == (Prim1)->inst_id)

/* Untyped vertex attribute */
struct s3d_attrib {
  float value[4];
  enum s3d_type type;
  enum s3d_attrib_usage usage;
};

/* Describe a per vertex data */
struct s3d_vertex_data {
  /* Semantic of the data. Note that the S3D_GEOMETRY_NORMAL is not a valid
   * vertex usage */
  enum s3d_attrib_usage usage;
  enum s3d_type type;
  /* Retreive the vertex data value of `ivert'. Set it to S3D_KEEP, to keep the
   * previously set data */
  void (*get)
    (const unsigned ivert, /* Index of the vertex */
     float* value, /* Retrieved attrib value */
     void* ctx); /* Pointer to user data */
};

/* Invalid vertex data */
#define S3D_VERTEX_DATA_NULL__ { S3D_ATTRIBS_COUNT__, S3D_FLOAT, NULL }
static const struct s3d_vertex_data S3D_VERTEX_DATA_NULL = S3D_VERTEX_DATA_NULL__;

/* Intersection point */
struct s3d_hit {
  struct s3d_primitive prim; /* Intersected primitive */
  float normal[3]; /* Un-normalized geometry normal (left hand convention) */
  float uv[2]; /* Barycentric coordinates of the hit onto `prim' */
  float distance; /* Hit distance from the query origin */
};

/* Constant defining a NULL intersection. Should be used to initialize a hit */
#define S3D_HIT_NULL__ {                                                       \
  {S3D_INVALID_ID, S3D_INVALID_ID, S3D_INVALID_ID, S3D_INVALID_ID, NULL, NULL},\
  {0.f, 0.f, 0.f},                                                             \
  {0.f, 0.f},                                                                  \
  FLT_MAX                                                                      \
}

static const struct s3d_hit S3D_HIT_NULL = S3D_HIT_NULL__;

enum s3d_scene_view_flag {
  S3D_TRACE = BIT(0),
  S3D_SAMPLE = BIT(1),
  S3D_GET_PRIMITIVE = BIT(2)
};

/* Helper macro that defines whether or not the hit is valid, i.e. the ray
 * intersects a shape or not */
#define S3D_HIT_NONE(Hit) ((Hit)->distance >= FLT_MAX)

/* Quality of the partitioning data structure used to accelerate geometry
 * queries. The lowest the structure quality is, the fastest it is built. On
 * the counterpart, a weak structure quality means that the partitioning of the
 * geometry is sub-optimal, leading to lower geometry query performances. */
enum s3d_accel_struct_quality {
  S3D_ACCEL_STRUCT_QUALITY_LOW,
  S3D_ACCEL_STRUCT_QUALITY_MEDIUM,
  S3D_ACCEL_STRUCT_QUALITY_HIGH
};

/* Define the properties of the partitioning data structure used to accelerate
 * geometry queries */
enum s3d_accel_struct_flag {
  /* Avoid optimisations that reduce arithmetic accuracy */
  S3D_ACCEL_STRUCT_FLAG_ROBUST = BIT(0),
  /* Improve the building performances of the acceleration structure for
   * dynamic scenes */
  S3D_ACCEL_STRUCT_FLAG_DYNAMIC = BIT(1),
  /* Reduce the memory consumption of the acceleration structure */
  S3D_ACCEL_STRUCT_FLAG_COMPACT = BIT(2)
};

/* Configuration of the partitioning structure used to accelerate geometry
 * queries */
struct s3d_accel_struct_conf {
  enum s3d_accel_struct_quality quality;
  int mask; /* combination of s3d_accel_struct_flag */
};
#define S3D_ACCEL_STRUCT_CONF_DEFAULT__ {                                      \
  S3D_ACCEL_STRUCT_QUALITY_MEDIUM,                                             \
  S3D_ACCEL_STRUCT_FLAG_ROBUST                                                 \
}
static const struct s3d_accel_struct_conf S3D_ACCEL_STRUCT_CONF_DEFAULT =
  S3D_ACCEL_STRUCT_CONF_DEFAULT__;

/* Filter function data type. One can define such function to discard
 * intersections along a ray or the result of a closest point query with
 * respect to user defined criteria, e.g.: masked/transparent primitive, etc.
 * Return 0 if or the intersection is not discarded and a value not equal to zero
 * otherwise. */
typedef int
(*s3d_hit_filter_function_T)
  (const struct s3d_hit* hit,
   const float org[3],
   const float dir[3], /* Direction from `org' to `hit' */
   const float range[2], /* Submitted range */
   void* query_data, /* User data submitted on query invocation */
   void* filter_data); /* Data defined on the setup of the filter function */

/* Forward declaration of s3d opaque data types */
struct s3d_device; /* Entry point of the library */
struct s3d_scene; /* Collection of shapes */
struct s3d_scene_view; /* Scene state */
struct s3d_shape; /* Surfacic geometry */

/* Forward declaration of external data types */
struct logger;
struct mem_allocator;

/*
 * All the s3d structures are ref counted. Once created with the appropriated
 * `s3d_<TYPE>_create' function, the caller implicitly owns the created data,
 * i.e. its reference counter is set to 1. The s3d_<TYPE>_ref_<get|put>
 * functions get or release a reference on the data, i.e. they increment or
 * decrement the reference counter, respectively. When this counter reach 0 the
 * object is silently destroyed and cannot be used anymore.
 */

BEGIN_DECLS

/*******************************************************************************
 * Device API - A device is the entry point of the s3d library. Applications
 * use a s3d_device to create others s3d resources.
 ******************************************************************************/
S3D_API res_T
s3d_device_create
  (struct logger* logger, /* May be NULL <=> use default logger */
   struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const int verbose, /* Define the level of verbosity */
   struct s3d_device** dev);

S3D_API res_T
s3d_device_ref_get
  (struct s3d_device* dev);

S3D_API res_T
s3d_device_ref_put
  (struct s3d_device* dev);

/*******************************************************************************
 * Scene API - A scene is a collection of untyped shapes. It can be ray-traced
 * and/or "instantiated" through a shape.
 ******************************************************************************/
S3D_API res_T
s3d_scene_create
  (struct s3d_device* dev,
   struct s3d_scene** scn);

S3D_API res_T
s3d_scene_ref_get
  (struct s3d_scene* scn);

S3D_API res_T
s3d_scene_ref_put
  (struct s3d_scene* scn);

S3D_API res_T
s3d_scene_instantiate
  (struct s3d_scene* scn,
   struct s3d_shape** shape);

/* Attach the shape to the scene. On success, the scene gets a reference onto
 * the attached shape */
S3D_API res_T
s3d_scene_attach_shape
  (struct s3d_scene* scn,
   struct s3d_shape* shape);

/* Remove the shape from the scene. After its detachment, the scene
 * release its reference on the shape */
S3D_API res_T
s3d_scene_detach_shape
  (struct s3d_scene* scn,
   struct s3d_shape* shape);

/* Detach all the shapes from the scene and release the reference that the
 * scene takes onto them */
S3D_API res_T
s3d_scene_clear
  (struct s3d_scene* scn);

S3D_API res_T
s3d_scene_get_device
  (struct s3d_scene* scn,
   struct s3d_device** dev);

S3D_API res_T
s3d_scene_get_shapes_count
  (struct s3d_scene* scn,
   size_t* nshapes);

/*******************************************************************************
 * Scene view API - State of the scene geometry
 ******************************************************************************/
S3D_API res_T
s3d_scene_view_create
  (struct s3d_scene* scn,
   const int mask, /* Combination of s3d_scene_view_flag */
   struct s3d_scene_view** scnview);

S3D_API res_T
s3d_scene_view_create2
  (struct s3d_scene* scn,
   const int mask, /* Combination of s3d_scene_view_flag */
   /* Ignored if (mask & S3D_TRACE) == 0
    * NULL <=> use S3D_ACCEL_STRUCT_CONF_DEFAULT */
   const struct s3d_accel_struct_conf* cfg,
   struct s3d_scene_view** scnview);

S3D_API res_T
s3d_scene_view_ref_get
  (struct s3d_scene_view* scnview);

S3D_API res_T
s3d_scene_view_ref_put
  (struct s3d_scene_view* scnview);

S3D_API res_T
s3d_scene_view_get_mask
  (struct s3d_scene_view* scnview,
   int* mask);

/* Trace a ray into the scene and return the closest intersection along it. The
 * ray is defined by `origin' + t*`direction' = 0 with t in [`range[0]',
 * `range[1]').  Note that if a range is degenerated (i.e.  `range[0]' >=
 * `range[1]') then the ray is not traced and `hit' is set to S3D_HIT_NULL. Can
 * be called only if the scnview was created with the S3D_TRACE flag. */
S3D_API res_T
s3d_scene_view_trace_ray
  (struct s3d_scene_view* scnview,
   const float origin[3], /* Ray origin */
   const float direction[3], /* Ray direction. Must be normalized */
   const float range[2], /* In [0, INF)^2 */
   void* ray_data, /* User ray data sent to the hit filter func. May be NULL */
   struct s3d_hit* hit);

/* Trace a bundle of rays into the scene. Can be called only if the scnview was
 * created with the S3D_TRACE flag. */
S3D_API res_T
s3d_scene_view_trace_rays
  (struct s3d_scene_view* scnview,
   const size_t nrays, /* # rays */
   const int mask, /* Combination of s3d_rays_flag */
   const float* origins, /* List of 3D ray origins */
   const float* directions, /* List of 3D ray directions */
   const float* ranges, /* List of 2D ray ranges. in [0, INF)^2 */
   void* rays_data, /* User ray data sent to the hit filter func. May be NULL */
   const size_t sizeof_ray_data, /* Size in Bytes of *one* ray data */
   struct s3d_hit* hits);

/* Return the point onto the scene surfaces that is the closest of the
 * submitted `pos'. Note that even though only one point is returned, several
 * position can have the same minimal distance to the queried position. The
 * `radius' parameter defines the maximum search distance around `pos'. Each
 * candidate position are internally filtered by the hit_filter_function
 * attached to the corresponding shape; the user can thus reject a candidate
 * position according to its own criteria.  This function can be called only if
 * the scnview was created with the S3D_TRACE flag which is actually the flag
 * used to tell Star-3D to internally build an acceleration structure on which
 * this function relies. */
S3D_API res_T
s3d_scene_view_closest_point
  (struct s3d_scene_view* scnview,
   const float pos[3], /* Position to query */
   const float radius, /* Search distance in [0, radius[ */
   void* query_data, /* User data sent to the hit filter func. May be NULL */
   struct s3d_hit* hit);

/* Uniformly sample the scene and return the sampled primitive and its sample
 * uv position. Can be called only if the scnview was created with the
 * S3D_SAMPLE flag */
S3D_API res_T
s3d_scene_view_sample
  (struct s3d_scene_view* scnview,
   const float u, const float v, const float w, /* Random numbers in [0, 1) */
   struct s3d_primitive* primitive, /* Sampled primitive */
   float st[2]); /* Sampled parametric coordinates on the primitive */

/* Retrieve a primitive from the scene. Can be called only if the scnview was
 * created with the S3D_GET_PRIMITIVE flag */
S3D_API res_T
s3d_scene_view_get_primitive
  (struct s3d_scene_view* scnview,
   const unsigned iprim, /* in [0, #prims) */
   struct s3d_primitive* prim);

/* Return the overall number of scene primitives */
S3D_API res_T
s3d_scene_view_primitives_count
  (struct s3d_scene_view* scnview,
   size_t* primitives_count);

/* Compute the overall scene surface area */
S3D_API res_T
s3d_scene_view_compute_area
  (struct s3d_scene_view* scnview,
   float* area);

/* This function assumes that the scene defines a closed volume and that the
 * normals point into the volume. */
S3D_API res_T
s3d_scene_view_compute_volume
  (struct s3d_scene_view* scnview,
   float* volume);

/* Retrieve the Axis Aligned Bounding Box of the scene */
S3D_API res_T
s3d_scene_view_get_aabb
  (struct s3d_scene_view* scnview,
   float lower[3], /* AABB lower bound */
   float upper[3]); /* AABB upper bound */

/*******************************************************************************
 * Shape API - A shape defines a geometry that can be attached to a scene.
 ******************************************************************************/
S3D_API res_T
s3d_shape_ref_get
  (struct s3d_shape* shape);

S3D_API res_T
s3d_shape_ref_put
  (struct s3d_shape* shape);

/* Retrieve the id of the shape. This id covers a compact range of value.
 * Consequently, it can be used to map from the s3d shapes to the geometry
 * representation of the caller with a simple dynamic array */
S3D_API res_T
s3d_shape_get_id
  (const struct s3d_shape* shape,
   unsigned* id);

/* Enable/disable the shape, i.e. it cannot be hit when its associated scene is
 * ray-traced or sampled */
S3D_API res_T
s3d_shape_enable
  (struct s3d_shape* shape,
   const char enable);

/* Return whether or not the shape is enabled, i.e. ray-traced. Default is 1 */
S3D_API res_T
s3d_shape_is_enabled
  (const struct s3d_shape* shape,
   char* is_enabled);

/* Flip the surface orientation, i.e. flip the geometric normal of the surface */
S3D_API res_T
s3d_shape_flip_surface
  (struct s3d_shape* shape);

/*******************************************************************************
 * Primitive API - Define a geometric primitive of a shape
 ******************************************************************************/
/* Retrieve the attribute of the shape primitive `prim' at the barycentric
 * coordinates `uv' */
S3D_API res_T
s3d_primitive_get_attrib
  (const struct s3d_primitive* prim,
   const enum s3d_attrib_usage attr, /* Attribute to retrieve */
   const float st[2], /* Parametric coordinates of `attr' on `prim' */
   struct s3d_attrib* attrib); /* Resulting attrib */

/* Retrieve if the primitive `prim' has the attribute `attr' */
S3D_API res_T
s3d_primitive_has_attrib
  (const struct s3d_primitive* prim,
   const enum s3d_attrib_usage attr,
   char* has_attrib);

/* Uniform sampling of the primitive */
S3D_API res_T
s3d_primitive_sample
  (const struct s3d_primitive* prim,
   const float u, const float v, /* Random numbers in [0, 1) */
   float st[2]); /* Sampled parametric coordinates on prim */

S3D_API res_T
s3d_primitive_compute_area
  (const struct s3d_primitive* prim,
   float* area);

S3D_API res_T
s3d_primitive_get_transform
  (const struct s3d_primitive* prim,
   float transform[12]); /* 3x4 column major matrix */

S3D_API res_T
s3d_triangle_get_vertex_attrib
  (const struct s3d_primitive* prim,
   const size_t ivertex, /* in [0..3[ */
   const enum s3d_attrib_usage usage,
   struct s3d_attrib* attrib);

/*******************************************************************************
 * Sphere API - Manage a spherical shape. By default, the sphere normals point
 * outward the sphere. One can use the s3d_shape_flip_surface function to
 * revert them.
 ******************************************************************************/
S3D_API res_T
s3d_shape_create_sphere
  (struct s3d_device* dev,
   struct s3d_shape** sphere);

S3D_API res_T
s3d_sphere_setup
  (struct s3d_shape* shape,
   const float position[3],
   const float radius);

/* Define an intersection filter function. The filter function is invoked at
 * each intersection found during the s3d_scene_trace_ray(s) calls. If func
 * does not return 0, then the intersection is ignored and the ray pursues its
 * traversal. */
S3D_API res_T
s3d_sphere_set_hit_filter_function
  (struct s3d_shape* shape,
   s3d_hit_filter_function_T func,
   void* filter_data);

S3D_API res_T
s3d_sphere_get_hit_filter_data
  (struct s3d_shape* shape,
   void** data);

/*******************************************************************************
 * Mesh API - Manage a triangular meshes
 ******************************************************************************/
S3D_API res_T
s3d_shape_create_mesh
  (struct s3d_device* dev,
   struct s3d_shape** shape);

/* Set/update the data of the indexed triangular meshes */
S3D_API res_T
s3d_mesh_setup_indexed_vertices
  (struct s3d_shape* shape,
   const unsigned ntris,
   void (*get_indices) /* May be S3D_KEEP, i.e. do not update the indices */
    (const unsigned itri, unsigned ids[3], void* ctx),
   const unsigned nverts,
   /* List of the shape vertex data. Must have at least an attrib with the
    * S3D_POSITION usage. */
   struct s3d_vertex_data attribs[],
   const unsigned nattribs, /* # attributes in the attribs list */
   void* data); /* Client data set as the last param of the callbacks */

/* Copy the mesh data from `src' to `dst' */
S3D_API res_T
s3d_mesh_copy
  (const struct s3d_shape* src,
   struct s3d_shape* dst);

S3D_API res_T
s3d_mesh_get_vertices_count
  (const struct s3d_shape* shape,
   unsigned* nverts);

S3D_API res_T
s3d_mesh_get_vertex_attrib
  (const struct s3d_shape* shape,
   const unsigned ivert,
   const enum s3d_attrib_usage usage,
   struct s3d_attrib* attrib);

S3D_API res_T
s3d_mesh_get_triangles_count
  (const struct s3d_shape* shape,
   unsigned* ntris);

S3D_API res_T
s3d_mesh_get_triangle_indices
  (const struct s3d_shape* shape,
   const unsigned itri,
   unsigned ids[3]);

/* Define an intersection filter function. The filter function is invoked at
 * each intersection found during the s3d_scene_trace_ray(s) calls. If func
 * does not return 0, then the intersection is ignored and the ray pursues its
 * traversal. */
S3D_API res_T
s3d_mesh_set_hit_filter_function
  (struct s3d_shape* shape,
   s3d_hit_filter_function_T func,
   void* filter_data);

S3D_API res_T
s3d_mesh_get_hit_filter_data
  (struct s3d_shape* shape,
   void** data);

/*******************************************************************************
 * Instance API - An instance is a shape that encapsulates a scene and that
 * supports a local to world transformation. Since the scene geometry is stored
 * only a single time even though it is instantiated in several positions, one
 * can use this feature to create extremely large scene.
 ******************************************************************************/
S3D_API res_T
s3d_instance_set_position
  (struct s3d_shape* shape,
   const float position[3]);

S3D_API res_T
s3d_instance_translate
  (struct s3d_shape* shape,
   const enum s3d_transform_space space,
   const float translation[3]);

S3D_API res_T
s3d_instance_set_transform
  (struct s3d_shape* shape,
   const float transform[12]); /* 3x4 column major matrix */

S3D_API res_T
s3d_instance_transform
  (struct s3d_shape* shape,
   const enum s3d_transform_space space,
   const float transform[12]); /* 3x4 column major matrix */

END_DECLS

#endif /* S3D_H */

