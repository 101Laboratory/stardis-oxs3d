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

#ifndef SUVM_VOLUME_H
#define SUVM_VOLUME_H

#include "suvm_backend.h"

#include <rsys/dynamic_array_u32.h>
#include <rsys/dynamic_array_float.h>
#include <rsys/float3.h>
#include <rsys/ref_count.h>

/*
 * Tetrahedron geometric layout
 *
 *        3 (top vertex)  facet 0 = {0,1,2}
 *        +               facet 1 = {0,3,1}
 *       /|`.             facet 2 = {1,3,2}
 *      / |  `.           facet 3 = {2,3,0}
 *     /  |    `.
 *    /   |      `.
 * 0 +....|.........+ 2
 *    `.  |    _,-'
 *      `,|_,-'
 *        +
 *        1
 */

/* Forward declarations */
struct suvm_device;
struct mem_allocator;

struct buffer {
  void* mem; /* Raw memory block storing the data */
  size_t elmt_size; /* Size in bytes of one data */
  size_t elmt_stride; /* Size in bytes between two consecutive data */
  size_t elmt_alignment; /* Data alignment */
  size_t size; /* #data */
  struct mem_allocator* allocator;
};
static const struct buffer BUFFER_NULL;

enum node_type { NODE_INNER, NODE_LEAF };

/* Generic node */
struct node {
  enum node_type type;
};

/* Generate the dynamic array of BVH node */
#define DARRAY_NAME node
#define DARRAY_DATA struct node*
#include <rsys/dynamic_array.h>

/* Inner node */
struct ALIGN(16) node_inner {
  float low[2][3];
  float upp[2][3];
  struct node* children[2];
  struct node node;
};

/* Leaf node */
struct ALIGN(16) node_leaf {
  float low[3];
  uint32_t geom_id;
  float upp[3];
  uint32_t prim_id;
  struct node node;
};

struct suvm_volume {
  struct darray_u32 indices; /* List of uint32_t[4] */
  struct darray_float positions; /* List of float[3] */
  struct darray_float normals; /* List of per tetrahedron facet normals */
  struct buffer prim_data; /* Per primitive data */
  struct buffer vert_data; /* Per vertex data */

  int has_prim_data;
  int has_vert_data;

  float low[3], upp[3]; /* Volume AABB */

  RTCBVH bvh;
  struct node* bvh_root;

  struct suvm_device* dev;
  ref_T ref;
};

static FINLINE size_t
volume_get_primitives_count(const struct suvm_volume* vol)
{
  size_t sz = 0;
  ASSERT(vol);
  sz = darray_u32_size_get(&vol->indices);
  ASSERT(sz % 4/*#vertices per tetrahedron*/ == 0);
  return sz / 4;
}

static FINLINE size_t
volume_get_vertices_count(const struct suvm_volume* vol)
{
  size_t sz = 0;
  ASSERT(vol);
  sz = darray_float_size_get(&vol->positions);
  ASSERT(sz % 3/*#coords per vertex*/ == 0);
  return sz / 3;
}

static FINLINE size_t*
volume_primitive_get_indices
  (const struct suvm_volume* vol,
   const size_t iprim,
   size_t indices[4])
{
  const uint32_t* ids;
  ASSERT(vol && indices && iprim < volume_get_primitives_count(vol));
  ids = darray_u32_cdata_get(&vol->indices) + iprim*4/*#vertices per tetra*/;
  indices[0] = (size_t)ids[0];
  indices[1] = (size_t)ids[1];
  indices[2] = (size_t)ids[2];
  indices[3] = (size_t)ids[3];
  return indices;
}

static FINLINE float*
volume_primitive_get_vertex_position
  (const struct suvm_volume* vol,
   const size_t iprim,
   const size_t ivert, /* In [0, 3] */
   float pos[3])
{
  const uint32_t* ids;
  ASSERT(vol && iprim < volume_get_primitives_count(vol) && pos && ivert < 4);
  ids = darray_u32_cdata_get(&vol->indices) + iprim*4/*#vertex per tetra*/;
  pos[0] = darray_float_cdata_get(&vol->positions)[ids[ivert]*3 + 0];
  pos[1] = darray_float_cdata_get(&vol->positions)[ids[ivert]*3 + 1];
  pos[2] = darray_float_cdata_get(&vol->positions)[ids[ivert]*3 + 2];
  return pos;
}

static INLINE float*
volume_primitive_compute_facet_normal
  (const struct suvm_volume* vol,
   const size_t iprim,
   const int ifacet/*In [0,3]*/,
   float normal[3])
{
  const uint32_t* ids;
  const float* v0;
  const float* v1;
  const float* v2;

  float E0[3], E1[3];

  /* Facet indices, Ensure CCW ordering */
  const int facet[4][3] = {{0,1,2}, {0,3,1}, {1,3,2}, {2,3,0}};

  ASSERT(vol && iprim < volume_get_primitives_count(vol) && normal);
  ASSERT(ifacet < 4);

  /* Fetch tetrahedron indices */
  ids = darray_u32_cdata_get(&vol->indices) + iprim*4;

  /* Fetch facet vertices */
  v0 = darray_float_cdata_get(&vol->positions) + ids[facet[ifacet][0]]*3;
  v1 = darray_float_cdata_get(&vol->positions) + ids[facet[ifacet][1]]*3;
  v2 = darray_float_cdata_get(&vol->positions) + ids[facet[ifacet][2]]*3;

  /* Compute the normal */
  f3_sub(E0, v1, v0);
  f3_sub(E1, v2, v0);
  f3_cross(normal, E0, E1);
  f3_normalize(normal, normal);

  return normal;
}

static FINLINE float*
volume_primitive_get_facet_normal
  (const struct suvm_volume* vol,
   const size_t iprim,
   const int ifacet, /* In [0, 3] */
   float normal[3])
{
  ASSERT(vol && iprim < volume_get_primitives_count(vol) && normal);
  ASSERT(ifacet < 4);

  if(!darray_float_size_get(&vol->normals)) {
    volume_primitive_compute_facet_normal(vol, iprim, ifacet, normal);
  } else {
    const size_t id =
      (iprim*4/*#facets per tetra*/ + (size_t)ifacet)*3/*#coords per normal*/;
    ASSERT(id + 3 <= darray_float_size_get(&vol->normals));
    normal[0] = darray_float_cdata_get(&vol->normals)[id+0];
    normal[1] = darray_float_cdata_get(&vol->normals)[id+1];
    normal[2] = darray_float_cdata_get(&vol->normals)[id+2];
  }
  return normal;
}

static INLINE const void*
buffer_at(const struct buffer* buf, const size_t i)
{
  void* data;
  ASSERT(buf && i < buf->size);
  data = (char*)buf->mem + i * buf->elmt_stride;
  ASSERT(IS_ALIGNED(data, buf->elmt_alignment));
  return data;
}

static INLINE const void*
volume_primitive_get_data(const struct suvm_volume* vol, const size_t iprim)
{
  ASSERT(vol && vol->has_prim_data && iprim < volume_get_primitives_count(vol));
  return buffer_at(&vol->prim_data, iprim);
}

static INLINE const void*
volume_primitive_get_vertex_data
  (const struct suvm_volume* vol,
   const size_t iprim,
   const size_t ivert /* In [0, 3] */)
{
  const uint32_t* ids;
  ASSERT(vol && vol->has_vert_data && iprim < volume_get_primitives_count(vol));
  ASSERT(ivert < 4);
  ids = darray_u32_cdata_get(&vol->indices) + iprim*4/*#vertex per tetra*/;
  return buffer_at(&vol->vert_data, ids[ivert]);
}

static INLINE void
volume_primitive_setup
  (const struct suvm_volume* vol,
   const size_t iprim,
   struct suvm_primitive* prim)
{
  ASSERT(vol && prim && iprim < volume_get_primitives_count(vol));
  prim->nvertices = 4;
  prim->iprim = iprim;
  prim->volume__ = vol;
  volume_primitive_get_indices(vol, prim->iprim, prim->indices);
  if(vol->has_prim_data) {
    prim->data = volume_primitive_get_data(vol, prim->iprim);
  }
  if(vol->has_vert_data) {
    prim->vertex_data[0] = volume_primitive_get_vertex_data(vol, iprim, 0);
    prim->vertex_data[1] = volume_primitive_get_vertex_data(vol, iprim, 1);
    prim->vertex_data[2] = volume_primitive_get_vertex_data(vol, iprim, 2);
    prim->vertex_data[3] = volume_primitive_get_vertex_data(vol, iprim, 3);
  }
}

extern LOCAL_SYM void
tetrahedron_setup
  (const struct suvm_volume* vol,
   const float low[3], /* NULL <=> compute on the fly */
   const float upp[3], /* NULL <=> compute on the fly */
   const size_t iprim,
   struct suvm_polyhedron* polyhedron);

#endif /* SUVM_VOLUME_H */

