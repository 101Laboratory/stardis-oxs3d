/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#include "suvm.h"
#include "suvm_c.h"
#include "suvm_device.h"
#include "suvm_volume.h"

#include <rsys/cstr.h>
#include <rsys/dynamic_array_double.h>
#include <rsys/dynamic_array_size_t.h>
#include <rsys/ref_count.h>

/* Generate the dynamic array of RTCBuildPrimitive */
#define DARRAY_NAME rtc_prim
#define DARRAY_DATA struct RTCBuildPrimitive
#define DARRAY_ALIGNMENT ALIGNOF(struct RTCBuildPrimitive)
#include <rsys/dynamic_array.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
check_tetrahedral_mesh_args(const struct suvm_tetrahedral_mesh_args* args)
{
  return args
      && args->ntetrahedra
      && args->nvertices
      && args->get_indices
      && args->get_position;
}

static INLINE void
buffer_release(struct buffer* buf)
{
  ASSERT(buf);
  if(buf->mem) MEM_RM(buf->allocator, buf->mem);
  *buf = BUFFER_NULL;
}

static res_T
buffer_init_from_data
  (struct mem_allocator* mem_allocator, /* May be NULL */
   struct buffer* buf,
   const struct suvm_data* data,
   const size_t ndata,
   void* context)
{
  struct mem_allocator* allocator = NULL;
  size_t idata;
  res_T res = RES_OK;
  ASSERT(buf && data && data->get && ndata);

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;

  *buf = BUFFER_NULL;
  if(!data->size || !IS_POW2(data->alignment)) {
    res = RES_BAD_ARG;
    goto error;
  }
  buf->elmt_size = data->size;
  buf->elmt_alignment = data->alignment;
  buf->elmt_stride = ALIGN_SIZE(data->size, data->alignment);
  buf->size = ndata;
  buf->allocator = allocator;

  /* Allocate the required memory */
  buf->mem = MEM_ALLOC_ALIGNED
    (buf->allocator, buf->elmt_stride*ndata, buf->elmt_alignment);
  if(!buf->mem) {
    res = RES_MEM_ERR;
    goto error;
  }

  /* Fill the buffer with the submitted data */
  FOR_EACH(idata, 0, ndata) {
    char* elmt = (char*)buf->mem + idata*buf->elmt_stride;
    data->get(idata, elmt, context);

    /* Clean up padding bytes to initialise them regarding their possible
     * hashing by the suvm_volume_compute_hash function */
    if(buf->elmt_stride != buf->elmt_size) {
      memset(elmt + buf->elmt_size, 0, buf->elmt_stride - buf->elmt_size);
    }
  }

exit:
  return res;
error:
  buffer_release(buf);
  goto exit;
}

static res_T
setup_tetrahedral_mesh_indices
  (struct suvm_volume* vol, const struct suvm_tetrahedral_mesh_args* args)
{
  size_t itetra;
  res_T res = RES_OK;
  ASSERT(vol && args);

  res = darray_u32_resize
    (&vol->indices, args->ntetrahedra*4/*#vertices per tetra*/);
  if(res != RES_OK) goto error;

  /* Locally copy the indices */
  FOR_EACH(itetra, 0, args->ntetrahedra) {
    size_t tetra[4];
    uint32_t* tetra_u32 = darray_u32_data_get(&vol->indices)+itetra*4;
    args->get_indices(itetra, tetra, args->context);
    ASSERT(tetra[0] < UINT32_MAX);
    ASSERT(tetra[1] < UINT32_MAX);
    ASSERT(tetra[2] < UINT32_MAX);
    ASSERT(tetra[3] < UINT32_MAX);
    tetra_u32[0] = (uint32_t)tetra[0];
    tetra_u32[1] = (uint32_t)tetra[1];
    tetra_u32[2] = (uint32_t)tetra[2];
    tetra_u32[3] = (uint32_t)tetra[3];
  }

exit:
  return res;
error:
  darray_u32_purge(&vol->indices);
  goto exit;
}

static res_T
setup_tetrahedral_mesh_position
  (struct suvm_volume* vol, const struct suvm_tetrahedral_mesh_args* args)
{
  size_t ivert;
  res_T res = RES_OK;
  ASSERT(vol && args);

  res = darray_float_resize
    (&vol->positions, args->nvertices*3/*#coords per vertex*/);
  if(res != RES_OK) goto error;

  /* Locally copy the positions */
  FOR_EACH(ivert, 0, args->nvertices) {
    double vertd[3];
    float* vertf = darray_float_data_get(&vol->positions)+ivert*3;
    args->get_position(ivert, vertd, args->context);
    vertf[0] = (float)vertd[0];
    vertf[1] = (float)vertd[1];
    vertf[2] = (float)vertd[2];
  }

exit:
  return res;
error:
  darray_float_purge(&vol->positions);
  goto exit;
}

static res_T
setup_tetrahedral_mesh
  (struct suvm_volume* vol, const struct suvm_tetrahedral_mesh_args* args)
{
  res_T res = RES_OK;
  ASSERT(vol && args);

  res = setup_tetrahedral_mesh_indices(vol, args);
  if(res != RES_OK) goto error;
  res = setup_tetrahedral_mesh_position(vol, args);
  if(res != RES_OK) goto error;

   /* Store the per tetrahedron data */
  if(args->tetrahedron_data.get) {
    res = buffer_init_from_data(vol->dev->allocator, &vol->prim_data,
      &args->tetrahedron_data, args->ntetrahedra, args->context);
    if(res != RES_OK) goto error;
    vol->has_prim_data = 1;
  }

  /* Store the per vertex data */
  if(args->vertex_data.get) {
    res = buffer_init_from_data(vol->dev->allocator, &vol->vert_data,
      &args->vertex_data, args->nvertices, args->context);
    if(res != RES_OK) goto error;
    vol->has_vert_data = 1;
  }

exit:
  return res;
error:
  darray_u32_purge(&vol->indices);
  darray_float_purge(&vol->positions);
  darray_float_purge(&vol->normals);
  buffer_release(&vol->prim_data);
  buffer_release(&vol->vert_data);
  goto exit;
}

static INLINE void*
rtc_node_inner_create
  (RTCThreadLocalAllocator allocator, unsigned int nchildren, void* ctx)
{
  struct node_inner* inner = NULL;
  ASSERT(nchildren == 2);
  (void)ctx, (void)nchildren;
  inner = rtcThreadLocalAlloc(allocator, sizeof(*inner), 16);
  if(!inner) return NULL;
  inner->node.type = NODE_INNER;
  return &inner->node;
}

static INLINE void
rtc_node_inner_set_children
  (void* ptr, void* children[2], unsigned nchildren, void* ctx)
{
  struct node_inner* inner = CONTAINER_OF(ptr, struct node_inner, node);
  struct node* node = ptr;
  ASSERT(node && node->type == NODE_INNER && children && nchildren == 2);
  (void)ctx, (void)nchildren, (void)node;
  inner->children[0] = children[0];
  inner->children[1] = children[1];
}

static INLINE void
rtc_node_inner_set_bounds
  (void* ptr,
   const struct RTCBounds* bounds[2],
   unsigned nchildren,
   void* ctx)
{
  struct node_inner* inner = CONTAINER_OF(ptr, struct node_inner, node);
  struct node* node = ptr;
  ASSERT(node && node->type == NODE_INNER && bounds && nchildren == 2);
  (void)ctx, (void)nchildren, (void)node;

  /* Setup the AABB of the 1st child */
  inner->low[0][0] = bounds[0]->lower_x;
  inner->low[0][1] = bounds[0]->lower_y;
  inner->low[0][2] = bounds[0]->lower_z;
  inner->upp[0][0] = bounds[0]->upper_x;
  inner->upp[0][1] = bounds[0]->upper_y;
  inner->upp[0][2] = bounds[0]->upper_z;

  /* Setup the AABB of the 2nd child */
  inner->low[1][0] = bounds[1]->lower_x;
  inner->low[1][1] = bounds[1]->lower_y;
  inner->low[1][2] = bounds[1]->lower_z;
  inner->upp[1][0] = bounds[1]->upper_x;
  inner->upp[1][1] = bounds[1]->upper_y;
  inner->upp[1][2] = bounds[1]->upper_z;
}

static INLINE void*
rtc_node_leaf_create
  (RTCThreadLocalAllocator allocator,
   const struct RTCBuildPrimitive* prim,
   size_t nprims,
   void* ctx)
{
  struct node_leaf* leaf = NULL;
  ASSERT(prim && nprims == 1);
  (void)ctx, (void)nprims;

  leaf = rtcThreadLocalAlloc(allocator, sizeof(*leaf), 16);
  if(!leaf) return NULL;

  leaf->low[0] = prim->lower_x;
  leaf->low[1] = prim->lower_y;
  leaf->low[2] = prim->lower_z;
  leaf->upp[0] = prim->upper_x;
  leaf->upp[1] = prim->upper_y;
  leaf->upp[2] = prim->upper_z;
  leaf->geom_id = prim->geomID;
  leaf->prim_id = prim->primID;
  leaf->node.type = NODE_LEAF;
  return &leaf->node;
}

static res_T
build_bvh(struct suvm_volume* vol)
{
  struct darray_rtc_prim rtc_prims;
  struct RTCBuildArguments args;
  size_t iprim, nprims;
  int rtc_prims_is_init = 0;
  res_T res = RES_OK;
  ASSERT(vol);

  nprims = volume_get_primitives_count(vol);

  /* Create the BVH */
  vol->bvh = rtcNewBVH(vol->dev->rtc);
  if(!vol->bvh) {
    const enum RTCError rtc_err = rtcGetDeviceError(vol->dev->rtc);
    log_err(vol->dev, "Could not create the BVH -- %s.\n",
      rtc_error_string(rtc_err));
    res = rtc_error_to_res_T(rtc_err);
    goto error;
  }

  /* Allocate the array of geometric primitives */
  darray_rtc_prim_init(vol->dev->allocator, &rtc_prims);
  rtc_prims_is_init = 1;
  res = darray_rtc_prim_resize(&rtc_prims, nprims);
  if(res != RES_OK) goto error;

  /* Setup the primitive array */
  FOR_EACH(iprim, 0, nprims) {
    const uint32_t* ids = darray_u32_cdata_get(&vol->indices) + iprim*4;
    const float* verts[4];
    struct RTCBuildPrimitive* prim = darray_rtc_prim_data_get(&rtc_prims)+iprim;
    double low[3], upp[3];

    ASSERT(ids[0] < volume_get_vertices_count(vol));
    ASSERT(ids[1] < volume_get_vertices_count(vol));
    ASSERT(ids[2] < volume_get_vertices_count(vol));
    ASSERT(ids[3] < volume_get_vertices_count(vol));

    /* Fetch the tetrahedron vertices */
    verts[0] = darray_float_cdata_get(&vol->positions) + ids[0]*3/*#coords*/;
    verts[1] = darray_float_cdata_get(&vol->positions) + ids[1]*3/*#coords*/;
    verts[2] = darray_float_cdata_get(&vol->positions) + ids[2]*3/*#coords*/;
    verts[3] = darray_float_cdata_get(&vol->positions) + ids[3]*3/*#coords*/;

    /* Compute the tetrahedron AABB */
    low[0] = MMIN(MMIN(verts[0][0],verts[1][0]), MMIN(verts[2][0],verts[3][0]));
    low[1] = MMIN(MMIN(verts[0][1],verts[1][1]), MMIN(verts[2][1],verts[3][1]));
    low[2] = MMIN(MMIN(verts[0][2],verts[1][2]), MMIN(verts[2][2],verts[3][2]));
    upp[0] = MMAX(MMAX(verts[0][0],verts[1][0]), MMAX(verts[2][0],verts[3][0]));
    upp[1] = MMAX(MMAX(verts[0][1],verts[1][1]), MMAX(verts[2][1],verts[3][1]));
    upp[2] = MMAX(MMAX(verts[0][2],verts[1][2]), MMAX(verts[2][2],verts[3][2]));

    /* Setup the build primitive */
    prim->lower_x = (float)low[0];
    prim->lower_y = (float)low[1];
    prim->lower_z = (float)low[2];
    prim->upper_x = (float)upp[0];
    prim->upper_y = (float)upp[1];
    prim->upper_z = (float)upp[2];
    prim->geomID = 0;
    prim->primID = (unsigned int)iprim;
  }

  /* Setup the build arguments */
  args = rtcDefaultBuildArguments();
  args.byteSize = sizeof(args);
  args.buildQuality = RTC_BUILD_QUALITY_MEDIUM;
  args.buildFlags = RTC_BUILD_FLAG_NONE;
  args.maxBranchingFactor = 2;
  args.maxDepth = 1024;
  args.sahBlockSize = 1;
  args.minLeafSize = 1;
  args.maxLeafSize = 1;
  args.traversalCost = 1.f;
  args.intersectionCost = 10.f;
  args.bvh = vol->bvh;
  args.primitives = darray_rtc_prim_data_get(&rtc_prims);
  args.primitiveCount = nprims;
  args.primitiveArrayCapacity = darray_rtc_prim_capacity(&rtc_prims);
  args.createNode = rtc_node_inner_create;
  args.setNodeChildren = rtc_node_inner_set_children;
  args.setNodeBounds = rtc_node_inner_set_bounds;
  args.createLeaf = rtc_node_leaf_create;
  args.splitPrimitive = NULL;
  args.buildProgress = NULL;
  args.userPtr = vol;

  /* Build the BVH */
  vol->bvh_root = rtcBuildBVH(&args);
  if(!vol->bvh_root) {
    const enum RTCError rtc_err = rtcGetDeviceError(vol->dev->rtc);
    log_err(vol->dev, "Error building the BVH -- %s.\n",
      rtc_error_string(rtc_err));
    res = rtc_error_to_res_T(rtc_err);
    goto error;
  }

exit:
  if(rtc_prims_is_init) darray_rtc_prim_release(&rtc_prims);
  return res;
error:
  if(vol->bvh) {
    rtcReleaseBVH(vol->bvh);
    vol->bvh = NULL;
  }
  goto exit;
}

static res_T
setup_tetrahedra_normals
  (struct suvm_volume* vol,
   const int precompute_normals)
{
  size_t itetra, ntetra;
  int fixup = 0;
  res_T res = RES_OK;
  ASSERT(vol);

  ntetra = volume_get_primitives_count(vol);

  if(precompute_normals) {
    res = darray_float_resize(&vol->normals,
      ntetra * 4/*#facets per tetrahedron*/ * 3/*#coords per normal*/);
    if(res != RES_OK) goto error;
  }

  FOR_EACH(itetra, 0, ntetra) {
    uint32_t* ids = NULL;
    const float* vert0 = NULL;
    const float* vert3 = NULL;
    float normal0[3];
    float v0[3];
    int flip = 0;

    /* Fetch tetrahedron indices */
    ids = darray_u32_data_get(&vol->indices) + itetra*4;
    ASSERT(ids[0] < volume_get_vertices_count(vol));
    ASSERT(ids[1] < volume_get_vertices_count(vol));
    ASSERT(ids[2] < volume_get_vertices_count(vol));
    ASSERT(ids[3] < volume_get_vertices_count(vol));

    /* Fetch the tetrahedron vertices */
    vert0 = darray_float_cdata_get(&vol->positions) + ids[0]*3/*#coords*/;
    vert3 = darray_float_cdata_get(&vol->positions) + ids[3]*3/*#coords*/;

    /* Compute the normal of the 1st facet */
    volume_primitive_compute_facet_normal(vol, itetra, 0, normal0);

    /* Check that the normal of the 1st facet looks the fourth vertex */
    if(f3_dot(normal0, f3_sub(v0, vert3, vert0)) < 0) {
      fixup = flip = 1;
      /* Revert tetrahedron orientation */
      SWAP(uint32_t, ids[1], ids[2]);
    }

    /* Precompute and store the facet the normals */
    if(precompute_normals) {
      float* n0 = darray_float_data_get(&vol->normals) + (itetra*4 + 0)*3;
      float* n1 = darray_float_data_get(&vol->normals) + (itetra*4 + 1)*3;
      float* n2 = darray_float_data_get(&vol->normals) + (itetra*4 + 2)*3;
      float* n3 = darray_float_data_get(&vol->normals) + (itetra*4 + 3)*3;
      if(!flip) {
        /* Copy the already computed normal of the facet 0 */
        n0[0] = normal0[0];
        n0[1] = normal0[1];
        n0[2] = normal0[2];
      } else {
        /* We could store the negated normal of the facet 0 but we recompute it
         * from scratch to ensure strict equivalence if it is not stored; since
         * tetrahedron vertices were flipped the negated normal and the
         * recomputed one are not strictly equals */
        volume_primitive_compute_facet_normal(vol, itetra, 0, n0);
      }
      volume_primitive_compute_facet_normal(vol, itetra, 1, n1);
      volume_primitive_compute_facet_normal(vol, itetra, 2, n2);
      volume_primitive_compute_facet_normal(vol, itetra, 3, n3);
    }

#ifndef NDEBUG
    {
      const float* verts[4];
      float normals[4][3];
      float pt[3], v1[3];

      verts[0] = vert0;
      verts[1] = darray_float_cdata_get(&vol->positions) + ids[1]*3/*#coords*/;
      verts[2] = darray_float_cdata_get(&vol->positions) + ids[2]*3/*#coords*/;
      verts[3] = vert3;

      /* Compute the position at the center of the tetrahedron */
      pt[0] = (verts[0][0] + verts[1][0] + verts[2][0] + verts[3][0]) * 0.25f;
      pt[1] = (verts[0][1] + verts[1][1] + verts[2][1] + verts[3][1]) * 0.25f;
      pt[2] = (verts[0][2] + verts[1][2] + verts[2][2] + verts[3][2]) * 0.25f;

      /* Fetch the tetrahedron normals */
      volume_primitive_get_facet_normal(vol, itetra, 0, normals[0]);
      volume_primitive_get_facet_normal(vol, itetra, 1, normals[1]);
      volume_primitive_get_facet_normal(vol, itetra, 2, normals[2]);
      volume_primitive_get_facet_normal(vol, itetra, 3, normals[3]);

      /* Check normals orientation */
      f3_sub(v0, pt, verts[0]);
      f3_sub(v1, pt, verts[3]);
      ASSERT(f3_dot(normals[0], v0) >= 0);
      ASSERT(f3_dot(normals[1], v1) >= 0);
      ASSERT(f3_dot(normals[2], v1) >= 0);
      ASSERT(f3_dot(normals[3], v1) >= 0);
    }
#endif
  }

  if(fixup) {
    log_warn(vol->dev, "Tetrahedra were not correctly oriented regarding the "
      "Star-UVM convention.\n");
  }

exit:
  return res;
error:
  darray_float_purge(&vol->normals);
  goto exit;
}

static void
volume_release(ref_T* ref)
{
  struct suvm_volume* vol = NULL;
  struct suvm_device* dev = NULL;
  ASSERT(ref);
  vol = CONTAINER_OF(ref, struct suvm_volume, ref);
  dev = vol->dev;
  darray_u32_release(&vol->indices);
  darray_float_release(&vol->positions);
  darray_float_release(&vol->normals);
  buffer_release(&vol->prim_data);
  buffer_release(&vol->vert_data);
  if(vol->bvh) rtcReleaseBVH(vol->bvh);
  MEM_RM(dev->allocator, vol);
  SUVM(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
suvm_tetrahedral_mesh_create
  (struct suvm_device* dev,
   const struct suvm_tetrahedral_mesh_args* args,
   struct suvm_volume** out_vol)
{
  struct suvm_volume* vol = NULL;
  res_T res = RES_OK;

  if(!dev || !check_tetrahedral_mesh_args(args) || !out_vol) {
    res = RES_BAD_ARG;
    goto error;
  }

  vol = MEM_CALLOC(dev->allocator, 1, sizeof(*vol));
  if(!vol) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&vol->ref);
  SUVM(device_ref_get(dev));
  vol->dev = dev;
  darray_u32_init(dev->allocator, &vol->indices);
  darray_float_init(dev->allocator, &vol->positions);
  darray_float_init(dev->allocator, &vol->normals);

  /* Locally copy the volumetric mesh data */
  res = setup_tetrahedral_mesh(vol, args);
  if(res != RES_OK) goto error;

  /* Ensure that the vertices of the tetrahedra are well ordered regarding the
   * normal convention and store them if required */
  res = setup_tetrahedra_normals(vol, args->precompute_normals);
  if(res != RES_OK) goto error;

  /* Build the BVH of the volumetric mesh */
  res = build_bvh(vol);
  if(res != RES_OK) goto error;

  /* Setup the volume AABB */
  if(vol->bvh_root->type == NODE_LEAF) {
    const struct node_leaf* leaf = NULL;
    leaf = CONTAINER_OF(vol->bvh_root, struct node_leaf, node);
    vol->low[0] = leaf->low[0];
    vol->low[1] = leaf->low[1];
    vol->low[2] = leaf->low[2];
    vol->upp[0] = leaf->upp[0];
    vol->upp[1] = leaf->upp[1];
    vol->upp[2] = leaf->upp[2];
  } else {
    const struct node_inner* inner = NULL;
    inner = CONTAINER_OF(vol->bvh_root, struct node_inner, node);
    vol->low[0] = MMIN(inner->low[0][0], inner->low[1][0]);
    vol->low[1] = MMIN(inner->low[0][1], inner->low[1][1]);
    vol->low[2] = MMIN(inner->low[0][2], inner->low[1][2]);
    vol->upp[0] = MMAX(inner->upp[0][0], inner->upp[1][0]);
    vol->upp[1] = MMAX(inner->upp[0][1], inner->upp[1][1]);
    vol->upp[2] = MMAX(inner->upp[0][2], inner->upp[1][2]);
  }

exit:
  if(out_vol) *out_vol = vol;
  return res;
error:
  if(vol) { SUVM(volume_ref_put(vol)); vol = NULL; }
  goto exit;
}

res_T
suvm_volume_ref_get(struct suvm_volume* vol)
{
  if(!vol) return RES_BAD_ARG;
  ref_get(&vol->ref);
  return RES_OK;
}

res_T
suvm_volume_ref_put(struct suvm_volume* vol)
{
  if(!vol) return RES_BAD_ARG;
  ref_put(&vol->ref, volume_release);
  return RES_OK;
}

res_T
suvm_volume_get_aabb
  (const struct suvm_volume* volume,
   double lower[3],
   double upper[3])
{
  if(!volume || !lower || !upper) return RES_BAD_ARG;
  lower[0] = volume->low[0];
  lower[1] = volume->low[1];
  lower[2] = volume->low[2];
  upper[0] = volume->upp[0];
  upper[1] = volume->upp[1];
  upper[2] = volume->upp[2];
  return RES_OK;
}

res_T
suvm_volume_get_primitives_count(const struct suvm_volume* vol, size_t* nprims)
{
  if(!vol || !nprims) return RES_BAD_ARG;
  *nprims = volume_get_primitives_count(vol);
  return RES_OK;
}

res_T
suvm_volume_get_primitive
  (const struct suvm_volume* vol,
   const size_t iprim,
   struct suvm_primitive* prim)
{
  if(!vol|| !prim || iprim >= volume_get_primitives_count(vol))
    return RES_BAD_ARG;
  volume_primitive_setup(vol, iprim, prim);
  return RES_OK;
}

res_T
suvm_volume_compute_hash
  (const struct suvm_volume* vol,
   const int cpnt_mask,
   hash256_T hash)
{
  struct sha256_ctx ctx;
  res_T res = RES_OK;

  if(!vol || !hash) {
    res = RES_BAD_ARG;
    goto error;
  }

  sha256_ctx_init(&ctx);

  if(cpnt_mask & SUVM_POSITIONS) {
    const float* pos = darray_float_cdata_get(&vol->positions);
    const size_t n = darray_float_size_get(&vol->positions);
    sha256_ctx_update(&ctx, (const char*)pos, sizeof(*pos)*n);
  }
  if(cpnt_mask & SUVM_INDICES) {
    const uint32_t* ids = darray_u32_cdata_get(&vol->indices);
    const size_t n = darray_u32_size_get(&vol->indices);
    sha256_ctx_update(&ctx, (const char*)ids, sizeof(*ids)*n);
  }
  if(cpnt_mask & SUVM_PRIMITIVE_DATA) {
    const size_t sz = vol->prim_data.size * vol->prim_data.elmt_stride;
    sha256_ctx_update(&ctx, vol->prim_data.mem, sz);
  }
  if(cpnt_mask & SUVM_VERTEX_DATA) {
    const size_t sz = vol->vert_data.size * vol->vert_data.elmt_stride;
    sha256_ctx_update(&ctx, vol->vert_data.mem, sz);
  }

  sha256_ctx_finalize(&ctx, hash);

exit:
  return res;
error:
  goto exit;
}

res_T
suvm_volume_get_mesh_desc
  (const struct suvm_volume* vol,
   struct suvm_mesh_desc* desc)
{
  res_T res = RES_OK;

  if(!vol || !desc) {
    res = RES_BAD_ARG;
    goto error;
  }

  desc->positions = darray_float_cdata_get(&vol->positions);
  desc->indices = darray_u32_cdata_get(&vol->indices);
  desc->dvertex = 3;
  desc->dprimitive = 4;
  desc->nvertices = darray_float_size_get(&vol->positions) / desc->dvertex;
  desc->nprimitives = darray_u32_size_get(&vol->indices) / desc->dprimitive;

exit:
  return res;
error:
  goto exit;
}

res_T
suvm_mesh_desc_compute_hash(const struct suvm_mesh_desc* desc, hash256_T hash)
{
  struct sha256_ctx ctx;

  if(!desc || !hash) return RES_BAD_ARG;

  #define HASH(Var, Nb) \
    sha256_ctx_update(&ctx, (const char*)(Var), sizeof(*Var)*(Nb));

  sha256_ctx_init(&ctx);
  HASH(desc->positions, desc->nvertices*desc->dvertex);
  HASH(desc->indices, desc->nprimitives*desc->dprimitive);
  HASH(&desc->nvertices, 1);
  HASH(&desc->nprimitives, 1);
  HASH(&desc->dvertex, 1);
  HASH(&desc->dprimitive, 1);
  sha256_ctx_finalize(&ctx, hash);

  #undef HASH

  return RES_OK;
}
