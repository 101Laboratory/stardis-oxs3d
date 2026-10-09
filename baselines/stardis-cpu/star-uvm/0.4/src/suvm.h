/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef SUVM_H
#define SUVM_H

#include <rsys/hash.h>
#include <rsys/rsys.h>
#include <float.h>

/* Library symbol management */
#if defined(SUVM_SHARED_BUILD) /* Build shared library */
  #define SUVM_API extern EXPORT_SYM
#elif defined(SUVM_STATIC) /* Use/build static library */
  #define SUVM_API extern LOCAL_SYM
#else /* Use shared library */
  #define SUVM_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the suvm function `Func'
 * returns an error. One should use this macro on suvm function calls for
 * which no explicit error checking is performed */
#ifndef NDEBUG
  #define SUVM(Func) ASSERT(suvm_ ## Func == RES_OK)
#else
  #define SUVM(Func) suvm_ ## Func
#endif

/* Maximum number of vertices per volumetric primitive */
#define SUVM_PRIMITIVE_MAX_VERTICES_COUNT 4

enum suvm_volume_cpnt_flag {
  SUVM_POSITIONS = BIT(0),
  SUVM_INDICES = BIT(1),
  SUVM_PRIMITIVE_DATA = BIT(2),
  SUVM_VERTEX_DATA = BIT(3)
};

struct suvm_data {
  void (*get)(const size_t id, void* data, void* ctx); /* Data getter */
  size_t size; /* Size of the data in bytes */
  size_t alignment; /* Alignment of the data */
};
#define SUVM_DATA_NULL__ {NULL, 0, 0}
static const struct suvm_data SUVM_DATA_NULL = SUVM_DATA_NULL__;

struct suvm_primitive {
  const void* data; /* Data of the primitive */
  const void* vertex_data[SUVM_PRIMITIVE_MAX_VERTICES_COUNT]; /* Vertex data */
  size_t indices[SUVM_PRIMITIVE_MAX_VERTICES_COUNT]; /* Vertex indices */
  size_t iprim; /* Identifier of the primitive */
  size_t nvertices; /* #vertices of the primitive */

  /* Internal data */
  const struct suvm_volume* volume__;
};
#define SUVM_PRIMITIVE_NULL__ {                                                \
  NULL, /* Primitive data */                                                   \
  {NULL}, /* Vertex data */                                                    \
  {0}, /* Vertex indices */                                                    \
  SIZE_MAX, /* Primitive id */                                                 \
  SIZE_MAX, /* #vertices */                                                    \
  NULL /* Pointer toward its associated volume */                              \
}
static const struct suvm_primitive SUVM_PRIMITIVE_NULL = SUVM_PRIMITIVE_NULL__;

#define SUVM_PRIMITIVE_NONE(Prim) ((Prim)->iprim == SIZE_MAX)

/* Precomputed data from a suvm_primitive used to speed up AABB/primitive
 * intersection tests */
struct suvm_polyhedron {
  float v[4/*#vertices*/][3/*#coords*/]; /* Vertices */
  float N[4/*#vertices*/][3/*#coords*/]; /* Normals */
  float D[4/*#facets*/]; /* Slope parameter of the plane (D = -(Ax+By+Cz)) */

  /* 2D equation of the silhouette edges projected along the X, Y and Z axis of
   * the form Ax + By + C = 0, with (A, B) the normals of the edge  */
  float Ep[3/*#axes*/][4/*#edges max*/][3/*#equation parameters*/];

  /* Number of silhouette edges in the ZY, XZ and XY planes (3 or 4) */
  int nEp[3/*#coords*/];

  /* Tetrahedron axis aligned bounding box */
  float lower[3/*#coords*/];
  float upper[3/*#coords*/];
};

/* Internal copy of the unstructured mesh */
struct suvm_mesh_desc {
  const float* positions;
  const uint32_t* indices;
  size_t nvertices;
  size_t nprimitives;
  unsigned dvertex; /* Dimension of a vertex */
  unsigned dprimitive; /* Dimension of a primitive */
};
#define SUVM_MESH_DESC_NULL__ {NULL, NULL, 0, 0, 0, 0}
static const struct suvm_mesh_desc SUVM_MESH_DESC_NULL = SUVM_MESH_DESC_NULL__;

struct suvm_tetrahedral_mesh_args {
  size_t ntetrahedra; /* #tetrahedra */
  size_t nvertices; /* #vertices */

  /* Each tetrahedron has to list the bottom triangle in counterclockwise order,
   * and then the top vertex. */
  void (*get_indices)(const size_t itetra, size_t ids[4], void* ctx);
  void (*get_position)(const size_t ivert, double pos[3], void* ctx);

  /* Per tetrahedron/vertex data. SUVM_DATA_NULL <=> no data */
  struct suvm_data tetrahedron_data;
  struct suvm_data vertex_data;

  /* Define whether tetrahedron normals are precomputed or not. When
   * precomputed, the normals of each tetrahedron facet are explicitly stored
   * and thus increase the memory footprint but speed up accessors that use
   * them (e.g. suvm_volume_at). */
  int precompute_normals;

  void* context; /* Client data set as the last param of the callbacks */
};
#define SUVM_TETRAHEDRAL_MESH_ARGS_NULL__ {                                    \
  0, 0, NULL, NULL, SUVM_DATA_NULL__, SUVM_DATA_NULL__, 0, NULL                \
}
static const struct suvm_tetrahedral_mesh_args SUVM_TETRAHEDRAL_MESH_ARGS_NULL =
  SUVM_TETRAHEDRAL_MESH_ARGS_NULL__;

enum suvm_intersection_type {
  SUVM_INTERSECT_NONE,
  SUVM_INTERSECT_INCLUDE,
  SUVM_INTERSECT_IS_INCLUDED,
  SUVM_INTERSECT_PARTIAL
};

/* Callback invoked on suvm_volume_intersect_aabb invocation on each primitives
 * intersected by the submitted Axis Aligned Bounding Box */
typedef void
(*suvm_primitive_intersect_aabb_T)
  (const struct suvm_primitive* primitive, /* Intersected primitive */
   const double low[3], /* AABB lower bound */
   const double upp[3], /* AABB upper bound */
   void* context); /* User data */

/* Forward declaration of external data types */
struct logger;
struct mem_allocator;

/* Forward declaration of opaque data types */
struct suvm_device;
struct suvm_volume;

BEGIN_DECLS

/*******************************************************************************
 * Device API
 ******************************************************************************/
SUVM_API res_T
suvm_device_create
  (struct logger* logger, /* NULL <=> use default logger */
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity */
   struct suvm_device** suvm);

SUVM_API res_T
suvm_device_ref_get
  (struct suvm_device* dev);

SUVM_API res_T
suvm_device_ref_put
  (struct suvm_device* dev);

/*******************************************************************************
 * Volume mesh API
 ******************************************************************************/
SUVM_API res_T
suvm_tetrahedral_mesh_create
  (struct suvm_device* dev,
   const struct suvm_tetrahedral_mesh_args* args,
   struct suvm_volume** volume);

SUVM_API res_T
suvm_volume_ref_get
  (struct suvm_volume* volume);

SUVM_API res_T
suvm_volume_ref_put
  (struct suvm_volume* volume);

SUVM_API res_T
suvm_volume_get_aabb
  (const struct suvm_volume* volume,
   double lower[3],
   double upper[3]);

/* Return the primitive into which `pos' lies and the barycentric coordinates
 * of `pos' into this primitive. The returned primitive is SUVM_PRIMITIVE_NULL
 * if `pos' is not included into any volumic primitive. One can use the
 * SUVM_PRIMIIVE_NONE macro to check this case. */
SUVM_API res_T
suvm_volume_at
  (struct suvm_volume* volume,
   const double pos[3],
   struct suvm_primitive* prim, /* Geometric primitive where `pos' lies */
   double barycentric_coords[4]); /* `pos' into the primitive */

/* Iterate over the volumetric primitives intersecting the submitted Axis
 * Aligned Bounding Box and invoke the `clbk' function onto them. */
SUVM_API res_T
suvm_volume_intersect_aabb
  (struct suvm_volume* volume,
   const double low[3], /* AABB lower bound */
   const double upp[3], /* AABB upper bound */
   suvm_primitive_intersect_aabb_T clbk,
   void* context); /* User data sent as the last argument of clbk */

SUVM_API res_T
suvm_volume_get_primitives_count
  (const struct suvm_volume* volume,
   size_t* nprims);

SUVM_API res_T
suvm_volume_get_primitive
  (const struct suvm_volume* volume,
   const size_t iprim, /* In [0, suvm_volume_get_primitives_count[ */
   struct suvm_primitive* prim);

SUVM_API res_T
suvm_volume_compute_hash
  (const struct suvm_volume* volume,
   const int cpnt_mask, /* Combination of suvm_volume_cpnt_flag */
   hash256_T hash);

SUVM_API res_T
suvm_volume_get_mesh_desc
  (const struct suvm_volume* volume,
   struct suvm_mesh_desc* desc);

SUVM_API res_T
suvm_mesh_desc_compute_hash
  (const struct suvm_mesh_desc* desc,
   hash256_T hash);

/*******************************************************************************
 * Primitive API
 ******************************************************************************/
SUVM_API res_T
suvm_primitive_setup_polyhedron
  (const struct suvm_primitive* prim,
   struct suvm_polyhedron* poly);

SUVM_API enum suvm_intersection_type
suvm_polyhedron_intersect_aabb
  (const struct suvm_polyhedron* poly,
   const float low[3],
   const float upp[3]);

END_DECLS

#endif /* SUVM_H */

