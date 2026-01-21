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

#ifndef SENC3D_SIDE_RANGE_H
#define SENC3D_SIDE_RANGE_H

#include "senc3d_internal_types.h"

#include <rsys/dynamic_array.h>

struct mem_allocator;

struct side_range {
  side_id_t first, last;
};

static FINLINE void
side_range_init(struct mem_allocator* alloc, struct side_range* data)
{
  ASSERT(data);
  (void)alloc;
  data->first = SIDE_NULL__;
  data->last = 0;
}

#define DARRAY_NAME side_range
#define DARRAY_DATA struct side_range
#define DARRAY_FUNCTOR_INIT side_range_init
#include <rsys/dynamic_array.h>


#endif /* SENC3D_SCENE_C_H */
