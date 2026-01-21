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

#include "test_svx_utils.h"
#include <rsys/math.h>

#define AXIS SVX_AXIS_Y

struct leaves_context {
  double lower;
  double upper;
  size_t nvoxels;
  size_t depth;
  enum svx_axis axis;

  char* leaves;
  size_t nleaves;
};

struct at_context {
  double position;
  size_t depth;
};

struct aabb {
  double lower[3];
  double upper[3];
  size_t depth;
};

struct build_context {
  double voxsz[3];
  double lower[3];
  double upper[3];
  size_t max_depth;
};


static void
get(const size_t xyz[3], const uint64_t mcode, void* dst, void* ctx)
{
  double* val = dst;
  CHK(xyz != NULL);
  CHK(val != NULL);
  CHK((intptr_t)ctx == 0xDECAFBAD);
  CHK(mcode == xyz[AXIS]);
  *val = (double)xyz[AXIS];
}

static int
no_merge(const struct svx_voxel voxels[], const size_t nvoxels, void* ctx)
{
  CHK(voxels != NULL);
  CHK(nvoxels != 0);
  CHK((intptr_t)ctx == 0xDECAFBAD);
  return 0; /* Merge nothing */
}

static void
keep_max(void* dst, const void* voxels[], const size_t nvoxels,  void* ctx)
{
  double* vox_dst = dst;
  double max_val = -DBL_MAX;
  size_t i;

  CHK(dst != NULL);
  CHK(voxels != NULL);
  CHK(nvoxels != 0);
  CHK(ctx != NULL);

  FOR_EACH(i, 0, nvoxels) {
    const double* val = voxels[i];
    max_val = MMAX(max_val, *val);
  }
  *vox_dst = max_val;
}

static int
max_lod
  (const struct svx_voxel* vox,
   const double pos[3],
   void* context)
{
  const struct at_context* ctx = context;
  CHK(vox != NULL);
  CHK(pos != NULL);
  CHK(ctx != NULL);
  CHK(vox->depth <= ctx->depth);
  CHK(pos[AXIS] == ctx->position);
  return vox->depth < ctx->depth;
}

static void
check_leaves
  (const struct svx_voxel* leaf,
   const size_t ileaf,
   void* context)
{
  const double* dbl = NULL;
  struct leaves_context* ctx = context;
  double delta;
  double lower;

  CHK(leaf != NULL);
  CHK(leaf->data != NULL);
  CHK(ctx != NULL);
  CHK(leaf->lower[ctx->axis] < leaf->upper[ctx->axis]);
  CHK(ileaf < ctx->nvoxels);

  dbl = leaf->data;
  CHK(*dbl >= 0);

  delta = (ctx->upper - ctx->lower) / (double)ctx->nvoxels;
  lower = *dbl * delta;

  CHK(eq_eps(lower, leaf->lower[ctx->axis], 1.e-6));
  CHK(eq_eps(lower+delta, leaf->upper[ctx->axis], 1.e-6));

  CHK(leaf->depth == ctx->depth - 1);
  CHK(ctx->leaves[ileaf] == 0);
  ctx->leaves[ileaf] = 1;
  ctx->nleaves += 1;
}

static void
get_aabb(const size_t xyz[3], const uint64_t mcode, void* dst, void* ctx)
{
  const struct build_context* build_ctx = ctx;
  struct aabb* aabb = dst;

  CHK(mcode == xyz[AXIS]);
  d3_splat(aabb->lower,-INF);
  d3_splat(aabb->upper, INF);
  aabb->lower[AXIS] =
    (double)xyz[AXIS] * build_ctx->voxsz[AXIS] + build_ctx->lower[AXIS];
  aabb->upper[AXIS] = aabb->lower[AXIS] + build_ctx->voxsz[AXIS];
  aabb->depth = build_ctx->max_depth;
}


static void
merge_aabb(void* dst, const void* voxels[], const size_t nvoxels, void* ctx)
{
  const struct build_context* build_ctx = ctx;
  double upper[3];
  double voxsz[3];
  struct aabb* aabb = dst;
  size_t depth = SIZE_MAX;
  size_t i;
  CHK(dst && voxels && nvoxels && ctx);

  d3_splat(aabb->lower,-INF);
  d3_splat(aabb->upper, INF);
  aabb->depth = 0;
  aabb->lower[AXIS] = DBL_MAX;
  aabb->upper[AXIS] =-DBL_MAX;

  FOR_EACH(i, 0, nvoxels) {
    const struct aabb* vox_aabb = voxels[i];
    if(depth == SIZE_MAX) {
      depth = vox_aabb->depth;
    } else { /* Not invalid */
      CHK(depth == vox_aabb->depth);
    }
    aabb->lower[AXIS] = MMIN(vox_aabb->lower[AXIS], aabb->lower[AXIS]);
    aabb->upper[AXIS] = MMAX(vox_aabb->upper[AXIS], aabb->upper[AXIS]);
  }

  d3_splat(upper, INF);
  d3_splat(voxsz, INF);

  CHK(build_ctx->max_depth >= depth);
  CHK(depth > 0);
  aabb->depth = depth - 1;

  i = build_ctx->max_depth - aabb->depth;
  voxsz[AXIS] = build_ctx->voxsz[AXIS] * (double)(1<<i);

  /* Clamp voxel to grid size */
  upper[AXIS] = MMIN(aabb->lower[AXIS] + voxsz[AXIS], build_ctx->upper[AXIS]);

  /* Adjust the voxel size from the clampd voxel */
  voxsz[AXIS] = upper[AXIS] - aabb->lower[AXIS];

  CHK(eq_eps(voxsz[AXIS], aabb->upper[AXIS] - aabb->lower[AXIS], 1.e-6));
}

static int
challenge_aabb(const struct svx_voxel voxels[], const size_t nvoxels, void* ctx)
{
  const struct build_context* build_ctx = ctx;
  size_t depth = SIZE_MAX;
  size_t i;
  CHK(voxels && nvoxels && ctx);

  FOR_EACH(i, 0, nvoxels) {
    double voxsz[3];
    const struct aabb* aabb = voxels[i].data;
    const size_t n = build_ctx->max_depth - aabb->depth;
    CHK(build_ctx->max_depth >= aabb->depth);

    if(depth == SIZE_MAX) {
      depth = aabb->depth;
    } else { /* Not invalid */
      CHK(depth == aabb->depth);
    }
    CHK(depth == voxels[i].depth);
    CHK(eq_eps(aabb->lower[AXIS], voxels[i].lower[AXIS], 1.e-6));
    CHK(eq_eps(aabb->upper[AXIS], voxels[i].upper[AXIS], 1.e-6));
    CHK(aabb->lower[(AXIS+1)%3] == voxels[i].lower[(AXIS+1)%3]);
    CHK(aabb->lower[(AXIS+2)%3] == voxels[i].lower[(AXIS+2)%3]);
    CHK(aabb->lower[(AXIS+1)%3] == -INF);
    CHK(aabb->lower[(AXIS+2)%3] == -INF);
    CHK(aabb->upper[(AXIS+1)%3] == voxels[i].upper[(AXIS+1)%3]);
    CHK(aabb->upper[(AXIS+2)%3] == voxels[i].upper[(AXIS+2)%3]);
    CHK(aabb->upper[(AXIS+1)%3] == INF);
    CHK(aabb->upper[(AXIS+2)%3] == INF);
    voxsz[AXIS] = build_ctx->voxsz[AXIS] * (double)(1<<n);
    CHK(eq_eps(voxsz[AXIS], aabb->upper[AXIS] - aabb->lower[AXIS], 1.e-6));
  }
  return 1;
}

static void
test_at_accessor(struct svx_tree* tree, const size_t nvoxels)
{
  struct svx_tree_desc desc;
  struct at_context ctx;
  size_t nvxls;
  double tree_sz;
  double delta;
  enum svx_axis axis;

  CHK(svx_tree_get_desc(tree, &desc) == RES_OK);
  CHK(desc.type == SVX_BINTREE);
  CHK(desc.frame[0] == AXIS);

  axis = desc.frame[0];
  tree_sz = desc.upper[axis] - desc.lower[axis];
  delta = tree_sz / (double)nvoxels;

  ctx.depth = desc.depth;
  CHK(ctx.depth > 0);

  nvxls = nvoxels;

  /* TODO Comment this */
  while(ctx.depth-- != 0) {
    double pos[3] = {0, 0, 0};
    const size_t iter = desc.depth - ctx.depth - 1;
    size_t i;

    FOR_EACH(i, 0, nvxls) {
      struct svx_voxel vox;
      const double low = desc.lower[axis] + (double)i * delta;
      const double upp = low + delta;
      size_t mcode;

      pos[axis] = desc.lower[axis] + ((double)i+1.0/(1u<<(2+iter)))*delta;

      ctx.position = pos[axis];
      CHK(svx_tree_at(tree, pos, max_lod, &ctx, &vox) == RES_OK);

      mcode = i * (size_t)(1u<<iter) + ((1u << iter) - 1);
      mcode = MMIN(mcode, nvoxels-1);

      CHK(*((double*)vox.data) == mcode);
      CHK(vox.is_leaf == (desc.depth-1 == ctx.depth));
      CHK(vox.depth == ctx.depth);
      CHK(eq_eps(vox.lower[axis], low, 1.e-6));
      CHK(eq_eps(vox.upper[axis], upp, 1.e-6));
    }

    nvxls = nvxls == 1 ? 0 : (nvxls + 1/*ceil*/)/2;
    delta = MMIN(delta * 2, tree_sz);
  }
  CHK(nvxls == 0);
}

int
main(int argc, char** argv)
{
  struct svx_device* dev = NULL;
  struct svx_tree* tree = NULL;
  struct mem_allocator allocator;
  struct svx_voxel_desc vox_desc = SVX_VOXEL_DESC_NULL;
  struct svx_tree_desc tree_desc = SVX_TREE_DESC_NULL;
  struct build_context build_ctx;
  struct leaves_context ctx;
  enum svx_axis axis;
  double low, upp;
  size_t nvxls;
  void* ptr = (void*)(intptr_t)0xDECAFBAD;
  FILE* stream = NULL;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(svx_device_create(NULL, &allocator, 1, &dev) == RES_OK);

  low = 0.0;
  upp = 1.0;
  axis = AXIS;
  nvxls = 5;

  vox_desc.get = get;
  vox_desc.merge = keep_max;
  vox_desc.challenge_merge = no_merge;
  vox_desc.context = ptr;
  vox_desc.size = sizeof(double);

  #define NEW_TREE svx_bintree_create

  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, &tree) == RES_OK);
  CHK(svx_tree_ref_put(tree) == RES_OK);

  upp = low;
  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, &tree) == RES_BAD_ARG);
  upp = 1.0;

  nvxls = 0;
  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, &tree) == RES_BAD_ARG);
  nvxls = 5;

  vox_desc.get = NULL;
  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, &tree) == RES_BAD_ARG);
  vox_desc.get = get;

  vox_desc.merge = NULL;
  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, &tree) == RES_BAD_ARG);
  vox_desc.merge = keep_max;

  vox_desc.challenge_merge = NULL;
  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, &tree) == RES_BAD_ARG);
  vox_desc.challenge_merge = no_merge;

  vox_desc.size = 0;
  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, &tree) == RES_BAD_ARG);
  vox_desc.size = sizeof(double);

  axis = SVX_AXIS_NONE__;
  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, &tree) == RES_BAD_ARG);
  axis = AXIS;

  CHK(NEW_TREE(NULL, low, upp, nvxls, axis, &vox_desc, &tree) == RES_BAD_ARG);
  CHK(NEW_TREE(dev, low, upp, nvxls, axis, NULL, &tree) == RES_BAD_ARG);
  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, NULL) == RES_BAD_ARG);
  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, &tree) == RES_OK);

  ctx.lower = low;
  ctx.upper = upp;
  ctx.nvoxels = nvxls;
  ctx.depth = 4;
  ctx.axis = AXIS;

  ctx.nleaves = 0;
  ctx.leaves = MEM_CALLOC(&allocator, 5, 1);
  CHK(ctx.leaves);
  CHK(svx_tree_for_each_leaf(tree, check_leaves, &ctx) == RES_OK);
  CHK(ctx.nleaves == 5);
  MEM_RM(&allocator, ctx.leaves);

  CHK(svx_tree_get_desc(NULL, &tree_desc) == RES_BAD_ARG);
  CHK(svx_tree_get_desc(tree, NULL) == RES_BAD_ARG);
  CHK(svx_tree_get_desc(tree, &tree_desc) == RES_OK);
  CHK(tree_desc.nleaves == 5);
  CHK(tree_desc.nvoxels == tree_desc.nleaves + 6/*#parents*/);
  CHK(tree_desc.depth == 4);
  CHK(tree_desc.type == SVX_BINTREE);
  CHK(tree_desc.lower[axis] == low);
  CHK(tree_desc.upper[axis] == upp);

  test_at_accessor(tree, nvxls);

  CHK(stream = tmpfile());
  CHK(svx_tree_write(NULL, stream) == RES_BAD_ARG);
  CHK(svx_tree_write(tree, NULL) == RES_BAD_ARG);
  CHK(svx_tree_write(tree, stream) == RES_OK);

  CHK(svx_tree_ref_put(tree) == RES_OK);
  
  rewind(stream);
  CHK(svx_tree_create_from_stream(NULL, stream, &tree) == RES_BAD_ARG);
  CHK(svx_tree_create_from_stream(dev, NULL, &tree) == RES_BAD_ARG);
  CHK(svx_tree_create_from_stream(dev, stream, NULL) == RES_BAD_ARG);
  CHK(svx_tree_create_from_stream(dev, stream, &tree) == RES_OK);
  fclose(stream);

  test_at_accessor(tree, nvxls);
  CHK(svx_tree_ref_put(tree) == RES_OK);

  build_ctx.max_depth = (size_t)log2i((int)round_up_pow2(nvxls));

  d3_splat(build_ctx.lower,-INF);
  d3_splat(build_ctx.upper, INF);
  d3_splat(build_ctx.voxsz, INF);
  build_ctx.lower[AXIS] = low;;
  build_ctx.upper[AXIS] = upp;
  build_ctx.voxsz[AXIS] = (upp-low)/(double)nvxls;

  vox_desc.get = get_aabb;
  vox_desc.merge = merge_aabb;
  vox_desc.challenge_merge = challenge_aabb;
  vox_desc.context = &build_ctx;
  vox_desc.size = sizeof(struct aabb);

  CHK(NEW_TREE(dev, low, upp, nvxls, axis, &vox_desc, &tree) == RES_OK);

  #undef NEW_TREE

  CHK(svx_tree_ref_put(tree) == RES_OK);
  CHK(svx_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

