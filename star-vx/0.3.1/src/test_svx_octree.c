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

#include <rsys/double3.h>
#include <rsys/morton.h>

#include <string.h>

struct leaves_context {
  double* lower;
  double* upper;
  size_t* nvoxels;
  size_t depth;
};

struct at_context {
  double position[3];
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

static int
no_merge(const struct svx_voxel voxels[], const size_t nvoxels, void* ctx)
{
  CHK(voxels != NULL);
  CHK(nvoxels != 0);
  CHK((intptr_t)ctx == 0xDECAFBAD);
  return 0; /* Merge nothing */
}

static int
merge_level0(const struct svx_voxel voxels[], const size_t nvoxels, void* ctx)
{
  double min_val = DBL_MAX;
  double max_val =-DBL_MAX;
  size_t i;
  CHK(voxels != NULL);
  CHK(nvoxels != 0);
  CHK((intptr_t)ctx == 0xDECAFBAD);

  FOR_EACH(i, 0, nvoxels) {
    const double* val = voxels[i].data;
    min_val = MMIN(min_val, *val);
    max_val = MMAX(max_val, *val);
  }

  FOR_EACH(i, 0, nvoxels) {
    if(max_val - min_val < 8) {
      CHK(voxels[i].depth == 5);
    }
  }

  return (max_val - min_val) < 8;
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

static void
get(const size_t xyz[3], const uint64_t mcode, void* dst, void* ctx)
{
  uint32_t ui3[3];
  double* val = dst;
  CHK(xyz != NULL);
  CHK(val != NULL);
  CHK((intptr_t)ctx == 0xDECAFBAD);

  ui3[0] = (uint32_t)xyz[0];
  ui3[1] = (uint32_t)xyz[1];
  ui3[2] = (uint32_t)xyz[2];

  CHK(mcode == morton_xyz_encode_u21(ui3));
  CHK(mcode == 
    ( morton3D_encode_u21(ui3[0]) << 2 
    | morton3D_encode_u21(ui3[1]) << 1
    | morton3D_encode_u21(ui3[2]) << 0));
  *val = (double)mcode;
}

static void
check_leaves
  (const struct svx_voxel* leaf,
   const size_t ileaf,
   void* context)
{
  const double* dbl = NULL;
  struct leaves_context* ctx = context;
  uint64_t mcode;
  uint32_t xyz[3];
  double lower[3];
  double delta[3];

  CHK(leaf != NULL);
  CHK(leaf->data != NULL);
  CHK(ctx != NULL);
  CHK(leaf->lower[0] < leaf->upper[0]);
  CHK(leaf->lower[1] < leaf->upper[1]);
  CHK(leaf->lower[2] < leaf->upper[2]);
  CHK(ileaf < ctx->nvoxels[0]*ctx->nvoxels[1]*ctx->nvoxels[2]);

  dbl = leaf->data;
  CHK(*dbl >= 0);

  mcode = (uint64_t)(*dbl);
  CHK(*dbl == (double)mcode);

  delta[0] = (ctx->upper[0] - ctx->lower[0]) / (double)ctx->nvoxels[0];
  delta[1] = (ctx->upper[1] - ctx->lower[1]) / (double)ctx->nvoxels[1];
  delta[2] = (ctx->upper[2] - ctx->lower[2]) / (double)ctx->nvoxels[2];

  morton_xyz_decode_u21(mcode, xyz);
  lower[0] = xyz[0] * delta[0];
  lower[1] = xyz[1] * delta[1];
  lower[2] = xyz[2] * delta[2];

  CHK(eq_eps(lower[0], leaf->lower[0], 1.e-6));
  CHK(eq_eps(lower[1], leaf->lower[1], 1.e-6));
  CHK(eq_eps(lower[2], leaf->lower[2], 1.e-6));
  CHK(eq_eps(lower[0] + delta[0], leaf->upper[0], 1.e-6));
  CHK(eq_eps(lower[1] + delta[1], leaf->upper[1], 1.e-6));
  CHK(eq_eps(lower[2] + delta[2], leaf->upper[2], 1.e-6));

  CHK(leaf->depth == ctx->depth - 1);
}

static void
write_scalars
  (const struct svx_voxel* leaf,
   const size_t ileaf,
   void* context)
{
  FILE* stream = context;
  (void)ileaf;
  CHK(stream != NULL);
  CHK(leaf != NULL);
  fprintf(stream, "%g\n", *(double*)leaf->data);
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
  CHK(d3_eq(pos, ctx->position));
  return vox->depth < ctx->depth;
}

static void
get_aabb(const size_t xyz[3], const uint64_t mcode, void* dst, void* ctx)
{
  const struct build_context* build_ctx = ctx;
  struct aabb* aabb = dst;
  uint32_t ui3[3];

  aabb->lower[0] = (double)xyz[0] * build_ctx->voxsz[0] + build_ctx->lower[0];
  aabb->lower[1] = (double)xyz[1] * build_ctx->voxsz[1] + build_ctx->lower[1];
  aabb->lower[2] = (double)xyz[2] * build_ctx->voxsz[2] + build_ctx->lower[2];
  
  ui3[0] = (uint32_t)xyz[0];
  ui3[1] = (uint32_t)xyz[1];
  ui3[2] = (uint32_t)xyz[2];
  CHK(mcode == morton_xyz_encode_u21(ui3));
  d3_add(aabb->upper, aabb->lower, build_ctx->voxsz);
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

  d3_splat(aabb->lower, DBL_MAX);
  d3_splat(aabb->upper,-DBL_MAX);
  aabb->depth = 0;

  FOR_EACH(i, 0, nvoxels) {
    const struct aabb* vox_aabb = voxels[i];
    if(depth == SIZE_MAX) {
      depth = vox_aabb->depth;
    } else {
      CHK(depth == vox_aabb->depth);
    }
    aabb->lower[0] = MMIN(vox_aabb->lower[0], aabb->lower[0]);
    aabb->lower[1] = MMIN(vox_aabb->lower[1], aabb->lower[1]);
    aabb->lower[2] = MMIN(vox_aabb->lower[2], aabb->lower[2]);
    aabb->upper[0] = MMAX(vox_aabb->upper[0], aabb->upper[0]);
    aabb->upper[1] = MMAX(vox_aabb->upper[1], aabb->upper[1]);
    aabb->upper[2] = MMAX(vox_aabb->upper[2], aabb->upper[2]);
  }

  CHK(build_ctx->max_depth >= depth);
  CHK(depth > 0);
  aabb->depth = depth - 1;

  i = build_ctx->max_depth - aabb->depth;
  voxsz[0] = build_ctx->voxsz[0] * (double)(1<<i);
  voxsz[1] = build_ctx->voxsz[1] * (double)(1<<i);
  voxsz[2] = build_ctx->voxsz[2] * (double)(1<<i);

  /* Clamp voxel to grid size */
  upper[0] = MMIN(aabb->lower[0] + voxsz[0], build_ctx->upper[0]);
  upper[1] = MMIN(aabb->lower[1] + voxsz[1], build_ctx->upper[1]);
  upper[2] = MMIN(aabb->lower[2] + voxsz[2], build_ctx->upper[2]);

  /* Adjust the voxel size from the clampd voxel */
  voxsz[0] = upper[0] - aabb->lower[0];
  voxsz[1] = upper[1] - aabb->lower[1];
  voxsz[2] = upper[2] - aabb->lower[2];

  CHK(eq_eps(voxsz[0], aabb->upper[0] - aabb->lower[0], 1.e-6));
  CHK(eq_eps(voxsz[1], aabb->upper[1] - aabb->lower[1], 1.e-6));
  CHK(eq_eps(voxsz[2], aabb->upper[2] - aabb->lower[2], 1.e-6));
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
    } else {
      CHK(depth == aabb->depth);
    }
    CHK(depth == voxels[i].depth);
    CHK(d3_eq_eps(aabb->lower, voxels[i].lower, 1.e-6));
    CHK(d3_eq_eps(aabb->upper, voxels[i].upper, 1.e-6));
    voxsz[0] = build_ctx->voxsz[0] * (double)(1<<n);
    voxsz[1] = build_ctx->voxsz[1] * (double)(1<<n);
    voxsz[2] = build_ctx->voxsz[2] * (double)(1<<n);
    CHK(eq_eps(voxsz[0], aabb->upper[0] - aabb->lower[0], 1.e-6));
    CHK(eq_eps(voxsz[1], aabb->upper[1] - aabb->lower[1], 1.e-6));
    CHK(eq_eps(voxsz[2], aabb->upper[2] - aabb->lower[2], 1.e-6));
  }

  return 1;
}

static void
test_at_accessor(struct svx_tree* oct, const size_t nvoxels[3])
{
  struct svx_tree_desc tree_desc;
  struct at_context ctx;
  size_t nvxls;
  double delta[3];
  double ocsz[3];
  size_t x, y, z;

  CHK(nvoxels != NULL);
  CHK(svx_tree_get_desc(oct, &tree_desc) == RES_OK);
  CHK(tree_desc.type == SVX_OCTREE);
  CHK(tree_desc.frame[0] == SVX_AXIS_X);
  CHK(tree_desc.frame[1] == SVX_AXIS_Y);
  CHK(tree_desc.frame[2] == SVX_AXIS_Z);

  ocsz[0] = tree_desc.upper[0] - tree_desc.lower[0];
  ocsz[1] = tree_desc.upper[1] - tree_desc.lower[1];
  ocsz[2] = tree_desc.upper[2] - tree_desc.lower[2];
  delta[0] = ocsz[0]/(double)nvoxels[0];
  delta[1] = ocsz[1]/(double)nvoxels[1];
  delta[2] = ocsz[2]/(double)nvoxels[2];

  nvxls = nvoxels[0];
  nvxls = MMAX(nvxls, nvoxels[1]);
  nvxls = MMAX(nvxls, nvoxels[2]);

  ctx.depth = tree_desc.depth;
  CHK(ctx.depth > 0);

  while(ctx.depth-- != 0) {
    const size_t iter = tree_desc.depth - ctx.depth - 1;
    double pos[3];
    double low[3];
    double upp[3];

    FOR_EACH(x, 0, nvxls) {
      pos[0] = tree_desc.lower[0] + ((double)x+1.0/(1u<<(2+iter)))*delta[0];
      low[0] = tree_desc.lower[0] + (double)x * delta[0];
      upp[0] = low[0] + delta[0];
      if(x*(size_t)(1u<<iter) >= nvoxels[0]) break;

      FOR_EACH(y, 0, nvxls) {
        pos[1] = tree_desc.lower[1] + ((double)y+1.0/(1u<<(2+iter)))*delta[1];
        low[1] = tree_desc.lower[1] + (double)y * delta[1];
        upp[1] = low[1] + delta[1];

        if(y*(size_t)(1u<<iter) >= nvoxels[1]) break;

        FOR_EACH(z, 0, nvxls) {
          struct svx_voxel vox;
          uint32_t ui3[3];
          uint64_t mcode;

          pos[2] = tree_desc.lower[2] + ((double)z+1.0/(1u<<(2+iter)))*delta[2];
          low[2] = tree_desc.lower[2] + (double)z * delta[2];
          upp[2] = low[2] + delta[2];

          if(z*(size_t)(1u<<iter) >= nvoxels[2]) break;

          d3_set(ctx.position, pos);
          CHK(svx_tree_at(oct, pos, max_lod, &ctx, &vox) == RES_OK);
          CHK(!SVX_VOXEL_NONE(&vox));

          ui3[0] = (uint32_t)x * (1u << iter) + ((1u << iter) - 1);
          ui3[1] = (uint32_t)y * (1u << iter) + ((1u << iter) - 1);
          ui3[2] = (uint32_t)z * (1u << iter) + ((1u << iter) - 1);
          ui3[0] = MMIN(ui3[0], (uint32_t)(nvoxels[0]-1));
          ui3[1] = MMIN(ui3[1], (uint32_t)(nvoxels[1]-1));
          ui3[2] = MMIN(ui3[2], (uint32_t)(nvoxels[2]-1));

          mcode = morton_xyz_encode_u21(ui3);
          CHK(*((double*)vox.data) == mcode);
          CHK(vox.is_leaf == (tree_desc.depth-1==ctx.depth));
          CHK(vox.depth == ctx.depth);
          CHK(d3_eq_eps(vox.lower, low, 1.e-6));
          CHK(d3_eq_eps(vox.upper, upp, 1.e-6));
        }
      }
    }

    nvxls = nvxls == 1 ? 0 : (nvxls + 1/*ceil*/) / 2;
    delta[0] = MMIN(delta[0] * 2, ocsz[0]);
    delta[1] = MMIN(delta[1] * 2, ocsz[1]);
    delta[2] = MMIN(delta[2] * 2, ocsz[2]);
  }
  CHK(nvxls == 0);
}

int
main(int argc, char** argv)
{
  struct svx_device* dev = NULL;
  struct svx_tree* oct = NULL;
  struct mem_allocator allocator;
  struct svx_voxel_desc voxdesc = SVX_VOXEL_DESC_NULL;
  struct svx_tree_desc tree_desc = SVX_TREE_DESC_NULL;
  struct build_context build_ctx;
  double low[3];
  double upp[3];
  size_t nvxls[3];
  struct leaves_context ctx;
  void* ptr = (void*)(intptr_t)0xDECAFBAD;
  FILE* stream = NULL;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);

  CHK(svx_device_create(NULL, &allocator, 1, &dev) == RES_OK);

  d3_splat(low, 0.0);
  d3_splat(upp, 1.0);
  nvxls[0] = nvxls[1] = nvxls[2] = 5;

  ctx.lower = low;
  ctx.upper = upp;
  ctx.nvoxels = nvxls;
  ctx.depth = 4;

  #define NEW_SCN svx_octree_create

  voxdesc.get = get;
  voxdesc.merge = keep_max;
  voxdesc.challenge_merge = no_merge;
  voxdesc.context = ptr;
  voxdesc.size = sizeof(double);
  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_OK);

  CHK(svx_tree_ref_get(NULL) == RES_BAD_ARG);
  CHK(svx_tree_ref_get(oct) == RES_OK);
  CHK(svx_tree_ref_put(NULL) == RES_BAD_ARG);
  CHK(svx_tree_ref_put(oct) == RES_OK);
  CHK(svx_tree_ref_put(oct) == RES_OK);

  upp[0] = low[0];
  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_BAD_ARG);
  upp[0] = 1.0;

  nvxls[2] = 0;
  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_BAD_ARG);
  nvxls[2] = nvxls[0];

  CHK(NEW_SCN(NULL, low, upp, nvxls, &voxdesc, &oct) == RES_BAD_ARG);
  CHK(NEW_SCN(dev, NULL, upp, nvxls, &voxdesc, &oct) == RES_BAD_ARG);
  CHK(NEW_SCN(dev, low, NULL, nvxls, &voxdesc, &oct) == RES_BAD_ARG);
  CHK(NEW_SCN(dev, low, upp, NULL, &voxdesc, &oct) == RES_BAD_ARG);
  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, NULL) == RES_BAD_ARG);

  voxdesc.get = NULL;
  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_BAD_ARG);
  voxdesc.get = get;

  voxdesc.merge = NULL;
  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_BAD_ARG);
  voxdesc.merge = keep_max;

  voxdesc.challenge_merge = NULL;
  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_BAD_ARG);
  voxdesc.challenge_merge = no_merge;

  voxdesc.size = 0;
  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_BAD_ARG);
  voxdesc.size = sizeof(double);

  voxdesc.size = SVX_MAX_SIZEOF_VOXEL + 1;
  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_BAD_ARG);
  voxdesc.size = sizeof(double);

  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_OK);

  CHK(svx_tree_for_each_leaf(oct, check_leaves, &ctx) == RES_OK);

  CHK(svx_tree_get_desc(NULL, &tree_desc) == RES_BAD_ARG);
  CHK(svx_tree_get_desc(oct, NULL) == RES_BAD_ARG);
  CHK(svx_tree_get_desc(oct, &tree_desc) == RES_OK);
  CHK(tree_desc.nleaves == 5*5*5);
  CHK(tree_desc.nvoxels == tree_desc.nleaves + 36/*#parents*/);
  CHK(tree_desc.depth == 4);
  CHK(tree_desc.type == SVX_OCTREE);

  d3_splat(low, 0);
  d3_splat(upp, 1);
  CHK(d3_eq(tree_desc.lower, low));
  CHK(d3_eq(tree_desc.upper, upp));

  test_at_accessor(oct, nvxls);

  CHK(stream = tmpfile());
  CHK(svx_tree_write(NULL, stream) == RES_BAD_ARG);
  CHK(svx_tree_write(oct, NULL) == RES_BAD_ARG);
  CHK(svx_tree_write(oct, stream) == RES_OK);

  CHK(svx_tree_ref_put(oct) == RES_OK);

  rewind(stream);
  CHK(svx_tree_create_from_stream(NULL, stream, &oct) == RES_BAD_ARG);
  CHK(svx_tree_create_from_stream(dev, NULL, &oct) == RES_BAD_ARG);
  CHK(svx_tree_create_from_stream(dev, stream, NULL) == RES_BAD_ARG);
  CHK(svx_tree_create_from_stream(dev, stream, &oct) == RES_OK);
  fclose(stream);

  test_at_accessor(oct, nvxls);
  CHK(svx_tree_ref_put(oct) == RES_OK);

  nvxls[0] = nvxls[1] = nvxls[2] = 32;
  voxdesc.challenge_merge = merge_level0;
  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_OK);
  CHK(svx_tree_get_desc(oct, &tree_desc) == RES_OK);
  CHK(tree_desc.nleaves == nvxls[0]*nvxls[1]*nvxls[2] / 8);
  CHK(tree_desc.nvoxels == (tree_desc.nleaves*8 - 1) / 7);
  CHK(tree_desc.depth == (size_t)log2i((int)(nvxls[0]/2))+1);
  CHK(tree_desc.type == SVX_OCTREE);

  dump_data(stdout, oct, TYPE_FLOAT, 1, write_scalars);

  CHK(svx_tree_ref_put(oct) == RES_OK);

  nvxls[0] = 32;
  nvxls[1] = 16;
  nvxls[2] = 33;

  build_ctx.max_depth = (size_t)log2i
    ((int)round_up_pow2(MMAX(MMAX(nvxls[0], nvxls[1]), nvxls[2])));

  d3_set(build_ctx.lower, low);
  d3_set(build_ctx.upper, upp);
  build_ctx.voxsz[0] = (upp[0]-low[0])/(double)nvxls[0];
  build_ctx.voxsz[1] = (upp[1]-low[1])/(double)nvxls[1];
  build_ctx.voxsz[2] = (upp[2]-low[2])/(double)nvxls[2];

  voxdesc.get = get_aabb;
  voxdesc.merge = merge_aabb;
  voxdesc.challenge_merge = challenge_aabb;
  voxdesc.context = &build_ctx;
  voxdesc.size = sizeof(struct aabb);

  CHK(NEW_SCN(dev, low, upp, nvxls, &voxdesc, &oct) == RES_OK);

  #undef NEW_SCN

  CHK(svx_device_ref_put(dev) == RES_OK);
  CHK(svx_tree_ref_put(oct) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

