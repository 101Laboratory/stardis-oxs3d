/* Copyright (C) 2014-2017, 2021-2023 Vincent Forest (vaplv@free.fr)
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

#include "polygon.h"

#include <rsys/float3.h>
#include <rsys/dynamic_array_u32.h>
#include <rsys/hash_table.h>
#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#include <float.h>

/* Declare the htable_u32 data structure */
#define HTABLE_NAME u32
#define HTABLE_KEY uint32_t
#define HTABLE_DATA char
#include <rsys/hash_table.h>

struct vertex_node {
  uint32_t next, prev; /* Index toward the next/prev linked vertex */
  float pos[3]; /* World position of the vertex */
};

#define DARRAY_NAME vertex_node
#define DARRAY_DATA struct vertex_node
#include <rsys/dynamic_array.h>

struct polygon {
  ref_T ref;
  struct mem_allocator* allocator;

  struct darray_vertex_node pool; /* Linked list of vertices */
  struct htable_u32 vertices_concave; /* Set of concave vertex nodes */
  struct darray_u32 triangle_ids;/* Vertex indices defining a triangular mesh */
  uint32_t vertices; /* Index toward the first vertex */
  uint32_t nvertices; /* Total vertices count. May be == to the size of pool */
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
/* Remove the last vertices if they are the same of the first one */
static void
cleanup_polygon(struct polygon* poly)
{
  struct vertex_node* first;
  struct vertex_node* last;
  struct vertex_node* nodes;
  ASSERT(poly);

  if(poly->nvertices <= 1) return;

  nodes = darray_vertex_node_data_get(&poly->pool);
  first = nodes + poly->vertices;

  for(;;) {
    last = nodes + first->prev;
    if(!f3_eq_eps(first->pos, last->pos, 1.e-6f) || first == last)
      break;

    nodes[last->prev].next = last->next;
    nodes[last->next].prev = last->prev;
    --poly->nvertices;
  }
}


static void
node_normal_compute
  (const struct polygon* poly,
   const uint32_t inode,
   float normal[3])
{
  float e0[3], e1[3];
  const struct vertex_node* nodes;
  ASSERT(poly && normal && inode < darray_vertex_node_size_get(&poly->pool));
  ASSERT(poly->nvertices >= 3);
  nodes = darray_vertex_node_cdata_get(&poly->pool);
  f3_sub(e0, nodes[nodes[inode].prev].pos, nodes[inode].pos);
  f3_sub(e1, nodes[nodes[inode].next].pos, nodes[inode].pos);
  f3_cross(normal, e0, e1);
}

static void /* Compute the normal of a convex vertex */
convex_normal_compute(struct polygon* poly, float normal[3])
{
  float bbox_min[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
  float bbox_max[3] = {-FLT_MAX,-FLT_MAX,-FLT_MAX };
  struct vertex_node* nodes;
  uint32_t iconvex = 0;
  uint32_t inode;
  uint32_t ivert;
  ASSERT(poly && normal && poly->nvertices >= 3);

  nodes = darray_vertex_node_data_get(&poly->pool);
  inode = poly->vertices;

  /* Look for a convex vertex, i.e. a vertex lying on the polygon bbox */
  FOR_EACH(ivert, 0, poly->nvertices) {
    struct vertex_node* vertex = nodes + inode;
    int i;
    FOR_EACH(i, 0, 3) {
      if(bbox_min[i] > vertex->pos[i]) {
        bbox_min[i] = vertex->pos[i];
        iconvex = inode;
      }
      if(bbox_max[i] < vertex->pos[i]) {
        bbox_max[i] = vertex->pos[i];
        iconvex = inode;
      }
    }
    inode = vertex->next;
  }
  ASSERT(inode == poly->vertices);
  node_normal_compute(poly, iconvex, normal);
}

static char
node_is_an_ear
  (const struct polygon* poly,
   struct htable_u32* vertices_concave,
   const uint32_t inode)
{
  struct htable_u32_iterator it, it_end;
  const struct vertex_node* nodes;
  float E1[3], E2[3], P[3], normal[3];
  float len, det;
  (void)len;

  ASSERT(poly && vertices_concave);
  ASSERT(inode < darray_vertex_node_size_get(&poly->pool));

  if(!htable_u32_size_get(vertices_concave)) /* Polygon is convex */
    return 1;
  if(htable_u32_find(vertices_concave, &inode)) /* `Concave' vertex */
    return 0;

  nodes = darray_vertex_node_cdata_get(&poly->pool);

  /* Setup the "Moller trumbore" normal/{prev, curr, next} triangle test */
  f3_sub(E1, nodes[nodes[inode].prev].pos, nodes[inode].pos);
  f3_sub(E2, nodes[nodes[inode].next].pos, nodes[inode].pos);
  f3_cross(normal, E1, E2);
  len = f3_normalize(normal, normal);
  if(eq_epsf(len, 0.f, 1.e-6f)) return 1;

  f3_cross(P, normal, E2);
  det = f3_dot(P, E1);

  /* Check that {prev, curr, next} doesn't contain a concave vertex */
  htable_u32_begin(vertices_concave, &it);
  htable_u32_end(vertices_concave, &it_end);
  while(!htable_u32_iterator_eq(&it, &it_end)) {
    float T[3], u;
    const uint32_t inode_concave = *htable_u32_iterator_key_get(&it);
    htable_u32_iterator_next(&it);
    if(inode_concave == nodes[inode].next
    || inode_concave == nodes[inode].prev)
      continue;
    /* Project the concave vertex position onto the {prev, curr, next} triangle
     * plane and test if it lies into it */
    f3_sub(T, nodes[inode_concave].pos, nodes[inode].pos);
    u = f3_dot(T, P) / det;
    if(u >= 0.f && u <= 1.f) {
      float Q[3], v;
      f3_cross(Q, T, E1);
      v = f3_dot(Q, normal) / det;
      if(v >= 0.f && v <= 1.f && (u + v) <= 1.f)
        return 0;
    }
  }
  return 1;
}

static void
release_polygon(ref_T* ref)
{
  struct polygon* poly = CONTAINER_OF(ref, struct polygon, ref);
  ASSERT(ref);
  darray_vertex_node_release(&poly->pool);
  htable_u32_release(&poly->vertices_concave);
  darray_u32_release(&poly->triangle_ids);
  MEM_RM(poly->allocator, poly);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
polygon_create(struct mem_allocator* allocator, struct polygon** out_polygon)
{
  struct polygon* poly = NULL;
  struct mem_allocator* mem_allocator;
  res_T res = RES_OK;

  if(!out_polygon) {
    res = RES_BAD_ARG;
    goto error;
  }
  mem_allocator = allocator ? allocator : &mem_default_allocator;
  poly = MEM_CALLOC(mem_allocator, 1, sizeof(struct polygon));
  if(!poly) {
    res = RES_MEM_ERR;
    goto error;
  }
  poly->allocator = mem_allocator;
  ref_init(&poly->ref);
  darray_vertex_node_init(poly->allocator, &poly->pool);
  htable_u32_init(poly->allocator, &poly->vertices_concave);
  darray_u32_init(poly->allocator, &poly->triangle_ids);
  poly->vertices = UINT32_MAX; /* <=> No vertex */
  poly->nvertices = 0;

exit:
  if(out_polygon)
    *out_polygon = poly;
  return res;

error:
  if(poly) {
    POLYGON(ref_put(poly));
    poly = NULL;
  }
  goto exit;
}

res_T
polygon_ref_get(struct polygon* polygon)
{
  if(!polygon) return RES_BAD_ARG;
  ref_get(&polygon->ref);
  return RES_OK;
}

res_T
polygon_ref_put(struct polygon* polygon)
{
  if(!polygon) return RES_BAD_ARG;
  ref_put(&polygon->ref, release_polygon);
  return RES_OK;
}

res_T
polygon_clear(struct polygon* poly)
{
  if(!poly) return RES_BAD_ARG;
  darray_vertex_node_clear(&poly->pool);
  htable_u32_clear(&poly->vertices_concave);
  poly->vertices = UINT32_MAX;
  poly->nvertices = 0;
  return RES_OK;
}

res_T
polygon_vertex_add(struct polygon* poly, const float pos[3])
{
  struct vertex_node* nodes;
  struct vertex_node* node_free;
  uint32_t inode_free;
  res_T res = RES_OK;

  if(!poly || !pos) {
    res = RES_BAD_ARG;
    goto error;
  }

  nodes = darray_vertex_node_data_get(&poly->pool);

  /* Skip the vertex if it is roughly equal to the previous one */
  if(poly->nvertices && f3_eq_eps(nodes[poly->nvertices-1].pos, pos, 1.e-6f))
    goto exit;

  if(poly->nvertices >= 2) {
    /* Compute the area of the triangle built from the 2 last vertices and the
     * new one. If its area is ~0 then the vertices are aligned and
     * consequently the intermediary vertex can be removed */
    struct vertex_node* v0 = nodes + nodes[poly->vertices].prev; /* <=> tail */
    struct vertex_node* v1 = nodes + v0->prev;
    float e0[3], e1[3], normal[3], area;
    f3_sub(e0, v0->pos, pos);
    f3_sub(e1, v1->pos, pos);
    area = f3_len(f3_cross(normal, e0, e1));
    if(eq_eps(area, 0.f, 1.e-6f * 0.5f)) {
      f3_set(v0->pos, pos);
      goto exit;
    }
  }

  /* Alloc a new vertex node */
  inode_free = (uint32_t)darray_vertex_node_size_get(&poly->pool);
  res = darray_vertex_node_resize(&poly->pool, inode_free + 1);
  if(res != RES_OK)
    goto error;
  nodes = darray_vertex_node_data_get(&poly->pool);
  node_free = nodes + inode_free;

  /* Init the new node */
  node_free->next = node_free->prev = inode_free;
  f3_set(node_free->pos, pos);

  /* Enqueu the new node into the linked list of the polygon vertices */
  if(poly->vertices == UINT32_MAX) {
    poly->vertices = inode_free;
  } else {
    node_free->prev = nodes[poly->vertices].prev;
    node_free->next = poly->vertices;
    nodes[nodes[poly->vertices].prev].next = inode_free;
    nodes[poly->vertices].prev = inode_free;
  }
  ++poly->nvertices;

exit:
  return res;
error:
  goto exit;
}

res_T
polygon_vertices_count_get(const struct polygon* poly, uint32_t* nvertices)
{
  if(!poly || !nvertices) return RES_BAD_ARG;
  *nvertices = poly->nvertices;
  return RES_OK;
}

res_T
polygon_vertex_get
  (const struct polygon* poly, const uint32_t ivertex, float pos[3])
{
  const struct vertex_node* node;
  if(!poly || !pos || ivertex >= poly->nvertices)
    return RES_BAD_ARG;
  node = darray_vertex_node_cdata_get(&poly->pool) + ivertex;
  f3_set(pos, node->pos);
  return RES_OK;
}

res_T
polygon_triangulate
  (struct polygon* poly,
   const uint32_t** indices,
   uint32_t* nindices)
{
  struct vertex_node* nodes;
  uint32_t ivert, inode;
  float normal_convex[3], normal[3];
  res_T res = RES_OK;

  if(!poly || !indices || !nindices) {
    res = RES_BAD_ARG;
    goto error;
  }

  cleanup_polygon(poly);

  htable_u32_clear(&poly->vertices_concave);
  darray_u32_clear(&poly->triangle_ids);

  if(poly->nvertices < 3) /* Point or line */
    goto exit;

  if(poly->nvertices == 3) { /* Already a triangle */
    FOR_EACH(ivert, 0, 3) {
      res = darray_u32_push_back(&poly->triangle_ids, &ivert);
      if(res != RES_OK)
        goto error;
    }
    goto exit;
  }

  convex_normal_compute(poly, normal_convex);

  nodes = darray_vertex_node_data_get(&poly->pool);
  inode = poly->vertices;

  FOR_EACH(inode, 0, poly->nvertices) { /* Find the list of concave vertices */
    node_normal_compute(poly, inode, normal);
    if(f3_dot(normal_convex, normal) < 0.f) {
      const char dummy = 1;
      res = htable_u32_set(&poly->vertices_concave, &inode, &dummy);
      if(res != RES_OK)
        goto error;
    }
  }

  /* Implementation of the ear cutting algorithm "The Graham Scan Triangulates
   * Simple Polygons */
  inode = nodes[nodes[poly->vertices].next].next;
  while(inode != poly->vertices) {
    if(!node_is_an_ear(poly, &poly->vertices_concave, nodes[inode].prev)) {
      inode = nodes[inode].next;
    } else { /* `Prev' is an ear */
      const uint32_t iv0 = nodes[nodes[inode].prev].prev;
      const uint32_t iv1 = nodes[inode].prev;
      const uint32_t iv2 = inode;
      uint32_t inode_prev;

      /* Register the {prev.prev, prev, cur} triangle */
      if(RES_OK != (res = darray_u32_push_back(&poly->triangle_ids, &iv0))
      || RES_OK != (res = darray_u32_push_back(&poly->triangle_ids, &iv1))
      || RES_OK != (res = darray_u32_push_back(&poly->triangle_ids, &iv2)))
        goto error;

      /* Cut the ear by removing `prev' from the node list */
      inode_prev = nodes[inode].prev;
      nodes[nodes[inode_prev].prev].next = nodes[inode_prev].next;
      nodes[nodes[inode_prev].next].prev = nodes[inode_prev].prev;
      if(poly->vertices == inode_prev)
        poly->vertices = inode;

      node_normal_compute(poly, inode, normal);
      if(f3_dot(normal_convex, normal) > 0.f) /* inode is convex */
        htable_u32_erase(&poly->vertices_concave, &inode);

      node_normal_compute(poly, nodes[inode].prev, normal);
      if(f3_dot(normal_convex, normal) > 0.f) /* prev(inode) is convex */
        htable_u32_erase(&poly->vertices_concave, &nodes[inode].prev);

      if(nodes[inode].prev == poly->vertices)
        inode = nodes[inode].next;
    }
  }

exit:
  if(poly) {
    if(indices && nindices && poly) {
      *nindices = (uint32_t)darray_u32_size_get(&poly->triangle_ids);
      *indices = *nindices ? darray_u32_cdata_get(&poly->triangle_ids) : NULL;
    }
    if(poly->nvertices) { /* Restore the linked list */
      poly->vertices = 0;
      nodes = darray_vertex_node_data_get(&poly->pool);
      FOR_EACH(inode, 0, poly->nvertices) {
        nodes[inode].prev = inode == 0 ? poly->nvertices - 1 : inode - 1;
        nodes[inode].next = inode == poly->nvertices - 1 ? 0 : inode + 1;
      }
    }
  }
  return res;
error:
  if(poly)
    darray_u32_clear(&poly->triangle_ids);
  goto exit;
}

