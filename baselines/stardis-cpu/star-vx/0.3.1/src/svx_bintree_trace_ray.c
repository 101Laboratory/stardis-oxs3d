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

#include "svx.h"
#include "svx_tree.h"

#include <rsys/double3.h>

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static FINLINE void
setup_hit
  (struct svx_tree* btree,
   const struct buffer_index iattr, /* Index toward the voxel attributes */
   const double distance_min, /* Dst from ray org to the voxel entry point */
   const double distance_max, /* Dst from ray_org to the voxel exit point */
   const double lower, /* Lower bound of the current voxel in [0, 1] space */
   const double scale_exp2, /* Size of the voxel in [0, 1] space */
   const size_t depth, /* Depth of the voxel in the octree hierarchy */
   const int is_leaf, /* Define if the voxel is a leaf or not */
   const int flip, /* Define if the btree was reverted or not */
   struct svx_hit* hit)
{
  enum svx_axis axis;
  double low;
  double upp;
  ASSERT(btree && hit);
  ASSERT(distance_min >= 0 && distance_min <= distance_max);

  hit->distance[0] = distance_min;
  hit->distance[1] = distance_max;
  hit->voxel.data = buffer_get_attr(&btree->buffer, iattr);
  hit->voxel.depth = depth,
  hit->voxel.id = buffer_absolute_attr_index(&btree->buffer, iattr);
  hit->voxel.is_leaf = is_leaf;

  /* Transform the voxel aabb in the [0, 1]^3 normalized space and flip if
   * necessary */
  low = flip ? 1 - lower - scale_exp2 : lower;
  upp = low + scale_exp2;

  /* Transform the voxel AABB in world space */
  axis = btree->frame[0];
  d3_splat(hit->voxel.lower, -INF);
  d3_splat(hit->voxel.upper,  INF);
  hit->voxel.lower[axis] = low * btree->tree_size[axis] + btree->tree_low[axis];
  hit->voxel.upper[axis] = upp * btree->tree_size[axis] + btree->tree_low[axis];
}

/*******************************************************************************
 * Exported function
 ******************************************************************************/
res_T
bintree_trace_ray
  (struct svx_tree* btree,
   const double ray_org[3],
   const double ray_dir[3],
   const double ray_range[2],
   const svx_hit_challenge_T challenge,
   const svx_hit_filter_T filter,
   void* context,
   struct svx_hit* hit)
{
  #define STACK_LENGTH 23 /* #mantissa bits */
  struct stack_entry {
    float scale_exp2;
    uint32_t depth;
    struct buffer_index inode;
  };
  ALIGN(64) struct stack_entry stack[STACK_LENGTH];
  STATIC_ASSERT(sizeof(struct stack_entry) == 16, Unexpected_sizeof_stack_entry);

  struct buffer_index inode;
  size_t istack; /* Top of the stack */
  size_t iaxis; /* Id in [0, 2] of the axis along which the tree is defined */
  double rdir[3]; /* Ray direction adjusted wrt numerical problems */
  double pos_min, pos_max; /* Min/Max pos along the ray in [0,1] +dir 1D space */
  double org; /* Ray origin in the [0,1] +dir 1D space */
  double dir; /* Ray direction in the [0,1] +dir 1D space */
  double ts; /* 1/(Ray direction) in the [0,1] +dir 1D space */
  double rcp_btreesz; /* Reciprocal of the binary tree size */
  double low; /* Lower bound of the traversed node */
  float scale_exp2; /* Current size of a node in the [0,1] +dir 1D space */
  uint32_t depth; /* Depth of the traversed nodes */
  int ichild; /* Traversed child */
  int flip = 0; /* Is the ray flipped? */
  int null_dir = 0; /* Is the 1D dir is roughly null? */

  if(!btree || !ray_org || !ray_dir || !ray_range || btree->type != SVX_BINTREE
  || !d3_is_normalized(ray_dir) || !hit) {
    return RES_BAD_ARG;
  }

  *hit = SVX_HIT_NULL;
  if(ray_range[0] >= ray_range[1]) { /* Disabled ray */
    return RES_OK;
  }

  iaxis = btree->frame[0];
  rcp_btreesz = 1.0 / (double)btree->tree_size[iaxis];
  ASSERT(rcp_btreesz > 0);

  d3_set(rdir, ray_dir);

 /* Define whether the direction of the 1D ray is approximately aligned with the
  * infinite dimension. If it is, make sure it's really aligned so that the
  * caller can't have a ray that escapes the voxel from which the ray starts.
  * Indeed, even with an approximately zero direction along the 1D axis, the ray
  * can still cross the boundary of a voxel if the caller decides to make it
  * travel long distances. */
  null_dir = eq_eps(ray_dir[iaxis], 0, 1.e-6);
  if(null_dir) {
    rdir[iaxis] = 0;
    d3_normalize(rdir, rdir);
  }

  /* Transform the ray origin in [0, 1] space  */
  org = (ray_org[iaxis] - btree->tree_low[iaxis]) * rcp_btreesz;
  /* Transform the direction in the normalized bintree space */
  dir = (rdir[iaxis] * rcp_btreesz);

  /* The ray starts outside the binary tree and point outward the bin tree: it
   * cannot intersect the binary tree */
  if((org > 1 && dir >= 0)
  || (org < 0 && dir <= 0))
    return RES_OK;

  /* Mirror rays with negative direction */
  if(dir < 0) {
    flip = 1;
    org = 1 - org;
    dir = -dir;
  }

  /* Let a ray r defined as O + tD with O the ray origin and D the direction;
   * and X an axis aligned plane. r intersects X at a distance t computed as
   * below :
   *    t = (X-O) / D
   * Note that one can transform r in r' with a transform M without any impact
   * on the t parameter:
   *    M.X = M.O * t(M.D)
   *    t = (M.X-M.O)/(M.D) = (M.X-M.O)*ts; with ts = 1/(M.D) */
  ts = null_dir ? INF : 1.f / dir;

  /* Compute the range in [0, 1] of the ray/binary tree intersection */
  pos_min = MMAX(dir * ray_range[0] + org, 0);
  pos_max = MMIN(dir * ray_range[1] + org, 1);

  /* Challenge the root node */
  if(challenge) {
    struct svx_hit hit_root;
    const double t_min = null_dir ? ray_range[0] : (pos_min - org) * ts;
    const double t_max = null_dir ? ray_range[1] : (pos_max - org) * ts;
    struct buffer_index iattr_dummy = buffer_get_child_attr_index
      (&btree->buffer, btree->root, 0/*arbitrarly child index*/);

    /* Use the regular setup_hit procedure by providing a dummy attribute
     * index, and then overwrite the voxel data with the root one */
    setup_hit(btree, iattr_dummy, t_min, t_max, 0.f/*low*/, 1.f/*scale_exp2*/,
      0/*depth*/, 0/*is_leaf*/, flip, &hit_root);
    hit_root.voxel.data = btree->root_attr;

    if(challenge(&hit_root, ray_org, rdir, ray_range, context)) {
      if(!filter /* By default, i.e. with no filter, stop the traversal */
      || !filter(&hit_root, ray_org, rdir, ray_range, context)) {
        *hit = hit_root;
        return RES_OK; /* Do not traverse the binary tree */
      }
    }
  }

  /* Define the first traversed child and set its lower bound */
  if(pos_min <= 0.5) {
    /* Note that we use less than or *equal* in the previous test to be
     * consistent with the svx_tree_at function: a position onto a boundary is
     * owned by the node before the boundary */
    ichild = 0;
    low = 0.0;
  } else {
    ichild = 1;
    low = 0.5;
  }

  /* Init the traversal */
  depth = 1;
  istack = 0;
  scale_exp2 = 0.5f;
  inode = btree->root;

  /* Here we go */
  while(pos_min < pos_max) {
    const struct buffer_xnode* node = buffer_get_node(&btree->buffer, inode);
    const int ichild_adjusted = ichild ^ flip;
    const int ichild_flag = BIT(ichild_adjusted);

    if(node->is_valid & ichild_flag) {
      const int is_leaf = (node->is_leaf & ichild_flag) != 0;
      int go_deeper = 1;

      if(is_leaf || challenge) {
        struct svx_hit hit_tmp;
        const double upp = MMIN(low + scale_exp2, pos_max) ;
        const double t_min = null_dir ? ray_range[0] : (pos_min - org) * ts;
        const double t_max = null_dir ? ray_range[1] : (upp - org) * ts;
        const struct buffer_index iattr = buffer_get_child_attr_index
          (&btree->buffer, inode, ichild_adjusted);
        ASSERT(t_min <= t_max);

        setup_hit(btree, iattr, t_min, t_max, low, scale_exp2, depth, is_leaf,
          flip, &hit_tmp);

        if(is_leaf
        || challenge(&hit_tmp, ray_org, rdir, ray_range, context)) {
          go_deeper = 0;
          /* Stop the traversal if no filter is defined or if the filter
           * function returns 0 */
          if(!filter /* By default, i.e. with no filter, stop the traversal */
          || !filter(&hit_tmp, ray_org, rdir, ray_range, context)) {
            *hit = hit_tmp;
            break;
          }

          /* Stop traversal if no more voxel can be traversed */
          if(null_dir) break;
        }
      }

      if(go_deeper) {
        const float scale_exp2_child = scale_exp2 * 0.5f;
        const double child_mid = low + scale_exp2_child;

        /* Push parent node if necessary */
        if(ichild == 0 && !null_dir) {
          stack[istack].inode = inode;
          stack[istack].scale_exp2 = scale_exp2;
          stack[istack].depth = depth;
          ++istack;
        }

        /* Get the node index of the traversed child */
        inode = buffer_get_child_node_index
          (&btree->buffer, inode, (int)ichild_adjusted);

        ichild = pos_min > child_mid;
        if(ichild == 1) low = child_mid;
        scale_exp2 = scale_exp2_child;
        ++depth;
        continue;
      }
    }

    /* Purse traversal */
    ASSERT(pos_min >= low && pos_min <= low + scale_exp2);
    low += scale_exp2; /* Lower bound of the next node to traverse */
    pos_min = low; /* Snap the pos_min to the next node lower bound */
    ++ichild;

    if(ichild > 1) { /* No more child to traverse in this node => Pop node */
      if(istack == 0) break; /* No more node to traverse */

      /* Pop node */
      --istack;
      inode = stack[istack].inode;
      scale_exp2 = stack[istack].scale_exp2;
      depth = stack[istack].depth;

      /* A node is pushed on the stack if one of its child was not traversed.
       * Furthermore, this intersector ensures that the ray dir is alwas
       * positive along the axis of the binary tree.  As a consequence, the
       * child 0 is always traversed first and the remaining child to traverse
       * of a pushed node is thus always the child 1 */
      ichild = 1;
    }
  }
  return RES_OK;
}
