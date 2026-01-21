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

#ifndef SVX_TREE_H
#define SVX_TREE_H

#include "svx_buffer.h"
#include <rsys/ref_count.h>

/* Current version the tree data structure. One should increment it and perform
 * a version management onto serialized data when the tree data structure is
 * updated. */
static const int SVX_TREE_VERSION = 0;

struct svx_tree {
  size_t definition; /* #voxels of the tree along its dimensions */

  /* Submitted AABB */
  double lower[3], upper[3];

  /* Adjusted World space AABB of the tree. The submitted AABB is increased to
   * encompass the whole tree voxels. The number of tree voxels might be
   * greater than the submitted voxels count in order to ensure that definition
   * is a power of two. */
  double tree_low[3], tree_upp[3];
  double tree_size[3]; /* World space size of the tree AABB */

  struct buffer buffer; /* Buffer of voxel data */
  struct buffer_index root; /* Index toward the children of the root */
  ALIGN(16) char root_attr[SVX_MAX_SIZEOF_VOXEL]; /* Attribute of the root */

  size_t nleaves; /* #leaves */
  size_t depth; /* Maximum depth of the tree */

  enum svx_axis frame[3]; /* Define the frame (i.e. basis) of the tree */
  enum svx_tree_type type;
  struct svx_device* dev;
  ref_T ref;
};

extern LOCAL_SYM res_T
tree_create
  (struct svx_device* dev,
   const enum svx_tree_type type,
   const size_t vxsz,
   struct svx_tree** out_tree);

extern LOCAL_SYM res_T
octree_trace_ray
  (struct svx_tree* oct,
   const double ray_origin[3],
   const double ray_direction[3],
   const double ray_range[2],
   const svx_hit_challenge_T challenge,
   const svx_hit_filter_T filter,
   void* context,
   struct svx_hit* hit);

extern LOCAL_SYM res_T
bintree_trace_ray
  (struct svx_tree* btree,
   const double ray_origin[3],
   const double ray_direction[3],
   const double ray_range[2],
   const svx_hit_challenge_T challenge,
   const svx_hit_filter_T filter,
   void* context,
   struct svx_hit* hit);

extern LOCAL_SYM res_T
tree_read
  (struct svx_tree* tree,
   FILE* stream);

#endif /* SVX_TREE_H */

