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
#include "senc3d_enclosure_data.h"
#include "senc3d_scene_c.h"
#include "senc3d_device_c.h"
#include "senc3d.h"

#include <rsys/rsys.h>
#include <rsys/double3.h>
#include <rsys/mem_allocator.h>


/******************************************************************************
 * Helper function
 *****************************************************************************/
static void
enclosure_release(ref_T * ref)
{
  struct senc3d_enclosure* enclosure = NULL;
  struct senc3d_scene* scn = NULL;
  ASSERT(ref);
  enclosure = CONTAINER_OF(ref, struct senc3d_enclosure, ref);
  scn = enclosure->scene;
  MEM_RM(scn->dev->allocator, enclosure);
  SENC3D(scene_ref_put(scn));
}

/******************************************************************************
 * Local functions
 *****************************************************************************/
struct senc3d_enclosure*
enclosure_create
  (struct senc3d_scene* scn,
   const enclosure_id_t idx)
{
  struct senc3d_enclosure* enc;
  ASSERT(scn && idx < darray_enclosure_size_get(&scn->analyze.enclosures));
  enc = MEM_CALLOC(scn->dev->allocator, 1, sizeof(struct senc3d_enclosure));
  if(enc) {
    const struct enclosure_data* data
      = darray_enclosure_data_get(&scn->analyze.enclosures) + idx;
    enc->scene = scn;
    enc->data = data;
    ref_init(&enc->ref);
    SENC3D(scene_ref_get(scn));
  }
  return enc;
}

/******************************************************************************
 * Exported functions
 *****************************************************************************/
res_T
senc3d_enclosure_get_header
  (const struct senc3d_enclosure* enclosure,
   struct senc3d_enclosure_header* header)
{
  if(!enclosure || !header) return RES_BAD_ARG;
  *header = enclosure->data->header;
  return RES_OK;
}

res_T
senc3d_enclosure_get_triangle
  (const struct senc3d_enclosure* enclosure,
   const trg_id_t itri,
   vrtx_id_t indices[3])
{
  const struct side_enc* side;
  int i;
  if(!enclosure || !indices
    || itri >= enclosure->data->header.primitives_count)
    return RES_BAD_ARG;
  ASSERT(darray_sides_enc_size_get(&enclosure->data->sides)
    == enclosure->data->header.primitives_count);
  side = darray_sides_enc_cdata_get(&enclosure->data->sides) + itri;
  FOR_EACH(i, 0, 3) indices[i] = side->vertice_id[i];
  return RES_OK;
}

res_T
senc3d_enclosure_get_vertex
  (const struct senc3d_enclosure* enclosure,
   const vrtx_id_t ivert,
   double coord[3])
{
  if(!enclosure || !coord
    || ivert >= enclosure->data->header.vertices_count) {
    return RES_BAD_ARG;
  } else {
    const vrtx_id_t idx
      = darray_vrtx_id_cdata_get(&enclosure->data->vertices)[ivert];
    const union double3* positions
      = darray_position_cdata_get(&enclosure->scene->vertices);
    ASSERT(darray_vrtx_id_size_get(&enclosure->data->vertices)
      == enclosure->data->header.vertices_count);
    d3_set(coord, positions[idx].vec);
    return RES_OK;
  }
}

res_T
senc3d_enclosure_get_triangle_id
  (const struct senc3d_enclosure* enclosure,
   const trg_id_t itri,
   trg_id_t* gid,
   enum senc3d_side* sde)
{
  const struct side_enc* side;
  if(!enclosure || !gid || !sde
    || itri >= enclosure->data->header.primitives_count)
    return RES_BAD_ARG;
  ASSERT(darray_sides_enc_size_get(&enclosure->data->sides)
    == enclosure->data->header.primitives_count);
  side = darray_sides_enc_cdata_get(&enclosure->data->sides) + itri;
  *gid = TRGSIDE_2_TRG(side->side_id);
  *sde = TRGSIDE_2_SIDE(side->side_id);
  return RES_OK;
}

res_T
senc3d_enclosure_get_medium
  (const struct senc3d_enclosure* enclosure,
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
senc3d_enclosure_ref_get(struct senc3d_enclosure* enc)
{
  if(!enc) return RES_BAD_ARG;
  ref_get(&enc->ref);
  return RES_OK;
}

res_T
senc3d_enclosure_ref_put(struct senc3d_enclosure* enc)
{
  if(!enc) return RES_BAD_ARG;
  ref_put(&enc->ref, enclosure_release);
  return RES_OK;
}
