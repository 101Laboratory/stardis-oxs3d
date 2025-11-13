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

#ifndef SDIS_TILE_H
#define SDIS_TILE_H

#include "sdis_misc.h"

#include <rsys/list.h>
#include <rsys/ref_count.h>

/* Forward declarations */
struct mem_allocator;

#define TILE_SIZE 4 /* definition in X & Y of a tile */
STATIC_ASSERT(IS_POW2(TILE_SIZE), TILE_SIZE_must_be_a_power_of_2);

struct pixel {
  struct accum acc_temp; /* Temperature accumulator */
  struct accum acc_time; /* Time accumulator */
};

/* Tile of row ordered pixels */
struct tile {
  struct list_node node;
  struct mem_allocator* allocator;
  ref_T ref;

  struct tile_data {
    uint16_t x, y; /* 2D coordinates of the tile in tile space */
    struct pixel ALIGN(16) pixels[TILE_SIZE*TILE_SIZE];
  } data;
};

extern LOCAL_SYM res_T
tile_create
  (struct mem_allocator* allocator,
   struct tile** tile);

extern LOCAL_SYM void
tile_ref_get
  (struct tile* tile);

extern LOCAL_SYM void
tile_ref_put
  (struct tile* tile);

static INLINE struct pixel*
tile_at(struct tile* tile, const uint16_t x, const uint16_t y)
{
  ASSERT(tile && x < TILE_SIZE && y < TILE_SIZE);
  return tile->data.pixels + y*TILE_SIZE + x;
}

#endif /* SDIS_TILE_H */
