/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#include "sdis_tile.h"

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE void
release_tile(ref_T* ref)
{
  struct tile* tile = CONTAINER_OF(ref, struct tile, ref);
  ASSERT(ref);
  MEM_RM(tile->allocator, tile);
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
res_T
tile_create(struct mem_allocator* allocator, struct tile** out_tile)
{
  struct tile* tile = NULL;
  res_T res = RES_OK;
  ASSERT(allocator && out_tile);

  tile = MEM_ALLOC_ALIGNED(allocator, sizeof(*tile), 16);
  if(!tile) { res = RES_MEM_ERR; goto error; }

  ref_init(&tile->ref);
  list_init(&tile->node);
  tile->allocator = allocator;
  memset(&tile->data, 0, sizeof(tile->data));
exit:
  *out_tile = tile;
  return res;
error:
  if(tile) { tile_ref_put(tile); tile = NULL; }
  goto exit;
}

void
tile_ref_get(struct tile* tile)
{
  ASSERT(tile);
  ref_get(&tile->ref);
}

void
tile_ref_put(struct tile* tile)
{
  ASSERT(tile);
  ref_put(&tile->ref, release_tile);
}

