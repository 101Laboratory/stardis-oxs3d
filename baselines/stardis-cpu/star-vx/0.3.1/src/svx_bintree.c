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
#include "svx_c.h"
#include "svx_device.h"
#include "svx_tree.h"
#include "svx_tree_builder.h"

#include<rsys/mem_allocator.h>

/* Generate the bintree_builder API */
#define TREE_DIMENSION 1
#include "svx_tree_builder.h"

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
svx_bintree_create
  (struct svx_device* dev,
   const double lower, /* Lower bound of the bintree */
   const double upper, /* Upper bound of the bintree */
   const size_t nvoxels, /* # voxels along the range */
   const enum svx_axis axis, /* Axis along which the binary tree is defined */
   const struct svx_voxel_desc* desc, /* Descriptor of a voxel */
   struct svx_tree** out_bintree)
{
  struct svx_tree* bintree = NULL;
  double vox_sz; /* World space size of a voxel */
  struct bintree_builder bldr;
  struct voxel vox = VOXEL_NULL;
  size_t ivox;
  res_T res = RES_OK;

  if(!dev || !check_svx_voxel_desc(desc) || !out_bintree || axis<0 || axis>2) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(lower >= upper) {
    log_err(dev,
      "%s: the submitted range is degenerated\n"
      "\tlower = %g, upper = %g\n", FUNC_NAME, lower, upper);
    res = RES_BAD_ARG;
    goto error;
  }
  if(!nvoxels) {
    log_err(dev,
      "%s: the number of voxels along cannot be null.\n"
      "\t#voxels = %lu.\n",
      FUNC_NAME, (unsigned long)nvoxels);
    res = RES_BAD_ARG;
    goto error;
  }

  res = tree_create(dev, SVX_BINTREE, desc->size, &bintree);
  if(res != RES_OK) goto error;

  /* The binary tree definition that must be a power of two */
  bintree->definition = round_up_pow2(nvoxels);

  bintree->frame[0] = axis;

  /* Setup the binary tree AABB in world space */
  bintree->lower[axis] = lower;
  bintree->upper[axis] = upper;

  /* Compute the world space range of the binary tree */
  vox_sz = (upper - lower) / (double)nvoxels;
  bintree->tree_low[axis] = lower;
  bintree->tree_upp[axis] = lower + (double)bintree->definition * vox_sz;
  bintree->tree_size[axis] = bintree->tree_upp[axis] - bintree->tree_low[axis];

  /* Initialize the bintree builder */
  res = bintree_builder_init(&bldr, bintree->definition, bintree->tree_low,
      bintree->tree_upp, bintree->frame, desc, &bintree->buffer);
  if(res != RES_OK) goto error;

  /* Allocate the memory space of a temporary voxel */
  vox.data = MEM_CALLOC(dev->allocator, 1, desc->size);
  if(!vox.data) {
    res = RES_MEM_ERR;
    goto error;
  }

  FOR_EACH(ivox, 0, bintree->definition) {
    size_t xyz[3] = {0, 0, 0};
    if(ivox >= nvoxels) continue; /* Out of bound voxels */

    /* Retrieve the voxel data from the caller */
    xyz[axis] = ivox;
    desc->get(xyz, ivox, vox.data, desc->context);
    vox.mcode = ivox;

    /* Register the voxel against the bintree */
    res = bintree_builder_add_voxel(&bldr, &vox);
    if(res != RES_OK) goto error;
  }

  res = bintree_builder_finalize(&bldr, &bintree->root, bintree->root_attr);
  if(res != RES_OK) goto error;

  bintree->nleaves = bldr.nleaves;
  bintree->depth = (size_t)(bldr.tree_depth - bldr.non_empty_lvl)
    + 1 /* leaf level */;
  ASSERT(bldr.tree_depth > bldr.non_empty_lvl);

#ifndef NDEBUG
  {
    size_t nleaves = 0;
    CHK(buffer_check_tree(&bintree->buffer, bintree->root, 1, &nleaves) == RES_OK);
    CHK(nleaves == bintree->nleaves);
  }
#endif

  /* Adjust the binary tree definition with respect to the binary tree depth
   * since finest levels might be removed during the build due to the merging
   * process */
  bintree->definition = bintree->definition / ((size_t)1<<bldr.non_empty_lvl);

exit:
  if(vox.data) MEM_RM(dev->allocator, vox.data);
  if(out_bintree) *out_bintree = bintree;
  return res;
error:
  if(bintree) {
    SVX(tree_ref_put(bintree));
    bintree = NULL;
  }
  goto exit;
}


