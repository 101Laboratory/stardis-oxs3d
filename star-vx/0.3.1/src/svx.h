/* Copyright (C) 2018, 2020-2025 |Méso|Star> (contact@meso-star.com)
 * Copyright (C) 2018 Université Paul Sabatier
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

#ifndef SVX_H
#define SVX_H

#include <rsys/rsys.h>
#include <float.h>

/* Library symbol management */
#if defined(SVX_SHARED_BUILD) /* Build shared library */
  #define SVX_API extern EXPORT_SYM
#elif defined(SVX_STATIC) /* Use/build static library */
  #define SVX_API extern LOCAL_SYM
#else /* Use shared library */
  #define SVX_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the svx function `Func'
 * returns an error. One should use this macro on svx function calls for
 * which no explicit error checking is performed */
#ifndef NDEBUG
  #define SVX(Func) ASSERT(svx_ ## Func == RES_OK)
#else
  #define SVX(Func) svx_ ## Func
#endif

/* Maximum memory size of a voxel */
#define SVX_MAX_SIZEOF_VOXEL (sizeof(double)*16)

enum svx_axis {
  SVX_AXIS_X,
  SVX_AXIS_Y,
  SVX_AXIS_Z,
  SVX_AXIS_NONE__
};

enum svx_tree_type {
  SVX_BINTREE,
  SVX_OCTREE
};

/* Volume element */
struct svx_voxel {
  double lower[3], upper[3]; /* AABB of the voxel */
  const void* data; /* Data of the voxel */
  size_t id; /* Indentifier of the voxel */
  size_t depth; /* Depth of the voxel, in [0, svx_octree_desc.depth[ */
  int is_leaf; /* Define if the voxel is a leaf into the hierarchy */
};

#define SVX_VOXEL_NULL__ {                                                     \
  { DBL_MAX, DBL_MAX, DBL_MAX},                                                \
  {-DBL_MAX,-DBL_MAX,-DBL_MAX},                                                \
  NULL, SIZE_MAX, SIZE_MAX, 0 }
static const struct svx_voxel SVX_VOXEL_NULL = SVX_VOXEL_NULL__;

#define SVX_VOXEL_EQ(V0, V1) ((V0)->id == (V1)->id)

#define SVX_VOXEL_NONE(Voxel) ((Voxel)->id == SVX_VOXEL_NULL.id)

/* Descriptor of a voxel */
struct svx_voxel_desc {
  /* Retrieve the data of the voxels. Star-VoXel ensures that the Voxels are
   * accessed according to the morton order where Z vary priorly to Y and Y
   * priorly to X. */
  void
  (*get)
    (const size_t xyz[3], /* Voxel coordinate in voxel space */
     const uint64_t mcode, /* Morton code of the voxel */
     void* dst, /* Where to store data */
     void* ctx); /* Pointer toward user data */

  /* Merge the data of N voxels */
  void
  (*merge)
    (void* dst, /* Merged data */
     const void* voxels[], /* Data to merge */
     const size_t nvoxels, /* #submitted data */
     void* ctx); /* Pointer toward user data */

  /* Check if the voxel's data can be merged. Note that the `id' field of the
   * submitted voxels is undefined since these voxels are temporaries. */
  int
  (*challenge_merge)
    (const struct svx_voxel voxels[], /* Voxels candidate to the merge */
     const size_t nvoxels, /* #candidates */
     void* ctx); /* Pointer toward user data */

  void* context; /* Client side data sent as the last argument of the clbbs */
  size_t size; /* Size in bytes of a voxel. Must be <= SVX_MAX_SIZEOF_VOXEL */
};

#define SVX_VOXEL_DESC_NULL__ { NULL, NULL, NULL, NULL, 0 }
static const struct svx_voxel_desc SVX_VOXEL_DESC_NULL =
  SVX_VOXEL_DESC_NULL__;

struct svx_tree_desc {
  /* Submitted Axis Aligned Bounding Box */
  double lower[3], upper[3];

  size_t nleaves; /* #leaves */
  size_t nvoxels; /* #voxels, i.e. #leaves + #parents */
  size_t depth; /* Depth of the octree */

  enum svx_tree_type type;

  /* Define the axis in world space along which the tree is defined. In 1D,
   * (i.e. bintree) only the first component of the frame is defined while in
   * 3D (i.e. octree), the 3 components is always defined to SVX_AXIS_X,
   * SVX_AXIS_Y and SVX_AXYS_Z. */
  enum svx_axis frame[3];
};

#define SVX_TREE_DESC_NULL__ {                                                 \
  { DBL_MAX, DBL_MAX, DBL_MAX},                                                \
  {-DBL_MAX,-DBL_MAX,-DBL_MAX},                                                \
  0, 0, 0, 0,                                                                  \
  {SVX_AXIS_NONE__, SVX_AXIS_NONE__, SVX_AXIS_NONE__}                          \
}
static const struct svx_tree_desc SVX_TREE_DESC_NULL =
  SVX_TREE_DESC_NULL__;

struct svx_hit {
  /* Distance from the ray origin the the voxel entry/exit point, respectively.
   * These distances are clamped against the ray-range. */
  double distance[2];
  struct svx_voxel voxel; /* Intersected voxel */
};

#define SVX_HIT_NULL__ {{DBL_MAX,-DBL_MAX}, SVX_VOXEL_NULL__}
static const struct svx_hit SVX_HIT_NULL = SVX_HIT_NULL__;

#define SVX_HIT_NONE(Hit) ((Hit)->distance[0] > (Hit)->distance[1])

/* Function to invoke on a leaf */
typedef void
(*svx_leaf_function_T)
  (const struct svx_voxel* leaf,
   const size_t ileaf, /* Identifier of the leaf in [0, #leafs[ */
   void* context);

/* Hit challenge data type. The caller can implement a function of this type to
 * control the traversal of the octree hierarchy. If the function returns 1,
 * the octree traversal will not go deeper into the hierarchy and the traversed
 * voxel will be treated as a leaf. Note that this function is not invoked on
 * intersected leaves */
typedef int
(*svx_hit_challenge_T)
  (const struct svx_hit* hit,
   const double ray_org[3],
   const double ray_dir[3],
   const double ray_range[2],
   void* context);

/* Hit filter function data type. The caller can define a function of this type
 * to control the treatment at each "leaf" intersected during the octree ray
 * tracing. An intersected voxel is considered as a "leaf" if it is really a
 * leaf, or if the svx_hit_challenge_T function returns a not null value. If
 * the filter function returns 0, the octree traversal is stopped while a value
 * !=0 lets the ray to pursue its traversal. Such functions can be used to
 * discard specific voxels, to accumulate voxels data, to list the traversed
 * voxels, etc. */
 typedef int
(*svx_hit_filter_T)
  (const struct svx_hit* hit,
   const double ray_org[3],
   const double ray_dir[3],
   const double ray_range[2],
   void* context); /* User data submitted on trace ray invocation */

/* At filter function data type. One can define such function to discard voxels
 * during the traversal of the octree with respect to user defined criteria,
 * eg, depth or size of the voxel, etc. Return 0 if the voxel is not discarded
 * and a value not equal to zero otherwise. */
typedef int
(*svx_at_filter_T)
  (const struct svx_voxel* voxel,
   const double position[3],
   void* context);

/* Forward declaration of external data types */
struct logger;
struct mem_allocator;

/* Forward declaration of opaque data types */
struct svx_device;
struct svx_tree;

BEGIN_DECLS

/*******************************************************************************
 * Device
 ******************************************************************************/
SVX_API res_T
svx_device_create
  (struct logger* logger,
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   struct svx_device** svx);

SVX_API res_T
svx_device_ref_get
  (struct svx_device* svx);

SVX_API res_T
svx_device_ref_put
  (struct svx_device* svx);

/*******************************************************************************
 * Tree
 ******************************************************************************/
SVX_API res_T
svx_octree_create
  (struct svx_device* dev,
   const double lower[3], /* Lower bound of the octree */
   const double upper[3], /* Upper bound of the octree */
   const size_t nvoxels[3], /* # voxels along the 3 axis */
   const struct svx_voxel_desc* desc, /* Descriptor of a voxel */
   struct svx_tree** octree);

SVX_API res_T
svx_bintree_create
  (struct svx_device* dev,
   const double lower, /* Lower bound of the bintree */
   const double upper, /* Upper bound of the bintree */
   const size_t nvoxels, /* #voxels along the range */
   const enum svx_axis axis, /* Axis along which the binary tree is defined */
   const struct svx_voxel_desc* desc, /* Descriptor of a voxel */
   struct svx_tree** tree);

SVX_API res_T
svx_tree_create_from_stream
  (struct svx_device* dev,
   FILE* stream,
   struct svx_tree** tree);

SVX_API res_T
svx_tree_ref_get
  (struct svx_tree* tree);

SVX_API res_T
svx_tree_ref_put
  (struct svx_tree* tree);

SVX_API res_T
svx_tree_get_desc
  (const struct svx_tree* tree,
   struct svx_tree_desc* desc);

SVX_API res_T
svx_tree_for_each_leaf
  (struct svx_tree* tree,
   svx_leaf_function_T functor,
   void* context); /* Client data sent as the last argument of the functor */

SVX_API res_T
svx_tree_trace_ray
  (struct svx_tree* tree,
   const double ray_origin[3],
   const double ray_direction[3], /* Must be normalized */
   const double ray_range[2],
   const svx_hit_challenge_T challenge, /* NULL <=> Traversed up to the leaves */
   const svx_hit_filter_T filter, /* NULL <=> Stop RT at the 1st hit voxel */
   void* context, /* Data sent to the filter functor */
   struct svx_hit* hit);

SVX_API res_T
svx_tree_at
  (struct svx_tree* tree,
   const double position[3],
   svx_at_filter_T filter,
   void* context, /* Client data sent as the last argument of the filter func */
   struct svx_voxel* voxel);

SVX_API res_T
svx_tree_write
  (const struct svx_tree* tree,
   FILE* stream);

#endif /* SVX_H */

