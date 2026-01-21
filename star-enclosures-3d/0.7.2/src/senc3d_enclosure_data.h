/* Copyright (C) 2018-2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SENC3D_ENCLOSURE_DATA_H
#define SENC3D_ENCLOSURE_DATA_H

#include "senc3d.h"
#include "senc3d_internal_types.h"
#include "senc3d_side_range.h"

#include <rsys/rsys.h>
#include <rsys/ref_count.h>
#include <rsys/hash_table.h>
#include <rsys/dynamic_array.h>
#include <rsys/hash_table.h>

#include <limits.h>

#define DARRAY_NAME vrtx_id
#define DARRAY_DATA vrtx_id_t
#include <rsys/dynamic_array.h>

#define HTABLE_NAME vrtx_id
#define HTABLE_KEY vrtx_id_t
#define HTABLE_DATA vrtx_id_t
#include <rsys/hash_table.h>

struct side_enc {
  vrtx_id_t vertice_id[3];
  side_id_t side_id;
};

#define DARRAY_NAME sides_enc
#define DARRAY_DATA struct side_enc
#include <rsys/dynamic_array.h>

/* uchar array with init to zero */
static FINLINE void
zero_init_uchar
  (struct mem_allocator* alloc, uchar* data)
{
  ASSERT(data); (void) alloc;
  *data = 0;
}
#define DARRAY_FUNCTOR_INIT zero_init_uchar
#include <rsys/dynamic_array_uchar.h>

static void
init_header(struct senc3d_enclosure_header* header)
{
  ASSERT(header);
  header->enclosure_id = ENCLOSURE_NULL__;
  header->primitives_count = 0;
  header->unique_primitives_count = 0;
  header->vertices_count = 0;
  header->enclosed_media_count = 0;
  header->is_infinite = INT_MAX;
  header->volume = 0;
  header->area = 0;
}

#define DARRAY_NAME media
#define DARRAY_DATA medium_id_t
#include <rsys/dynamic_array.h>

static FINLINE res_T
bool_array_of_media_merge
  (struct darray_uchar* dst,
   const uchar* src,
   const unsigned count,
   const size_t sz)
{
  res_T res = RES_OK;
  size_t i;
  uchar* data_dst;
  unsigned c = 0;

  ASSERT(src && dst);

  OK(darray_uchar_resize(dst, sz));
  data_dst = darray_uchar_data_get(dst);
  FOR_EACH(i, 0, sz) {
    if(!src[i]) continue;
    data_dst[i] = 1;
    if(++c == count) break;
  }
end:
  return res;
error:
  goto end;
}

static FINLINE res_T
bool_array_of_media_to_darray_media
  (struct darray_media* dst,
   const struct darray_uchar* src,
   const size_t sz)
{
  res_T res = RES_OK;
  int64_t m_idx;
  const uchar* data;

  ASSERT(src && dst && sz < INT64_MAX);

  data = darray_uchar_cdata_get(src);
  ASSERT(sz == darray_uchar_size_get(src));
  darray_media_clear(dst);
  if(res != RES_OK) goto error;
  FOR_EACH(m_idx, 0, (int64_t)sz) {
    medium_id_t medium = medium_idx_2_medium_id(m_idx);
    if(!data[m_idx]) continue;
    res = darray_media_push_back(dst, &medium);
    if(res != RES_OK) goto error;
  }
end:
  return res;
error:
  goto end;
}

struct enclosure_data {
  struct senc3d_enclosure_header header;
  /* Same triangle can appear twice if both sides */
  struct darray_sides_enc sides;
  /* Index of vertices in scene's unique vertices */
  struct darray_vrtx_id vertices;
  /* List of the enclosed media */
  struct darray_uchar tmp_enclosed_media;
  struct darray_media enclosed_media;
  /* Number of components involved in this enclosure */
  component_id_t cc_count;
  /* Linked list of the components */
  component_id_t first_component;
  /* Range of triangles member of the enclosure */
  struct side_range side_range;
  /* Counts */
  side_id_t side_count;
};

static FINLINE void
enclosure_data_init(struct mem_allocator* alloc, struct enclosure_data* enc) {
  ASSERT(enc);
  init_header(&enc->header);
  enc->cc_count = 0;
  enc->first_component = COMPONENT_NULL__;
  enc->side_range.first = SIDE_NULL__;
  enc->side_range.last = 0;
  enc->side_count = 0;
  darray_sides_enc_init(alloc, &enc->sides);
  darray_vrtx_id_init(alloc, &enc->vertices);
  darray_uchar_init(alloc, &enc->tmp_enclosed_media);
  darray_media_init(alloc, &enc->enclosed_media);
}

static FINLINE res_T
enclosure_data_copy
  (struct enclosure_data* dst,
   const struct enclosure_data* src)
{
  res_T res = RES_OK;
  ASSERT(src && dst);
  dst->header = src->header;
  dst->cc_count = src->cc_count;
  dst->first_component = src->first_component;
  dst->side_range = src->side_range;
  dst->side_count = src->side_count;
  OK(darray_sides_enc_copy(&dst->sides, &src->sides));
  OK(darray_vrtx_id_copy(&dst->vertices, &src->vertices));
  OK(darray_uchar_copy(&dst->tmp_enclosed_media, &src->tmp_enclosed_media));
  OK(darray_media_copy(&dst->enclosed_media, &src->enclosed_media));
error:
  return res;
}

static FINLINE void
enclosure_data_release(struct enclosure_data* n) {
  ASSERT(n);
  darray_sides_enc_release(&n->sides);
  darray_vrtx_id_release(&n->vertices);
  darray_uchar_release(&n->tmp_enclosed_media);
  darray_media_release(&n->enclosed_media);
}

static FINLINE res_T
enclosure_data_copy_and_release
  (struct enclosure_data* dst,
   struct enclosure_data* src)
{
  res_T res = RES_OK;
  ASSERT(src && dst);
  dst->header = src->header;
  dst->cc_count = src->cc_count;
  dst->first_component = src->first_component;
  dst->side_range = src->side_range;
  dst->side_count = src->side_count;
  OK(darray_sides_enc_copy_and_release(&dst->sides, &src->sides));
  OK(darray_vrtx_id_copy_and_release(&dst->vertices, &src->vertices));
  OK(darray_uchar_copy_and_release(&dst->tmp_enclosed_media,
    &src->tmp_enclosed_media));
  OK(darray_media_copy_and_release(&dst->enclosed_media, &src->enclosed_media));
error:
  return res;
}

#define DARRAY_NAME enclosure
#define DARRAY_DATA struct enclosure_data
#define DARRAY_FUNCTOR_INIT enclosure_data_init
#define DARRAY_FUNCTOR_COPY enclosure_data_copy
#define DARRAY_FUNCTOR_RELEASE enclosure_data_release
#define DARRAY_FUNCTOR_COPY_AND_RELEASE enclosure_data_copy_and_release
#include <rsys/dynamic_array.h>

#define DARRAY_NAME enc_id
#define DARRAY_DATA enclosure_id_t
#include <rsys/dynamic_array.h>

#define DARRAY_NAME enc_ids_array
#define DARRAY_DATA struct darray_enc_id
#define DARRAY_FUNCTOR_INIT darray_enc_id_init
#define DARRAY_FUNCTOR_COPY darray_enc_id_copy
#define DARRAY_FUNCTOR_RELEASE darray_enc_id_release
#define DARRAY_FUNCTOR_COPY_AND_RELEASE darray_enc_id_copy_and_release
#include <rsys/dynamic_array.h>

#endif /* SENC3D_ENCLOSURE_DATA_H */
