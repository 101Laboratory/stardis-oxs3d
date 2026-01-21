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

#ifndef TREE_DIMENSION

#ifndef SVX_TREE_GENERIC_FUNC_H
#define SVX_TREE_GENERIC_FUNC_H

#include "svx.h"
#include "svx_tree.h"
#include "svx_tree_builder.h" /* For the TREE_DEPTH_MAX constant */

#endif /* SVX_TREE_GENERIC_FUNC_H */
#else /*!TREE_DIMENSION */

#ifdef COMPILER_GCC
  #pragma GCC push_options
  #pragma GCC optimize("unroll-loops")
#endif

#if TREE_DIMENSION == 1
  #define TREE_FUNC(Func) CONCAT(bintree_, Func)
  #define SVX_TREE_TYPE SVX_BINTREE
#elif TREE_DIMENSION == 3
  #define TREE_FUNC(Func) CONCAT(octree_, Func)
  #define SVX_TREE_TYPE SVX_OCTREE
#else
  #error "Invalid TREE_DIMENSION value"
#endif

#define NCHILDREN BIT(TREE_DIMENSION)

static res_T
TREE_FUNC(for_each_leaf)
  (struct svx_tree* oct, svx_leaf_function_T func, void* ctx)
{
  struct stack_entry {
    struct buffer_index inode;
    size_t depth;
    double low[TREE_DIMENSION];
    double hsz[TREE_DIMENSION]; /* Half size */
  } stack[TREE_DEPTH_MAX*NCHILDREN];
  int istack;
  struct svx_voxel leaf = SVX_VOXEL_NULL;
  size_t ileaf = 0;
  int i;

  if(!oct || !func || oct->type != SVX_TREE_TYPE) return RES_BAD_ARG;

  stack[0].depth = 0;
  stack[0].inode = oct->root;

  FOR_EACH(i, 0, TREE_DIMENSION) {
    stack[0].low[i] =  oct->tree_low[oct->frame[i]];
    stack[0].hsz[i] =
      (oct->tree_upp[oct->frame[i]] - oct->tree_low[oct->frame[i]])*0.5;
  }
  istack = 1;

  do {
    const struct stack_entry entry = stack[--istack];
    const size_t child_depth = entry.depth + 1;
    double child_hsz[TREE_DIMENSION];
    double mid[TREE_DIMENSION]; /* Middle point of the current node */
    struct buffer_xnode* node;
    int ichild;

    node = buffer_get_node(&oct->buffer, entry.inode);

    FOR_EACH(i, 0, TREE_DIMENSION) {
      mid[i] = entry.low[i] + entry.hsz[i];
      child_hsz[i] = entry.hsz[i] * 0.5;
    }

    FOR_EACH(ichild, 0, NCHILDREN) {
      const uint8_t ichild_flag = (uint8_t)BIT(ichild);
      double low[TREE_DIMENSION];

      if((node->is_valid & ichild_flag) == 0) continue; /* Empty node */

      FOR_EACH(i, 0, TREE_DIMENSION) {
        low[i] = ichild & BIT(TREE_DIMENSION-1-i) ? mid[i] : entry.low[i];
      }

      if(node->is_leaf & ichild_flag) {
        struct buffer_index iattr;

        iattr = buffer_get_child_attr_index
          (&oct->buffer, entry.inode, ichild);

        FOR_EACH(i, 0, TREE_DIMENSION) {
          leaf.lower[oct->frame[i]] = low[i];
          leaf.upper[oct->frame[i]] = low[i] + entry.hsz[i];
        }
        leaf.data = buffer_get_attr(&oct->buffer, iattr);
        leaf.id = buffer_absolute_attr_index(&oct->buffer, iattr);
        leaf.depth = child_depth;
        leaf.is_leaf = 1;

        func(&leaf, ileaf++, ctx);
      } else {
        struct stack_entry* top = stack + istack;

        top->inode = buffer_get_child_node_index
          (&oct->buffer, entry.inode, ichild);
        FOR_EACH(i, 0, TREE_DIMENSION) {
          top->low[i] = low[i];
          top->hsz[i] = child_hsz[i];
        }
        top->depth = child_depth;
        ++istack;
      }
    }
  } while(istack);

  return RES_OK;
}

static res_T
TREE_FUNC(at)
  (struct svx_tree* tree,
   const double position[3],
   svx_at_filter_T filter,
   void* context,
   struct svx_voxel* voxel)
{
  struct buffer_index inode;
  struct buffer_index iattr;
  struct svx_voxel vox = SVX_VOXEL_NULL;
  double scale_exp2;
  double low[TREE_DIMENSION];
  double pos[TREE_DIMENSION];
  int i;
  res_T res = RES_OK;

  if(!tree || !position || !voxel || tree->type != SVX_TREE_TYPE) {
    res = RES_BAD_ARG;
    goto error;
  }

  *voxel = SVX_VOXEL_NULL;

  /* The position is outside the octree */
  FOR_EACH(i, 0, TREE_DIMENSION) {
    if(position[tree->frame[i]] > tree->upper[tree->frame[i]]
    || position[tree->frame[i]] < tree->lower[tree->frame[i]])
      goto exit;
  }

  FOR_EACH(i, 0, TREE_DIMENSION) {
    /* Transform the position in the normalized octree space,
     * i.e. octree lies in [0, 1]^2 */
    pos[i] = (position[tree->frame[i]] - tree->tree_low[tree->frame[i]])
          / tree->tree_size[tree->frame[i]];
    /* Initialized the lower left corner of the current node */
    low[i] = 0;
  }

  /* Root voxel */
  vox.depth = 0;
  vox.is_leaf = 0;
  FOR_EACH(i, 0, TREE_DIMENSION) {
    vox.lower[tree->frame[i]] = tree->lower[tree->frame[i]];
    vox.upper[tree->frame[i]] = tree->upper[tree->frame[i]];
  }
  if(filter) {
    vox.data = tree->root_attr;
    vox.id = buffer_absolute_attr_index(&tree->buffer, tree->buffer.attr_head);
    if(!filter(&vox, position, context)) { *voxel = vox; goto exit; }
  }

  scale_exp2 = 0.5;
  inode = tree->root;
  for(;;) {
    struct buffer_xnode* node = buffer_get_node(&tree->buffer, inode);
    int ichild;
    uint8_t ichild_flag;
    double mid[TREE_DIMENSION];

    ++vox.depth;

    /* Compute the middle point of the node */
    FOR_EACH(i, 0, TREE_DIMENSION) mid[i] = low[i] + scale_exp2;

    /* Define the child of the current node into which pos `lies' and compute
     * its lower left corner */
    ichild = 0;
    FOR_EACH(i, 0, TREE_DIMENSION) {
      if(pos[i] > mid[i]) { ichild |= BIT(TREE_DIMENSION-1-i); low[i] = mid[i]; }
    }

    ichild_flag = (uint8_t)BIT(ichild);
    if((node->is_valid & ichild_flag) == 0) break; /* Empty node */

    vox.is_leaf = (node->is_leaf & ichild_flag) != 0;
    if(filter || vox.is_leaf) {
      iattr = buffer_get_child_attr_index(&tree->buffer, inode, ichild);

      /* Setup the voxel */
      FOR_EACH(i, 0, TREE_DIMENSION) {
        const int iaxis = tree->frame[i];
        vox.lower[iaxis] = low[i] * tree->tree_size[iaxis] + tree->tree_low[iaxis];
        vox.upper[iaxis] = vox.lower[iaxis] + tree->tree_size[iaxis] * scale_exp2;
      }
      vox.data = buffer_get_attr(&tree->buffer, iattr);
      vox.id = buffer_absolute_attr_index(&tree->buffer, iattr);
      vox.is_leaf = (node->is_leaf & ichild_flag) != 0;

      if(vox.is_leaf || !filter(&vox, position, context)) {
        *voxel = vox;
        break;
      }
    }

    inode = buffer_get_child_node_index(&tree->buffer, inode, ichild);
    scale_exp2 *= 0.5;
  }

exit:
  return res;
error:
  goto exit;
}

#undef TREE_FUNC
#undef SVX_TREE_TYPE
#undef TREE_DIMENSION
#undef NCHILDREN

#ifdef COMPILER_GCC
  #pragma GCC pop_options
#endif

#endif /* !TREE_DIMENSION */
