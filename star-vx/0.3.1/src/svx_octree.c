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
#include "svx_tree_generic_func.h"

#include <rsys/double3.h>
#include<rsys/mem_allocator.h>

/* Generate the octree_builder API */
#define TREE_DIMENSION 3
#include "svx_tree_builder.h"

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
svx_octree_create
  (struct svx_device* dev,
   const double lower[3], /* Lower bound of the octree */
   const double upper[3], /* Upper bound of the octree */
   const size_t nvoxels[3], /* # voxels along the 3 axis */
   const struct svx_voxel_desc* desc, /* Descriptor of a voxel */
   struct svx_tree** out_oct)
{
  struct svx_tree* oct = NULL;
  double vox_sz[3]; /* World space size of a voxel */
  struct octree_builder bldr;
  struct voxel vox = VOXEL_NULL;
  uint64_t mcode_max;
  uint64_t mcode;
  res_T res = RES_OK;

  if(!dev || !lower || !upper || !nvoxels || !check_svx_voxel_desc(desc)
  || !out_oct) {
    res = RES_BAD_ARG;
    goto error;
  }
  if(lower[0] >= upper[0]
  || lower[1] >= upper[1]
  || lower[2] >= upper[2]) {
    log_err(dev,
      "%s: the submitted AABB is degenerated.\n"
      "\tlower={%g, %g, %g}, upper={%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(lower), SPLIT3(upper));
    res = RES_BAD_ARG;
    goto error;
  }
  if(!nvoxels[0] || !nvoxels[1] || !nvoxels[2]) {
    log_err(dev,
      "%s: the number of voxels along each axis cannot be null.\n"
      "\t#voxels XYZ = {%lu, %lu, %lu}.\n",
      FUNC_NAME,
      (unsigned long)nvoxels[0],
      (unsigned long)nvoxels[1],
      (unsigned long)nvoxels[2]);
    res = RES_BAD_ARG;
    goto error;
  }

  res = tree_create(dev, SVX_OCTREE, desc->size, &oct);
  if(res != RES_OK) goto error;

  oct->frame[0] = SVX_AXIS_X;
  oct->frame[1] = SVX_AXIS_Y;
  oct->frame[2] = SVX_AXIS_Z;

  /* Compute the octree definition */
  oct->definition = MMAX(nvoxels[0], 2);
  oct->definition = MMAX(nvoxels[1], oct->definition);
  oct->definition = MMAX(nvoxels[2], oct->definition);
  oct->definition = round_up_pow2(oct->definition);

  /* Setup the octree AABB in world space */
  d3_set(oct->lower, lower);
  d3_set(oct->upper, upper);

  /* Compute the voxel size in world space */
  vox_sz[0] = (upper[0] - lower[0]) / (double)nvoxels[0];
  vox_sz[1] = (upper[1] - lower[1]) / (double)nvoxels[1];
  vox_sz[2] = (upper[2] - lower[2]) / (double)nvoxels[2];

  /* Compute the octree AABB in world space */
  oct->tree_low[0] = lower[0];
  oct->tree_low[1] = lower[1];
  oct->tree_low[2] = lower[2];
  oct->tree_upp[0] = oct->tree_low[0] + (double)oct->definition * vox_sz[0];
  oct->tree_upp[1] = oct->tree_low[1] + (double)oct->definition * vox_sz[1];
  oct->tree_upp[2] = oct->tree_low[2] + (double)oct->definition * vox_sz[2];
  oct->tree_size[0] = oct->tree_upp[0] - oct->tree_low[0];
  oct->tree_size[1] = oct->tree_upp[1] - oct->tree_low[1];
  oct->tree_size[2] = oct->tree_upp[2] - oct->tree_low[2];

  /* Intialize the octree builder */
  res = octree_builder_init(&bldr, oct->definition, oct->tree_low,
    oct->tree_upp, oct->frame, desc, &oct->buffer);
  if(res != RES_OK) goto error;

  vox.data = MEM_CALLOC(dev->allocator, 1, desc->size);
  if(!vox.data) {
    res = RES_MEM_ERR;
    goto error;
  }

  mcode_max = oct->definition * oct->definition * oct->definition;
  FOR_EACH(mcode, 0, mcode_max) {
    size_t xyz[3];
    uint32_t ui3[3];

    morton_xyz_decode_u21(mcode, ui3);

    /* Out of bound voxels */
    if(ui3[0] >= nvoxels[0]
    || ui3[1] >= nvoxels[1]
    || ui3[2] >= nvoxels[2])
      continue;

    /* Retrieve the voxel data from the caller */
    xyz[0] = (size_t)ui3[0];
    xyz[1] = (size_t)ui3[1];
    xyz[2] = (size_t)ui3[2];
    desc->get(xyz, mcode, vox.data, desc->context);
    vox.mcode = mcode;

    /* Register the voxel against the octree */
    res = octree_builder_add_voxel(&bldr, &vox);
    if(res != RES_OK) goto error;
  }

  res = octree_builder_finalize(&bldr, &oct->root, oct->root_attr);
  if(res != RES_OK) goto error;

  oct->nleaves = bldr.nleaves;
  oct->depth = (size_t)(bldr.tree_depth - bldr.non_empty_lvl)
    + 1 /* leaf level */;
  ASSERT(bldr.tree_depth > bldr.non_empty_lvl);

#ifndef NDEBUG
  {
    size_t nleaves = 0;
    CHK(buffer_check_tree(&oct->buffer, oct->root, 3, &nleaves) == RES_OK);
    CHK(nleaves == oct->nleaves);
  }
#endif

  /* Adjust the octree definition with respect to the octree depth since finest
   * levels might be removed during the build due to the merging process */
  oct->definition = oct->definition / ((size_t)1 << bldr.non_empty_lvl);

exit:
  if(vox.data) MEM_RM(dev->allocator, vox.data);
  if(out_oct) *out_oct = oct;
  return res;
error:
  if(oct) {
    SVX(tree_ref_put(oct));
    oct = NULL;
  }
  goto exit;
}

