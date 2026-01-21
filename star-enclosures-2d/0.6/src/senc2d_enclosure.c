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
#include "senc2d_enclosure_data.h"
#include "senc2d_scene_c.h"
#include "senc2d_device_c.h"
#include "senc2d.h"

#include <rsys/rsys.h>
#include <rsys/double2.h>
#include<rsys/mem_allocator.h>


/******************************************************************************
 * Helper function
 *****************************************************************************/
static void
enclosure_release(ref_T * ref)
{
  struct senc2d_enclosure* enclosure = NULL;
  struct senc2d_scene* scn = NULL;
  ASSERT(ref);
  enclosure = CONTAINER_OF(ref, struct senc2d_enclosure, ref);
  scn = enclosure->scene;
  MEM_RM(scn->dev->allocator, enclosure);
  SENC2D(scene_ref_put(scn));
}

/******************************************************************************
 * Local functions
 *****************************************************************************/
struct senc2d_enclosure*
enclosure_create
  (struct senc2d_scene* scn,
   const enclosure_id_t idx)
{
  struct senc2d_enclosure* enc;
  ASSERT(scn && idx < darray_enclosure_size_get(&scn->analyze.enclosures));
  enc = MEM_CALLOC(scn->dev->allocator, 1, sizeof(struct senc2d_enclosure));
  if(enc) {
    const struct enclosure_data* data
      = darray_enclosure_data_get(&scn->analyze.enclosures) + idx;
    enc->scene = scn;
    enc->data = data;
    ref_init(&enc->ref);
    SENC2D(scene_ref_get(scn));
  }
  return enc;
}

/******************************************************************************
 * Exported functions
 *****************************************************************************/
res_T
senc2d_enclosure_get_header
  (const struct senc2d_enclosure* enclosure,
   struct senc2d_enclosure_header* header)
{
  if(!enclosure || !header) return RES_BAD_ARG;
  *header = enclosure->data->header;
  return RES_OK;
}

res_T
senc2d_enclosure_get_segment
  (const struct senc2d_enclosure* enclosure,
   const seg_id_t iseg,
   vrtx_id_t indices[2])
{
  const struct side_enc* side;
  int i;
  if(!enclosure || !indices
    || iseg >= enclosure->data->header.primitives_count)
    return RES_BAD_ARG;
  ASSERT(darray_sides_enc_size_get(&enclosure->data->sides)
    == enclosure->data->header.primitives_count);
  side = darray_sides_enc_cdata_get(&enclosure->data->sides) + iseg;
  FOR_EACH(i, 0, 2) indices[i] = side->vertice_id[i];
  return RES_OK;
}

res_T
senc2d_enclosure_get_vertex
  (const struct senc2d_enclosure* enclosure,
   const vrtx_id_t ivert,
   double coord[2])
{
  if(!enclosure || !coord
    || ivert >= enclosure->data->header.vertices_count) {
    return RES_BAD_ARG;
  } else {
    const vrtx_id_t idx
      = darray_vrtx_id_cdata_get(&enclosure->data->vertices)[ivert];
    const union double2* positions
      = darray_position_cdata_get(&enclosure->scene->vertices);
    ASSERT(darray_vrtx_id_size_get(&enclosure->data->vertices)
      == enclosure->data->header.vertices_count);
    d2_set(coord, positions[idx].vec);
    return RES_OK;
  }
}

res_T
senc2d_enclosure_get_segment_id
  (const struct senc2d_enclosure* enclosure,
   const seg_id_t iseg,
   seg_id_t* gid,
   enum senc2d_side* sde)
{
  const struct side_enc* side;
  if(!enclosure || !gid || !sde
    || iseg >= enclosure->data->header.primitives_count)
    return RES_BAD_ARG;
  ASSERT(darray_sides_enc_size_get(&enclosure->data->sides)
    == enclosure->data->header.primitives_count);
  side = darray_sides_enc_cdata_get(&enclosure->data->sides) + iseg;
  *gid = SEGSIDE_2_SEG(side->side_id);
  *sde = SEGSIDE_2_SIDE(side->side_id);
  return RES_OK;
}

res_T
senc2d_enclosure_get_medium
  (const struct senc2d_enclosure* enclosure,
   const medium_id_t imed,
   medium_id_t* medium)
{
  if(!enclosure || !medium
    || imed >= enclosure->data->header.enclosed_media_count)
    return RES_BAD_ARG;
  ASSERT(enclosure->data->header.enclosed_media_count
    == darray_media_size_get(&enclosure->data->enclosed_media));
  *medium = darray_media_cdata_get(&enclosure->data->enclosed_media)[imed];
  return RES_OK;
}

res_T
senc2d_enclosure_ref_get(struct senc2d_enclosure* enc)
{
  if(!enc) return RES_BAD_ARG;
  ref_get(&enc->ref);
  return RES_OK;
}

res_T
senc2d_enclosure_ref_put(struct senc2d_enclosure* enc)
{
  if(!enc) return RES_BAD_ARG;
  ref_put(&enc->ref, enclosure_release);
  return RES_OK;
}
