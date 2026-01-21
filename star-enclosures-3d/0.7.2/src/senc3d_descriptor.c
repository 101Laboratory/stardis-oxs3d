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

#include "senc3d_enclosure_c.h"
#include "senc3d_scene_c.h"
#include "senc3d.h"

#include <rsys/rsys.h>
#include <rsys/double3.h>
#include <rsys/mem_allocator.h>

/******************************************************************************
 * Exported functions
 *****************************************************************************/
res_T
senc3d_scene_get_max_medium
  (const struct senc3d_scene* scn, medium_id_t* max_medium_id)
{
  if(!scn || !max_medium_id) return RES_BAD_ARG;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == darray_side_range_size_get(&scn->media_use));
  ASSERT(darray_side_range_size_get(&scn->media_use) >= 1);
  if(darray_side_range_size_get(&scn->media_use) == 1)
    *max_medium_id = SENC3D_UNSPECIFIED_MEDIUM;
  else {
    size_t sz = darray_side_range_size_get(&scn->media_use) - 2;
    ASSERT(sz <= MEDIUM_MAX__);
    *max_medium_id = (medium_id_t)sz;
  }
  return RES_OK;
}

res_T
senc3d_scene_get_enclosure_count
  (const struct senc3d_scene* scn, enclosure_id_t* count)
{
  if(!scn || !count) return RES_BAD_ARG;
  if(darray_trg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  ASSERT(scn->analyze.enclosures_count ==
    darray_enclosure_size_get(&scn->analyze.enclosures));
  *count = scn->analyze.enclosures_count;
  return RES_OK;
}

res_T
senc3d_scene_get_enclosure_count_by_medium
  (const struct senc3d_scene* scn,
   const medium_id_t imed,
   enclosure_id_t* count)
{
  size_t tmp;
  size_t m_idx;
  const struct darray_enc_id* enc_ids;
  if(!scn || !count) return RES_BAD_ARG;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == darray_side_range_size_get(&scn->media_use));
  ASSERT(darray_side_range_size_get(&scn->media_use) >= 1);
  if(imed != SENC3D_UNSPECIFIED_MEDIUM
    && imed >= darray_side_range_size_get(&scn->media_use))
    return RES_BAD_ARG;
  if(darray_trg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  m_idx = medium_id_2_medium_idx(imed);
  enc_ids = darray_enc_ids_array_cdata_get(&scn->analyze.enc_ids_array_by_medium)
    + m_idx;
  tmp = darray_enc_id_size_get(enc_ids);
  ASSERT(tmp <= ENCLOSURE_MAX__); /* API type */
  *count = (enclosure_id_t)tmp;
  return RES_OK;
}

FINLINE res_T
senc3d_scene_get_enclosure
  (struct senc3d_scene* scn,
   const enclosure_id_t idx,
   struct senc3d_enclosure** out_enc)
{
  struct senc3d_enclosure* enc;
  if(!scn || !out_enc) return RES_BAD_ARG;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == darray_side_range_size_get(&scn->media_use));
  ASSERT(darray_side_range_size_get(&scn->media_use) >= 1);
  if(darray_trg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  if(idx >= darray_enclosure_size_get(&scn->analyze.enclosures))
    return RES_BAD_ARG;
  enc = enclosure_create(scn, idx);
  if(!enc) return RES_MEM_ERR;
  *out_enc = enc;
  return RES_OK;
}

res_T
senc3d_scene_get_enclosure_by_medium
  (struct senc3d_scene* scn,
   const medium_id_t imed,
   const enclosure_id_t idx,
   struct senc3d_enclosure** out_enc)
{
  size_t m_idx;
  const struct darray_enc_id* enc_ids;
  enclosure_id_t index;
  if(!scn || !out_enc) return RES_BAD_ARG;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == darray_side_range_size_get(&scn->media_use));
  ASSERT(darray_side_range_size_get(&scn->media_use) >= 1);
  if(imed != SENC3D_UNSPECIFIED_MEDIUM
    && imed >= darray_side_range_size_get(&scn->media_use))
    return RES_BAD_ARG;
  if(darray_trg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  m_idx = medium_id_2_medium_idx(imed);
  enc_ids = darray_enc_ids_array_cdata_get(&scn->analyze.enc_ids_array_by_medium)
    + m_idx;
  if(idx >= darray_enc_id_size_get(enc_ids)) return RES_BAD_ARG;
  index = darray_enc_id_cdata_get(enc_ids)[idx];
  return senc3d_scene_get_enclosure(scn, index, out_enc);
}

res_T
senc3d_scene_get_triangle_enclosures
  (const struct senc3d_scene* scn,
   const trg_id_t itri,
   enclosure_id_t enclosures[2])
{
  const struct triangle_enc* trg;
  int i;
  if(!enclosures || !scn) return RES_BAD_ARG;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == darray_side_range_size_get(&scn->media_use));
  ASSERT(darray_side_range_size_get(&scn->media_use) >= 1);
  if(darray_trg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  if(itri >= darray_triangle_enc_size_get(&scn->analyze.triangles_enc))
    return RES_BAD_ARG;
  trg = darray_triangle_enc_cdata_get(&scn->analyze.triangles_enc) + itri;
  FOR_EACH(i, 0, 2) enclosures[i] = trg->enclosure[i];
  return RES_OK;
}

res_T
senc3d_scene_get_frontier_segments_count
  (const struct senc3d_scene* scn,
   vrtx_id_t* count)
{
  size_t tmp;
  if(!scn || !count)
    return RES_BAD_ARG;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == darray_side_range_size_get(&scn->media_use));
  ASSERT(darray_side_range_size_get(&scn->media_use) >= 1);
  if(darray_trg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  tmp = darray_frontier_edge_size_get(&scn->analyze.frontiers);
  ASSERT(tmp <= VRTX_MAX__);
  *count = (vrtx_id_t)tmp; /* Back to API type */
  return RES_OK;
}

res_T
senc3d_scene_get_frontier_segment
  (const struct senc3d_scene* scn,
   const unsigned iseg, /* There is no defined type for segment IDs */
   vrtx_id_t vrtx_id[2],
   unsigned* trg_id)
{
  const struct frontier_edge* edge;
  if(!vrtx_id || !scn || !trg_id
    || iseg >= darray_frontier_edge_size_get(&scn->analyze.frontiers))
    return RES_BAD_ARG;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == darray_side_range_size_get(&scn->media_use));
  ASSERT(darray_side_range_size_get(&scn->media_use) >= 1);
  if(darray_trg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  edge = darray_frontier_edge_cdata_get(&scn->analyze.frontiers) + iseg;
  ASSERT(edge->vrtx0 <= VRTX_MAX__);
  ASSERT(edge->vrtx1 <= VRTX_MAX__);
  vrtx_id[0] = edge->vrtx0;
  vrtx_id[1] = edge->vrtx1;
  *trg_id = edge->trg;
  return RES_OK;
}

res_T
senc3d_scene_get_overlapping_triangles_count
  (const struct senc3d_scene* scn,
   vrtx_id_t* count)
{
  size_t tmp;
  if(!scn || !count)
    return RES_BAD_ARG;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == darray_side_range_size_get(&scn->media_use));
  ASSERT(darray_side_range_size_get(&scn->media_use) >= 1);
  tmp = darray_trg_id_size_get(&scn->analyze.overlapping_ids);
  ASSERT(tmp <= VRTX_MAX__);
  *count = (trg_id_t)tmp; /* Back to API type */
  return RES_OK;
}

res_T
senc3d_scene_get_overlapping_triangle
  (const struct senc3d_scene* scn,
   const unsigned idx,
   unsigned* trg_id)
{
  if(!scn || !trg_id
    || idx >= darray_trg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_ARG;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == darray_side_range_size_get(&scn->media_use));
  ASSERT(darray_side_range_size_get(&scn->media_use) >= 1);
  *trg_id = darray_trg_id_cdata_get(&scn->analyze.overlapping_ids)[idx];
  return RES_OK;
}
