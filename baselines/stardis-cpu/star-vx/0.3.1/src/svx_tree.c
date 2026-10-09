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

#include "svx_device.h"
#include "svx_tree.h"
#include "svx_tree_generic_func.h"

#include <rsys/double3.h>
#include<rsys/mem_allocator.h>

/* Generate the generic binary functions */
#define TREE_DIMENSION 1
#include "svx_tree_generic_func.h"

/* Generate the generic octree functions */
#define TREE_DIMENSION 3
#include "svx_tree_generic_func.h"

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static res_T
check_octree(struct svx_tree* tree)
{
  ASSERT(tree && tree->type == SVX_OCTREE);

  if(tree->frame[0] != SVX_AXIS_X
  && tree->frame[1] != SVX_AXIS_Y
  && tree->frame[2] != SVX_AXIS_Z)
    return RES_BAD_ARG;

  if(!IS_POW2(tree->definition))
    return RES_BAD_ARG;

  if(tree->lower[0] >= tree->upper[0]
  || tree->lower[1] >= tree->upper[1]
  || tree->lower[2] >= tree->upper[2])
    return RES_BAD_ARG;

  if(tree->tree_low[0] >= tree->tree_upp[0]
  || tree->tree_low[1] >= tree->tree_upp[1]
  || tree->tree_low[2] >= tree->tree_upp[2])
    return RES_BAD_ARG;

  if(tree->tree_low[0] != tree->lower[0]
  || tree->tree_low[1] != tree->lower[1]
  || tree->tree_low[2] != tree->lower[2])
    return RES_BAD_ARG;

  if(tree->tree_upp[0] < tree->upper[0]
  || tree->tree_upp[1] < tree->upper[1]
  || tree->tree_upp[2] < tree->upper[2])
    return RES_BAD_ARG;

  if(tree->tree_size[0] != tree->tree_upp[0] - tree->tree_low[0]
  || tree->tree_size[1] != tree->tree_upp[1] - tree->tree_low[1]
  || tree->tree_size[2] != tree->tree_upp[2] - tree->tree_low[2])
    return RES_BAD_ARG;

  #ifndef NDEBUG
  {
    size_t nleaves = 0;
    res_T res = buffer_check_tree(&tree->buffer, tree->root, 3, &nleaves);
    if(res != RES_OK) return res;

    if(nleaves != tree->nleaves)
      return RES_BAD_ARG;
  }
  #endif

  return RES_OK;
}

static res_T
check_bintree(struct svx_tree* tree)
{
  enum svx_axis axis = SVX_AXIS_NONE__;
  ASSERT(tree && tree->type == SVX_BINTREE);

  if(tree->frame[0] == SVX_AXIS_NONE__
  || tree->frame[1] != SVX_AXIS_NONE__
  || tree->frame[2] != SVX_AXIS_NONE__)
    return RES_BAD_ARG;

  if(!IS_POW2(tree->definition))
    return RES_BAD_ARG;

  axis = tree->frame[0];
  if(tree->lower[axis] >= tree->upper[axis])
    return RES_BAD_ARG;

  if(tree->tree_low[axis] >= tree->tree_upp[axis])
    return RES_BAD_ARG;

  if(tree->tree_low[axis] != tree->lower[axis])
    return RES_BAD_ARG;

  if(tree->tree_upp[axis] < tree->upper[axis])
    return RES_BAD_ARG;

  if(tree->tree_size[axis] != tree->tree_upp[axis] - tree->tree_low[axis])
    return RES_BAD_ARG;

  #ifndef NDEBUG
  {
    size_t nleaves = 0;
    res_T res = buffer_check_tree(&tree->buffer, tree->root, 1, &nleaves);
    if(res != RES_OK) return res;

    if(nleaves != tree->nleaves)
      return RES_BAD_ARG;
  }
  #endif

  return RES_OK;
}

static void
tree_clear(struct svx_tree* tree)
{
  ASSERT(tree);
  tree->definition = 0;
  d3_splat(tree->lower, DBL_MAX);
  d3_splat(tree->upper,-DBL_MAX);
  d3_splat(tree->tree_low, DBL_MAX);
  d3_splat(tree->tree_upp,-DBL_MAX);
  d3_splat(tree->tree_size, -1);
  tree->root = BUFFER_INDEX_NULL;
  buffer_clear(&tree->buffer);
  memset(tree->root_attr, 0, sizeof(tree->root_attr));
  tree->nleaves = 0;
  tree->depth = 0;
  tree->frame[0] = SVX_AXIS_NONE__;
  tree->frame[1] = SVX_AXIS_NONE__;
  tree->frame[2] = SVX_AXIS_NONE__;
  tree->type = -1;
}

static void
tree_release(ref_T* ref)
{
  struct svx_tree* tree;
  struct svx_device* dev;
  ASSERT(ref);
  tree = CONTAINER_OF(ref, struct svx_tree, ref);
  buffer_release(&tree->buffer);
  dev = tree->dev;
  MEM_RM(dev->allocator, tree);
  SVX(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
svx_tree_create_from_stream
  (struct svx_device* dev,
   FILE* stream,
   struct svx_tree** out_tree)
{
  struct svx_tree* tree = NULL;
  res_T res = RES_BAD_ARG;

  if(!stream || !out_tree) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = tree_create
    (dev, -1/* Unknown tree type */, 1/* Dummy voxel size */, &tree);
  if(res != RES_OK) goto error;

  /* Setup the tree data from the stream */
  res = tree_read(tree, stream);
  if(res != RES_OK) goto error;

  switch(tree->type) {
    case SVX_OCTREE: res = check_octree(tree); break;
    case SVX_BINTREE: res = check_bintree(tree); break;
    default: FATAL("Unreachable code.\n"); break;
  }
  if(res != RES_OK) goto error;

exit:
  if(out_tree) *out_tree = tree;
  return res;
error:
  if(tree) { SVX(tree_ref_put(tree)); tree = NULL; }
  goto exit;
}

res_T
svx_tree_ref_get(struct svx_tree* tree)
{
  if(!tree) return RES_BAD_ARG;
  ref_get(&tree->ref);
  return RES_OK;
}

res_T
svx_tree_ref_put(struct svx_tree* tree)
{
  if(!tree) return RES_BAD_ARG;
  ref_put(&tree->ref, tree_release);
  return RES_OK;
}

res_T
svx_tree_get_desc
  (const struct svx_tree* tree, struct svx_tree_desc* desc)
{
  if(!tree || !desc) return RES_BAD_ARG;
  d3_set(desc->lower, tree->lower);
  d3_set(desc->upper, tree->upper);
  desc->nleaves = tree->nleaves;
  desc->nvoxels = buffer_absolute_attr_index
    (&tree->buffer, tree->buffer.attr_head) + 1; /* Root node */
  desc->depth = tree->depth;
  desc->type = tree->type;
  desc->frame[0] = tree->frame[0];
  desc->frame[1] = tree->frame[1];
  desc->frame[2] = tree->frame[2];
  return RES_OK;
}

res_T
svx_tree_for_each_leaf(struct svx_tree* tree, svx_leaf_function_T func, void* ctx)
{
  res_T res = RES_OK;
  if(!tree) return RES_BAD_ARG;
  switch(tree->type) {
    case SVX_BINTREE: res = bintree_for_each_leaf(tree, func, ctx); break;
    case SVX_OCTREE: res = octree_for_each_leaf(tree, func, ctx); break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return res;
}

res_T
svx_tree_trace_ray
  (struct svx_tree* tree,
   const double org[3],
   const double dir[3],
   const double range[2],
   const svx_hit_challenge_T challenge, /* NULL <=> Traversed up to the leaves */
   const svx_hit_filter_T filter, /* NULL <=> Stop RT at the 1st hit voxel */
   void* context, /* Data sent to the filter functor */
   struct svx_hit* hit)
{
  res_T res = RES_OK;
  if(!tree) return RES_BAD_ARG;
  switch(tree->type) {
    case SVX_BINTREE:
      res = bintree_trace_ray
        (tree, org, dir, range, challenge, filter, context, hit);
      break;
    case SVX_OCTREE:
      res = octree_trace_ray
        (tree, org, dir, range, challenge, filter, context, hit);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return res;
}

res_T
svx_tree_at
  (struct svx_tree* tree,
   const double pos[3],
   svx_at_filter_T filter,
   void* context,
   struct svx_voxel* vox)
{
  res_T res = RES_OK;
  if(!tree) return RES_BAD_ARG;
  switch(tree->type){
    case SVX_BINTREE: res = bintree_at(tree, pos, filter, context, vox); break;
    case SVX_OCTREE: res = octree_at(tree, pos, filter, context, vox); break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return res;
}

res_T
svx_tree_write(const struct svx_tree* tree, FILE* stream)
{
  res_T res = RES_OK;

  if(!tree || !stream) {
    res = RES_BAD_ARG;
    goto error;
  }

  #define WRITE(Var, N) {                                                      \
    if(fwrite((Var), sizeof(*(Var)), (N), stream) != (N)) {                    \
      res = RES_IO_ERR;                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0
  WRITE(&SVX_TREE_VERSION, 1);
  WRITE(&tree->definition, 1);
  WRITE(tree->lower, 3);
  WRITE(tree->upper, 3);
  WRITE(tree->tree_low, 3);
  WRITE(tree->tree_upp, 3);
  WRITE(tree->tree_size, 3);
  WRITE(&tree->nleaves, 1);
  WRITE(&tree->depth, 1);
  WRITE(tree->frame, 3);
  WRITE(&tree->type, 1);
  WRITE(&tree->root, 1);
  WRITE(tree->root_attr, SVX_MAX_SIZEOF_VOXEL);
  #undef WRITE

  res = buffer_write(&tree->buffer, stream);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
tree_create
  (struct svx_device* dev,
   const enum svx_tree_type type,
   const size_t vxsz,
   struct svx_tree** out_tree)
{
  struct svx_tree* tree = NULL;
  res_T res = RES_OK;

  if(!dev || !out_tree) {
    res = RES_BAD_ARG;
    goto error;
  }

  tree = MEM_ALLOC_ALIGNED(dev->allocator, sizeof(*tree), 16);
  if(!tree) {
    res = RES_MEM_ERR;
    goto error;
  }
  memset(tree, 0, sizeof(*tree));
  ref_init(&tree->ref);
  SVX(device_ref_get(dev));
  buffer_init(dev->allocator, vxsz, &tree->buffer);
  tree->dev = dev;
  d3_splat(tree->lower, DBL_MAX);
  d3_splat(tree->upper,-DBL_MAX);
  d3_splat(tree->tree_low, DBL_MAX);
  d3_splat(tree->tree_upp,-DBL_MAX);
  d3_splat(tree->tree_size, -1);
  tree->type = type;
  tree->frame[0] = SVX_AXIS_NONE__;
  tree->frame[1] = SVX_AXIS_NONE__;
  tree->frame[2] = SVX_AXIS_NONE__;
exit:
  if(out_tree) *out_tree = tree;
  return res;
error:
  if(tree) {
    SVX(tree_ref_put(tree));
    tree = NULL;
  }
  goto exit;
}

res_T
tree_read(struct svx_tree* tree, FILE* stream)
{
  int version = 0;
  res_T res = RES_OK;
  ASSERT(tree && stream);

  tree_clear(tree);

  #define READ(Var, N) {                                                       \
    if(fread((Var), sizeof(*(Var)), (N), stream) != (N)) {                     \
      if(feof(stream)) {                                                       \
        res = RES_BAD_ARG;                                                     \
      } else if(ferror(stream)) {                                              \
        res = RES_IO_ERR;                                                      \
      } else {                                                                 \
        res = RES_UNKNOWN_ERR;                                                 \
      }                                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  /* Currently only one version of the tree data structure could be serialized.
   * The version management is thus as simple as rejecting any tree data
   * structure whose version is not the current version. */
  READ(&version, 1);
  if(version != SVX_TREE_VERSION) {
    log_err(tree->dev,
      "%s: unexpected tree version %d. Expecting a tree in version %d.\n",
      FUNC_NAME, version, SVX_TREE_VERSION);
    res = RES_BAD_ARG;
    goto error;
  }

  READ(&tree->definition, 1);
  READ(tree->lower, 3);
  READ(tree->upper, 3);
  READ(tree->tree_low, 3);
  READ(tree->tree_upp, 3);
  READ(tree->tree_size, 3);
  READ(&tree->nleaves, 1);
  READ(&tree->depth, 1);
  READ(tree->frame, 3);
  READ(&tree->type, 1);
  READ(&tree->root, 1);
  READ(tree->root_attr, SVX_MAX_SIZEOF_VOXEL);
  #undef READ

  res = buffer_read(&tree->buffer, stream);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  tree_clear(tree);
  goto exit;
}

