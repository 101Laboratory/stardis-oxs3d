/* Copyright (C) 2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "suniq.h"

#include <rsys/double3.h>
#include <rsys/dynamic_array_double.h>
#include <rsys/dynamic_array_size_t.h>
#include <rsys/hash_table.h>
#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>

struct pos { double xyz[3]; };
static const struct pos POS_NULL = {0};

static INLINE size_t
pos_hash(const struct pos* pos)
{
  ASSERT(pos);
  return (size_t)hash_fnv64(pos->xyz, sizeof(pos->xyz));
}

static INLINE char
pos_eq(const struct pos* a, const struct pos* b)
{
  ASSERT(a && b);
  return (char)d3_eq(a->xyz, b->xyz);
}

struct key {
  double sorted_vertices[3][3];
};
static const struct key KEY_NULL = {0};

static INLINE size_t
key_hash(const struct key* key)
{
  double d[9];
  ASSERT(key);
  d3_set(d+0, key->sorted_vertices[0]);
  d3_set(d+3, key->sorted_vertices[1]);
  d3_set(d+6, key->sorted_vertices[2]);
  return (size_t)hash_fnv64(d, sizeof(d));
}

static INLINE char
key_eq(const struct key* a, const struct key* b)
{
  ASSERT(a && b);
  return (char)
      (  d3_eq(a->sorted_vertices[0], b->sorted_vertices[0])
      && d3_eq(a->sorted_vertices[1], b->sorted_vertices[1])
      && d3_eq(a->sorted_vertices[2], b->sorted_vertices[2]));
}

/* Generate the hash table that maps a triangle key to an index */
#define HTABLE_NAME key2itri
#define HTABLE_KEY struct key
#define HTABLE_KEY_FUNCTOR_HASH key_hash
#define HTABLE_KEY_FUNCTOR_EQ key_eq
#define HTABLE_DATA size_t
#include <rsys/hash_table.h>

/* Generate the hash table that maps a position to an index */
#define HTABLE_NAME pos2ivtx
#define HTABLE_KEY struct pos
#define HTABLE_KEY_FUNCTOR_HASH pos_hash
#define HTABLE_KEY_FUNCTOR_EQ pos_eq
#define HTABLE_DATA size_t
#include <rsys/hash_table.h>

struct suniq {
  struct mem_allocator* allocator;

  struct htable_key2itri key2itri;
  struct htable_pos2ivtx pos2ivtx;

  struct darray_size_t indices; /* List of size_t[3] */
  struct darray_double positions; /* List of double[3] */
  ref_T ref;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
/* Return an integer greater than, equal to, or less than 0, if a is greater
 * than, equal to, or less than b, respectively */
static INLINE int
cmp_d3(const double a[3], const double b[3])
{
  ASSERT(a && b);
  return a[0] < b[0] ? -1 : a[0] > b[0] ? +1
       : a[1] < b[1] ? -1 : a[1] > b[1] ? +1
       : a[2] < b[2] ? -1 : a[2] > b[2] ? +1
       : 0;
}

static void
key_setup(struct key* key, const struct suniq_triangle* tri)
{
  double (*v)[3] = NULL;

  ASSERT(key && tri);

  d3_set(key->sorted_vertices[0], tri->vertices[0]);
  d3_set(key->sorted_vertices[1], tri->vertices[1]);
  d3_set(key->sorted_vertices[2], tri->vertices[2]);

  #define SWAP3(A, B) { \
    SWAP(double, A[0], B[0]); \
    SWAP(double, A[1], B[1]); \
    SWAP(double, A[2], B[2]); \
  } (void)0

  /* Bubble sort (ascending order) */
  v = key->sorted_vertices;
  if(cmp_d3(v[0], v[1]) > 0) SWAP3(v[0], v[1]);
  if(cmp_d3(v[1], v[2]) > 0) SWAP3(v[1], v[2]);
  if(cmp_d3(v[0], v[1]) > 0) SWAP3(v[0], v[1]);

  #undef SWAP3
}

static res_T
save_vertex(struct suniq* suniq, const struct pos* vertex, size_t* out_ivtx)
{
  size_t ivtx = SIZE_MAX; /* Vertex index */
  int i = 0;
  res_T res = RES_OK;

  ASSERT(suniq && vertex && out_ivtx);

  /* Define the vertex index */
  ivtx = darray_double_size_get(&suniq->positions) / 3/*#coords per vertex*/;

  /* Save vertex coordinates */
  FOR_EACH(i, 0, 3) {
    res = darray_double_push_back(&suniq->positions, &vertex->xyz[i]);
    if(res != RES_OK) goto error;
  }

  /* Register the vertex */
  res = htable_pos2ivtx_set(&suniq->pos2ivtx, vertex, &ivtx);
  if(res != RES_OK) goto error;

exit:
  *out_ivtx = ivtx;
  return res;
error:
  ivtx = SIZE_MAX;
  goto exit;
}

static res_T
register_vertex(struct suniq* suniq, const double vertex[3], size_t* out_id)
{
  struct pos pos = POS_NULL;
  size_t* vertex_exist = NULL;
  size_t id = SIZE_MAX;
  res_T res = RES_OK;

  ASSERT(suniq && vertex && out_id);

  d3_set(pos.xyz, vertex);

  vertex_exist = htable_pos2ivtx_find(&suniq->pos2ivtx, &pos);
  if(vertex_exist) {
    id = *vertex_exist;
  } else {
    res = save_vertex(suniq, &pos, &id);
    if(res != RES_OK) goto error;
  }

exit:
  *out_id = id;
  return res;
error:
  id = SIZE_MAX;
  goto exit;
}

static res_T
save_triangle
  (struct suniq* suniq,
   const struct suniq_triangle* triangle,
   const struct key* key,
   size_t* out_itri)
{
  size_t itri = SIZE_MAX; /* Trinngle index */
  int i = 0;
  res_T res = RES_OK;

  ASSERT(suniq && triangle && key && out_itri);

  /* Define the triangle index */
  itri = darray_size_t_size_get(&suniq->indices) / 3/*#verices par triangle*/;

  /* Save triangle indices */
  FOR_EACH(i, 0, 3) {
    size_t ivtx = 0;

    res = register_vertex(suniq, triangle->vertices[i], &ivtx);
    if(res != RES_OK) goto error;
    res = darray_size_t_push_back(&suniq->indices, &ivtx);
    if(res != RES_OK) goto error;
  }

  /* Register the triangle */
  res = htable_key2itri_set(&suniq->key2itri, key, &itri);
  if(res != RES_OK) goto error;

exit:
  *out_itri = itri;
  return res;
error:
  itri = SIZE_MAX;
  goto exit;
}

static void
release_suniq(ref_T* ref)
{
  struct suniq* suniq = CONTAINER_OF(ref, struct suniq, ref);

  ASSERT(ref);

  htable_key2itri_release(&suniq->key2itri);
  htable_pos2ivtx_release(&suniq->pos2ivtx);
  darray_size_t_release(&suniq->indices);
  darray_double_release(&suniq->positions);
  MEM_RM(suniq->allocator, suniq);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
suniq_create(struct mem_allocator* allocator, struct suniq** out_suniq)
{
  struct mem_allocator* mem_allocator = NULL;
  struct suniq* suniq = NULL;
  res_T res = RES_OK;

  if(!out_suniq) { res = RES_BAD_ARG; goto error; }

  mem_allocator = allocator ? allocator : &mem_default_allocator;
  suniq = MEM_CALLOC(mem_allocator, 1, sizeof(*suniq));
  if(!suniq) { res = RES_MEM_ERR; goto error; }

  ref_init(&suniq->ref);
  suniq->allocator = mem_allocator;
  htable_key2itri_init(suniq->allocator, &suniq->key2itri);
  htable_pos2ivtx_init(suniq->allocator, &suniq->pos2ivtx);
  darray_size_t_init(suniq->allocator, &suniq->indices);
  darray_double_init(suniq->allocator, &suniq->positions);

exit:
  if(out_suniq) *out_suniq = suniq;
  return res;
error:
  if(suniq) { SUNIQ(ref_put(suniq)); suniq = NULL; }
  goto exit;
}

res_T
suniq_ref_get(struct suniq* suniq)
{
  return suniq ? ref_get(&suniq->ref), RES_OK : RES_BAD_ARG;
}

res_T
suniq_ref_put(struct suniq* suniq)
{
  return suniq ? ref_put(&suniq->ref, release_suniq), RES_OK : RES_BAD_ARG;
}

res_T
suniq_register_triangle
  (struct suniq* suniq,
   const struct suniq_triangle* tri,
   size_t* out_id) /* Can be NULL */
{
  struct key key = KEY_NULL;
  size_t* triangle_exist = NULL;
  size_t id = SIZE_MAX; /* Index of the triangle */
  res_T res = RES_OK;

  if(!suniq || !tri) { res = RES_BAD_ARG; goto error; }

  key_setup(&key, tri);

  triangle_exist = htable_key2itri_find(&suniq->key2itri, &key);
  if(triangle_exist) {
    id = *triangle_exist;
  } else {
    res = save_triangle(suniq, tri, &key, &id);
    if(res != RES_OK) goto error;
  }

exit:
  if(out_id) *out_id = id;
  return res;
error:
  id = SIZE_MAX;
  goto exit;
}

res_T
suniq_get_triangle
  (const struct suniq* suniq,
   const size_t id,
   struct suniq_triangle* triangle)
{
  struct suniq_desc desc = SUNIQ_DESC_NULL;
  res_T res = RES_OK;

  if((res = suniq_get_desc(suniq, &desc)) != RES_OK) goto error;
  if((res = suniq_desc_get_triangle(&desc, id, triangle)) != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

res_T
suniq_get_desc(const struct suniq* suniq, struct suniq_desc* desc)
{
  if(!suniq || !desc) return RES_BAD_ARG;
  desc->indices = darray_size_t_cdata_get(&suniq->indices);
  desc->positions = darray_double_cdata_get(&suniq->positions);
  desc->ntriangles = darray_size_t_size_get(&suniq->indices)/3/*#ids*/;
  desc->nvertices = darray_double_size_get(&suniq->positions)/3/*#coords*/;
  return RES_OK;
}

res_T
suniq_desc_get_vertex
  (const struct suniq_desc* desc,
   const size_t ivertex,
   double vertex[3])
{
  if(!desc || ivertex >= desc->nvertices || !vertex) return RES_BAD_ARG;

  vertex[0] = desc->positions[ivertex*3/*#coords per vertex*/+0];
  vertex[1] = desc->positions[ivertex*3/*#coords per vertex*/+1];
  vertex[2] = desc->positions[ivertex*3/*#coords per vertex*/+2];

  return RES_OK;
}

res_T
suniq_desc_get_triangle_indices
  (const struct suniq_desc* desc,
   const size_t itriangle,
   size_t indices[3])
{
  if(!desc || itriangle >= desc->ntriangles || !indices) return RES_BAD_ARG;

  indices[0] = desc->indices[itriangle*3/*#indices per triangle*/+0];
  indices[1] = desc->indices[itriangle*3/*#indices per triangle*/+1];
  indices[2] = desc->indices[itriangle*3/*#indices per triangle*/+2];

  return RES_OK;
}

res_T
suniq_desc_get_triangle
  (const struct suniq_desc* desc,
   const size_t itriangle,
   struct suniq_triangle* triangle)
{
  size_t indices[3] = {0,0,0};
  int i = 0;
  res_T res = RES_OK;

  if(!triangle) { res = RES_BAD_ARG; goto error; }

  res = suniq_desc_get_triangle_indices(desc, itriangle, indices);
  if(res != RES_OK) goto error;

  FOR_EACH(i, 0, 3) {
    res = suniq_desc_get_vertex(desc, indices[i], triangle->vertices[i]);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
suniq_clear(struct suniq* suniq)
{
  if(!suniq) return RES_BAD_ARG;

  htable_key2itri_clear(&suniq->key2itri);
  htable_pos2ivtx_clear(&suniq->pos2ivtx);
  darray_size_t_clear(&suniq->indices);
  darray_double_clear(&suniq->positions);

  return RES_OK;
}
