/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#include "senc2d_enclosure_c.h"
#include "senc2d_scene_c.h"
#include "senc2d.h"

#include <rsys/rsys.h>
#include <rsys/double2.h>
#include<rsys/mem_allocator.h>

/******************************************************************************
 * Exported functions
 *****************************************************************************/
res_T
senc2d_scene_get_max_medium
  (const struct senc2d_scene* scn, medium_id_t* max_medium_id)
{
  if(!scn || !max_medium_id) return RES_BAD_ARG;
  if(darray_seg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  *max_medium_id = scn->next_medium_idx - 1;
  return RES_OK;
}

res_T
senc2d_scene_get_enclosure_count
  (const struct senc2d_scene* scn, enclosure_id_t* count)
{
  if(!scn || !count) return RES_BAD_ARG;
  if(darray_seg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  ASSERT(scn->analyze.enclosures_count ==
    darray_enclosure_size_get(&scn->analyze.enclosures));
  *count = scn->analyze.enclosures_count;
  return RES_OK;
}

res_T
senc2d_scene_get_enclosure_count_by_medium
  (const struct senc2d_scene* scn,
   const medium_id_t imed,
   enclosure_id_t* count)
{
  size_t tmp;
  size_t m_idx;
  const struct darray_enc_id* enc_ids;
  if(!scn || !count
    || (imed != SENC2D_UNSPECIFIED_MEDIUM && imed >= scn->next_medium_idx))
    return RES_BAD_ARG;
  if(darray_seg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == 1 + scn->next_medium_idx);
  m_idx = medium_id_2_medium_idx(imed);
  enc_ids = darray_enc_ids_array_cdata_get(&scn->analyze.enc_ids_array_by_medium)
    + m_idx;
  tmp = darray_enc_id_size_get(enc_ids);
  ASSERT(tmp <= ENCLOSURE_MAX__); /* API type */
  *count = (enclosure_id_t)tmp;
  return RES_OK;
}

FINLINE res_T
senc2d_scene_get_enclosure
  (struct senc2d_scene* scn,
   const enclosure_id_t idx,
   struct senc2d_enclosure** out_enc)
{
  struct senc2d_enclosure* enc;
  if(!scn || !out_enc) return RES_BAD_ARG;
  if(darray_seg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  if(idx >= darray_enclosure_size_get(&scn->analyze.enclosures))
    return RES_BAD_ARG;
  enc = enclosure_create(scn, idx);
  if(!enc) return RES_MEM_ERR;
  *out_enc = enc;
  return RES_OK;
}

res_T
senc2d_scene_get_enclosure_by_medium
  (struct senc2d_scene* scn,
   const medium_id_t imed,
   const enclosure_id_t idx,
   struct senc2d_enclosure** out_enc)
{
  size_t m_idx;
  const struct darray_enc_id* enc_ids;
  enclosure_id_t index;
  if(!scn || !out_enc
    || (imed != SENC2D_UNSPECIFIED_MEDIUM && imed >= scn->next_medium_idx))
    return RES_BAD_ARG;
  if(darray_seg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  ASSERT(darray_enc_ids_array_size_get(&scn->analyze.enc_ids_array_by_medium)
    == 1 + scn->next_medium_idx);
  m_idx = medium_id_2_medium_idx(imed);
  enc_ids =
    darray_enc_ids_array_cdata_get(&scn->analyze.enc_ids_array_by_medium) + m_idx;
  if(idx >= darray_enc_id_size_get(enc_ids)) return RES_BAD_ARG;
  index = darray_enc_id_cdata_get(enc_ids)[idx];
  return senc2d_scene_get_enclosure(scn, index, out_enc);
}

res_T
senc2d_scene_get_segment_enclosures
  (const struct senc2d_scene* scn,
   const seg_id_t iseg,
   enclosure_id_t enclosures[2])
{
  const struct segment_enc* seg;
  int i;
  if(!enclosures || !scn) return RES_BAD_ARG;
  if(darray_seg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  if(iseg >= darray_segment_enc_size_get(&scn->analyze.segments_enc))
    return RES_BAD_ARG;
  seg = darray_segment_enc_cdata_get(&scn->analyze.segments_enc) + iseg;
  FOR_EACH(i, 0, 2) enclosures[i] = seg->enclosure[i];
  return RES_OK;
}

res_T
senc2d_scene_get_frontier_vertice_count
  (const struct senc2d_scene* scn,
   unsigned* count)
{
  size_t tmp;
  if(!scn || !count)
    return RES_BAD_ARG;
  if(darray_seg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  tmp = darray_frontier_vertex_size_get(&scn->analyze.frontiers);
  ASSERT(tmp <= VRTX_MAX__);
  *count = (unsigned)tmp; /* Back to API type */
  return RES_OK;
}

res_T
senc2d_scene_get_frontier_vertex
  (const struct senc2d_scene* scn,
   const unsigned iver,
   unsigned vrtx_id[SENC2D_GEOMETRY_DIMENSION-1],
   unsigned* seg_id)
{
  const struct frontier_vertex* vrtx;
  if(!vrtx_id || !scn || !seg_id
    || iver >= darray_frontier_vertex_size_get(&scn->analyze.frontiers))
    return RES_BAD_ARG;
  if(darray_seg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_OP;
  vrtx = darray_frontier_vertex_cdata_get(&scn->analyze.frontiers) + iver;
  *vrtx_id = vrtx->vrtx;
  *seg_id = vrtx->seg;
  return RES_OK;
}

res_T
senc2d_scene_get_overlapping_segments_count
  (const struct senc2d_scene* scn,
   unsigned* count)
{
  size_t tmp;
  if(!scn || !count)
    return RES_BAD_ARG;
  tmp = darray_seg_id_size_get(&scn->analyze.overlapping_ids);
  ASSERT(tmp <= VRTX_MAX__);
  *count = (seg_id_t)tmp; /* Back to API type */
  return RES_OK;
}

res_T
senc2d_scene_get_overlapping_segment
  (const struct senc2d_scene* scn,
   const unsigned idx,
   unsigned* trg_id)
{
  if(!scn || !trg_id
    || idx >= darray_seg_id_size_get(&scn->analyze.overlapping_ids))
    return RES_BAD_ARG;
  *trg_id = darray_seg_id_cdata_get(&scn->analyze.overlapping_ids)[idx];
  return RES_OK;
}
