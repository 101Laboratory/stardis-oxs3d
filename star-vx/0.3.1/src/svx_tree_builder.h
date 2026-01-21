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

#ifndef SVX_TREE_BUILDER_H
#define SVX_TREE_BUILDER_H

#include "svx_buffer.h"
#include <rsys/double3.h>
#include <rsys/morton.h>

#define TREE_DEPTH_MAX 16 /* Maximum depth of a tree */

struct voxel {
  uint64_t mcode; /* Morton code of the voxel */
  void* data; /* Data of the voxel */
};
static const struct voxel VOXEL_NULL = {0, NULL};

#endif /*  SVX_TREE_BUILDER_H */
#else /* TREE_DIMENSION */

#if (TREE_DIMENSION<=0) || (TREE_DIMENSION>3)
  #error "Invalid TREE_DIMENSION value"
#endif

#if   TREE_DIMENSION == 1
  #define XD(Name) CONCAT(bintree_, Name)
#elif TREE_DIMENSION == 2
  #define XD(Name) CONCAT(quadtree_, Name)
#elif TREE_DIMENSION == 3
  #define XD(Name) CONCAT(octree_, Name)
#else
  #error "Invalid TREE_DIMENSION value"
#endif

#define NCHILDREN BIT(TREE_DIMENSION)

#ifdef COMPILER_CL
  #pragma warning(push)
  #pragma warning(disable:4324) /* Structure was padded due to alignment */
#endif

struct XD(node) {
  struct buffer_index ichild_node; /* Index of the 1st child node */
  struct buffer_index ichild_attr; /* Index of the 1st child attr */
  uint8_t is_valid; /* Mask defining whether the children are valid or not */
  uint8_t is_leaf; /* Mask defining whether the children are leaves or not */
  ALIGN(16) char data[NCHILDREN][SVX_MAX_SIZEOF_VOXEL]; /* Data of the leaves */
};

#ifdef COMPILER_CL
  #pragma warning(pop)
#endif

/* Stacked children of a tree node */
struct XD(stack) {
  struct XD(node) nodes[NCHILDREN]; /* List of registered children */
  uint8_t mask; /* Mask of valid children nodes (0 = empty) */
};

struct XD(builder) {
  struct XD(stack) stacks[TREE_DEPTH_MAX];
  struct buffer* buffer;

  double lower[3]; /* Tree lower bound in world space */
  double upper[3]; /* Tree upper bound in world space */
  double voxsz[3]; /* Size of the finest voxel in world space */
  enum svx_axis frame[3];

  const struct svx_voxel_desc* desc;
  size_t nleaves; /* Number of emitted leaves */

  int tree_depth; /* Maximum tree desc */
  int non_empty_lvl; /* Index of the 1st non empty level */

  uint64_t mcode; /* Morton code of the voxel */
};

/*******************************************************************************
 * Stack functions
 ******************************************************************************/
static INLINE void
XD(stack_clear)(struct XD(stack)* stack)
{
  int inode;
  FOR_EACH(inode, 0, NCHILDREN) {
    int ichild;
    stack->nodes[inode].is_leaf = 0;
    stack->nodes[inode].is_valid = 0;
    stack->nodes[inode].ichild_node = BUFFER_INDEX_NULL;
    stack->nodes[inode].ichild_attr = BUFFER_INDEX_NULL;
    FOR_EACH(ichild, 0, NCHILDREN) {
      memset(stack->nodes[inode].data[ichild], 0, SVX_MAX_SIZEOF_VOXEL);
    }
  }
  stack->mask = 0;
}

/* Build a parent tree node from the registered stack nodes */
static INLINE void
XD(stack_setup_node)
  (struct XD(stack)* stack,
   struct XD(node)* node,
   const double node_low[3], /* World space node lower bound */
   const double node_sz[3], /* World space node size */
   const size_t node_depth, /* Depth of the node */
   const enum svx_axis frame[3],
   const struct svx_voxel_desc* desc)
{
  double child_sz[3];
  double grandchild_sz[3];
  int ichild;
  int i;
  ASSERT(stack && node && check_svx_voxel_desc(desc));
  ASSERT(node_low && node_sz);
  ASSERT(node_sz[0] > 0 && node_sz[1] > 0 && node_sz[2] > 0);
  ASSERT(frame);

  node->ichild_node = BUFFER_INDEX_NULL;
  node->ichild_attr = BUFFER_INDEX_NULL;
  node->is_valid = stack->mask;
  node->is_leaf = 0;

  if(stack->mask == 0) return; /* Empty stack */

  d3_splat(child_sz, INF);
  d3_splat(grandchild_sz, INF);
  FOR_EACH(i, 0, TREE_DIMENSION) {
    child_sz[frame[i]] = node_sz[frame[i]] * 0.5f;
    grandchild_sz[frame[i]] = node_sz[frame[i]] * 0.25f;
  }

  /* Try to merge the child's leaves */
  FOR_EACH(ichild, 0, NCHILDREN) {
    const void* data[NCHILDREN];
    struct svx_voxel voxels[NCHILDREN];
    double child_low[3];
    const uint8_t ichild_flag = (uint8_t)BIT(ichild);
    struct XD(node)* child = stack->nodes + ichild;
    int igrandchild;
    size_t ngrandchildren = 0; /* #active grand children */

    d3_splat(child_low, -INF);
    FOR_EACH(i, 0, TREE_DIMENSION) {
      const int iaxis = frame[i];
      child_low[iaxis] = node_low[iaxis];
      if(ichild & (NCHILDREN >> (1+i))) child_low[iaxis] += child_sz[iaxis];
    }

    if(!(stack->mask & ichild_flag)) continue; /* Empty child */

    /* Fetch the grandchildren data */
    FOR_EACH(igrandchild, 0, NCHILDREN) {
      struct svx_voxel* vox = &voxels[igrandchild];
      const uint8_t igrandchild_flag = (uint8_t)BIT(igrandchild);

      if(!(child->is_valid & igrandchild_flag)) continue; /* Empty grandchild */

      voxels[ngrandchildren].data = child->data[igrandchild];
      voxels[ngrandchildren].depth = node_depth + 2;
      voxels[ngrandchildren].id = SIZE_MAX;
      voxels[ngrandchildren].is_leaf = (child->is_leaf & igrandchild_flag)!=0;
      voxels[ngrandchildren].data = child->data[igrandchild];
      data[ngrandchildren] = child->data[igrandchild];

      d3_splat(vox->lower,-INF);
      d3_splat(vox->upper, INF);
      FOR_EACH(i, 0, TREE_DIMENSION) {
        const int iaxis = frame[i];
        vox->lower[iaxis] = child_low[iaxis];
        if(igrandchild & (NCHILDREN >> (1+i))) {
          vox->lower[iaxis] += grandchild_sz[iaxis];
        }
        vox->upper[iaxis] = vox->lower[iaxis] + grandchild_sz[iaxis];
      }

      ++ngrandchildren;
    }

    desc->merge(node->data[ichild], data, ngrandchildren, desc->context);

    if(child->is_leaf == (BIT(NCHILDREN)-1)/*all active bitmask*/
    && desc->challenge_merge(voxels, ngrandchildren, desc->context)) {
      /* The node becomes a leaf: the children does not exist anymore */
      node->is_leaf |= ichild_flag;
      stack->mask ^= ichild_flag;
    }
  }
}

static res_T
XD(stack_write)
  (struct XD(stack)* stack, /* Node to write */
   struct buffer* buf, /* Buffer where nodes are written */
   struct buffer_index* out_index, /* Index of the first written node */
   size_t* out_nleaves) /* #writen leaves */
{
  struct buffer_index nodes_id = BUFFER_INDEX_NULL;
  struct XD(node)* node = NULL;
  size_t nleaves = 0;
  int inode;
  res_T res = RES_OK;
  ASSERT(stack && buf && out_index && out_nleaves);

  /* No registered nodes, this means that the nodes were merged in an higher
   * level */
  if(!stack->mask) goto exit;

  /* Write the attrib of the children */
  FOR_EACH(inode, 0, NCHILDREN) {
    char* data = NULL;
    size_t nattrs = 0;
    size_t nvoxs = 0;
    int ichild = 0;

    if((stack->mask & BIT(inode)) == 0) continue; /* Empty node */

    node = stack->nodes + inode;

    nattrs =  (size_t)popcount(node->is_valid);
    ASSERT(nattrs > 0);

    res = buffer_alloc_attrs(buf, nattrs, &node->ichild_attr);
    if(res != RES_OK) goto error;

    data = buffer_get_attr(buf, node->ichild_attr);
    nvoxs = 0;
    FOR_EACH(ichild, 0, NCHILDREN) {
      if(!(node->is_valid & BIT(ichild))) continue;
      memcpy(data + nvoxs*buf->voxsize, node->data[ichild], buf->voxsize);
      ++nvoxs;
    }
    ASSERT(nvoxs == nattrs);
    nleaves += (size_t)popcount(node->is_leaf);
  }

  do {
    struct buffer_index index = BUFFER_INDEX_NULL;
    struct buffer_xnode* xnodes = NULL;
    const size_t nnodes = (size_t)popcount(stack->mask);
    size_t ixnode = 0;

    /* Alloc the tree nodes */
    res = buffer_alloc_nodes(buf, (size_t)nnodes, &nodes_id);
    if(res != RES_OK) goto error;
    xnodes = buffer_get_node(buf, nodes_id);

    FOR_EACH(inode, 0, NCHILDREN) {
      uint16_t attr_offset = UINT16_MAX;
      uint16_t node_offset = UINT16_MAX;

      if((stack->mask & BIT(inode)) == 0) continue; /* Empty node */
      node = stack->nodes + inode;

      /* Setup the offset toward the children and children attribs */
      if(node->ichild_node.ipage == nodes_id.ipage) {
        node_offset = node->ichild_node.inode;
      }
      /* The page id of the children is not the same as that of node */
      if(node_offset > BUFFER_XNODE_MAX_CHILDREN_OFFSET) {
        res = buffer_alloc_far_index(buf, &index);
        if(res != RES_OK) break;
        *buffer_get_far_index(buf, index) = node->ichild_node;
        node_offset = BUFFER_XNODE_FLAG_FAR_INDEX | index.inode;
      }

      /* Setup the offset toward the children attribs */
      if(node->ichild_attr.ipage == nodes_id.ipage) {
        attr_offset = node->ichild_attr.inode;
      }

      /* The page id of the attribs is not tthe same as that of node */
      if(attr_offset > BUFFER_XNODE_FLAG_FAR_INDEX) {
        res = buffer_alloc_far_index(buf, &index);
        if(res != RES_OK) break;
        *buffer_get_far_index(buf, index) = node->ichild_attr;
        attr_offset = BUFFER_XNODE_FLAG_FAR_INDEX | index.inode;
      }

      xnodes[ixnode].node_offset = node_offset;
      xnodes[ixnode].attr_offset = attr_offset;
      xnodes[ixnode].is_valid = node->is_valid;
      xnodes[ixnode].is_leaf = node->is_leaf;
      ++ixnode;
    }
  /* inode < NCHILDREN <=> not enough memory in the current page. A far index
   * could not be stored in the same page of its associated node. The write
   * process was stoped. Rewrite the whole stacked nodes in a new page. */
  } while(inode < NCHILDREN);


exit:
  /* Return the index toward the first writen nodes */
  *out_index = nodes_id;
  *out_nleaves = nleaves;
  return res;
error:
  goto exit;
}


/*******************************************************************************
 * Builder functions
 ******************************************************************************/
static res_T
XD(builder_init)
  (struct XD(builder)* bldr,
   const size_t definition,
   const double lower[3], /* Lower bound of the tree */
   const double upper[3], /* Upper bound of the tree */
   const enum svx_axis frame[3],
   const struct svx_voxel_desc* desc,
   struct buffer* buffer)
{
  int ilvl, i;
  res_T res = RES_OK;
  ASSERT(bldr && IS_POW2(definition) && check_svx_voxel_desc(desc));
  ASSERT(lower && upper && frame);
  memset(bldr, 0, sizeof(struct XD(builder)));

  /* Compute the maximum depth of the tree */
  bldr->tree_depth = log2i((int)definition);
  if(bldr->tree_depth > TREE_DEPTH_MAX) {
    res = RES_MEM_ERR;
    goto error;
  }

  /* Init the per tree level stack */
  FOR_EACH(ilvl, 0, bldr->tree_depth) {
    XD(stack_clear)(&bldr->stacks[ilvl]);
  }

  buffer_clear(buffer);
  bldr->nleaves = 0;
  bldr->desc = desc;
  bldr->buffer = buffer;
  bldr->non_empty_lvl = bldr->tree_depth - 1;
  bldr->frame[0] = frame[0];
  bldr->frame[1] = frame[1];
  bldr->frame[2] = frame[2];
  d3_splat(bldr->lower,-INF);
  d3_splat(bldr->upper, INF);
  d3_splat(bldr->voxsz, INF);

  FOR_EACH(i, 0, TREE_DIMENSION) {
    const int iaxis = frame[i];
    bldr->lower[iaxis] = lower[iaxis];
    bldr->upper[iaxis] = upper[iaxis];
    bldr->voxsz[iaxis] = (upper[iaxis] - lower[iaxis]) / (double)definition;
  }

exit:
  return res;
error:
  goto exit;
}

static res_T
XD(builder_add_voxel)
  (struct XD(builder)* bldr,
   const struct voxel* vox)
{
  uint64_t mcode_xor;
  int inode;
  int ichild;
  uint8_t ichild_flag;
  res_T res = RES_OK;
  ASSERT(bldr && vox && vox->mcode >= bldr->mcode);

  /* Define if the bits in [4 .. 63] of the previous and the next Morton
   * codes are the same */
  mcode_xor = bldr->mcode ^ vox->mcode;

  /* The next voxel is not in the current node */
  if(mcode_xor >= NCHILDREN) {
    size_t ilvl;

    inode = (bldr->mcode >> TREE_DIMENSION) & (NCHILDREN-1);
    bldr->stacks[0].mask |= (uint8_t)BIT(inode);

    /* Flush the stack of the tree level that does not contain the next voxel.
     * The last tree level is actually the level that contains the root node
     * while the penultimate describes the root node itself. These 2 levels
     * contain all the voxels and can be skipped */
    FOR_EACH(ilvl, 0, (size_t)bldr->tree_depth-2/*The 2 last voxels contain all voxels*/) {
      uint32_t parent_coords[3] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
      double parent_sz[3];
      double parent_low[3];
      size_t parent_depth;
      uint64_t parent_mcode;
      struct XD(node)* parent_node;
      uint64_t mcode_max_lvl;
      size_t nleaves;
      int i;

      /* Compute the maximum morton code value for the current tree level */
      mcode_max_lvl =
        NCHILDREN/*#children*/
      * (1lu<<(TREE_DIMENSION*(ilvl+1)))/*#voxels per children*/;

      if(mcode_xor < mcode_max_lvl) break;

      /* Compute the parent node attribute */
      parent_mcode = bldr->mcode >> (TREE_DIMENSION * (ilvl+2));
      d3_splat(parent_sz, INF);
      d3_splat(parent_low, -INF);
      FOR_EACH(i, 0, TREE_DIMENSION) {
        const int iaxis = bldr->frame[i];
        parent_sz[iaxis] = bldr->voxsz[iaxis] * (double)(1 << (ilvl+2));
        parent_coords[iaxis] = morton3D_decode_u21
          (parent_mcode >> (TREE_DIMENSION-1-i));
        parent_low[iaxis] =
          parent_coords[iaxis] * parent_sz[iaxis] + bldr->lower[iaxis];
      }

      parent_depth = (size_t)bldr->tree_depth - 2 - ilvl;
      ASSERT(parent_depth <= (size_t)bldr->tree_depth-2);

      /* Retrieve the node index of the next level */
      inode = (int)parent_mcode & (NCHILDREN-1);

      /* The next voxel is not in the ilvl^th stack. Setup the parent node of the
       * nodes registered into the stack */
      parent_node = &bldr->stacks[ilvl+1].nodes[inode];
      XD(stack_setup_node)(&bldr->stacks[ilvl], parent_node, parent_low,
        parent_sz, parent_depth, bldr->frame, bldr->desc);
      bldr->stacks[ilvl+1].mask |= (uint8_t)BIT(inode);

      /* Write the nodes of the stack of the current tree level into the buf */
      res = XD(stack_write)
        (&bldr->stacks[ilvl], bldr->buffer, &parent_node->ichild_node, &nleaves);
      if(res != RES_OK) goto error;

      bldr->nleaves += nleaves;
      if(nleaves) bldr->non_empty_lvl = MMIN(bldr->non_empty_lvl, (int)ilvl);

      /* Reset the current stack */
      XD(stack_clear)(&bldr->stacks[ilvl]);
    }
  }

  /* Retrieve the index of the current voxel and of its parent */
  ichild = vox->mcode & (NCHILDREN-1);
  inode = (vox->mcode >> TREE_DIMENSION) & (NCHILDREN-1);
  ichild_flag = (uint8_t)BIT(ichild);

  /* Register the voxel */
  memcpy(bldr->stacks[0].nodes[inode].data[ichild], vox->data, bldr->desc->size);
  bldr->stacks[0].nodes[inode].is_valid |= ichild_flag;
  bldr->stacks[0].nodes[inode].is_leaf |= ichild_flag;

  /* Update morton code of the last registered voxel */
  bldr->mcode = vox->mcode;

exit:
  return res;
error:
  goto exit;
}

static INLINE res_T
XD(builder_finalize)
  (struct XD(builder)* bldr,
   struct buffer_index* root_id,
   void* root_data)
{
  const void* data[NCHILDREN];
  size_t inode;
  size_t nleaves;
  int ilvl;
  int ichild;
  size_t nchildren;
  res_T res = RES_OK;
  ASSERT(bldr);

  inode = (bldr->mcode >> TREE_DIMENSION) & (NCHILDREN-1);
  bldr->stacks[0].mask |= (uint8_t)BIT(inode);

  /* Flush the stacked nodes */
  FOR_EACH(ilvl, 0, bldr->tree_depth-1) {
    uint32_t parent_coords[3];
    double parent_sz[3];
    double parent_low[3];
    size_t parent_depth;
    uint64_t parent_mcode;
    struct XD(node)* parent_node;
    int i;

    if(bldr->stacks[ilvl].mask == 0) continue;

    /* Compute the parent node attribute */
    parent_mcode = bldr->mcode >> (TREE_DIMENSION * (ilvl+2));
    d3_splat(parent_sz, INF);
    d3_splat(parent_low,-INF);
    FOR_EACH(i, 0, TREE_DIMENSION) {
      const int iaxis = bldr->frame[i];
      parent_coords[iaxis] = morton3D_decode_u21
        (parent_mcode >> (TREE_DIMENSION-1-i));
      parent_sz[iaxis] = bldr->voxsz[iaxis] * (double)(1 << (ilvl+2));
      parent_low[iaxis] = parent_coords[iaxis] * parent_sz[iaxis];
    }

    parent_depth = (size_t)(bldr->tree_depth - 2 - ilvl);
    ASSERT(parent_depth <= (size_t)bldr->tree_depth-2);

    /* Retrieve the node index of the next level */
    inode = (int)parent_mcode & (NCHILDREN-1);

    /* Setup the parent node of the nodes registered into the current stack */
    parent_node = &bldr->stacks[ilvl+1].nodes[inode]; /* Fetch the parent node */
    XD(stack_setup_node)(&bldr->stacks[ilvl], parent_node, parent_low,
      parent_sz, parent_depth, bldr->frame, bldr->desc);
    bldr->stacks[ilvl+1].mask |= (uint8_t)BIT(inode);

    /* Write the stacked nodes of the current level */
    res = XD(stack_write)
      (&bldr->stacks[ilvl], bldr->buffer, &parent_node->ichild_node, &nleaves);
    if(res != RES_OK) goto error;
    bldr->nleaves += nleaves;
  }

  ilvl = bldr->tree_depth-1; /* Root level */

  /* Write the root node */
  res = XD(stack_write)(&bldr->stacks[ilvl], bldr->buffer, root_id, &nleaves);
  if(res != RES_OK) goto error;
  bldr->nleaves += nleaves;

  /* Setup the root attribs */
  nchildren = 0;
  ASSERT(bldr->stacks[ilvl].mask == 1); /* Only the root node is active */
  FOR_EACH(ichild, 0, NCHILDREN) {
    const int ichild_flag = BIT(ichild);
    if(!(bldr->stacks[ilvl].nodes[0].is_valid & ichild_flag)) continue;

    data[nchildren] = bldr->stacks[ilvl].nodes[0].data[ichild];
    ++nchildren;
  }
  bldr->desc->merge(root_data, data, nchildren, bldr->desc->context);

exit:
  return res;
error:
  goto exit;
}

#undef TREE_DIMENSION
#undef NCHILDREN
#undef XD

#endif /* !TREE_DIMENSION */
