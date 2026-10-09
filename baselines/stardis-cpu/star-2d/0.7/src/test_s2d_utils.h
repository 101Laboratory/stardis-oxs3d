/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef S2D_UTILS_H
#define S2D_UTILS_H

#include<rsys/mem_allocator.h>
#include <stdio.h>

struct line_segments_desc {
  const float* vertices;
  const unsigned* indices;
};

/*******************************************************************************
 * Geometries
 ******************************************************************************/
static const float square_verts[] = {
  11.f, 9.f,
  9.f, 9.f,
  9.f, 11.f,
  11.f, 11.f
};
const unsigned square_nverts = sizeof(square_verts)/(sizeof(float)*2);

const unsigned square_ids[] = {
  0, 1, /* Bottom */
  1, 2, /* Left */
  2, 3, /* Top */
  3, 0  /* Right */
};
const unsigned square_nsegs = sizeof(square_ids)/(sizeof(unsigned)*2);

static const struct line_segments_desc square_desc = { square_verts, square_ids };

static const float line_verts[] = { 9.f, 10.f, 11.f, 10.f };
const unsigned line_nverts = sizeof(line_verts)/(sizeof(float)*2);
const unsigned line_ids[] = { 0, 1 };
const unsigned line_nsegs = sizeof(line_ids)/(sizeof(unsigned)*2);
static const struct line_segments_desc line_desc = { line_verts, line_ids };

static INLINE void
line_segments_get_ids(const unsigned isegment, unsigned ids[2], void* data)
{
  const unsigned id = isegment * 2;
  const struct line_segments_desc* desc = data;
  CHK(desc != NULL);
  ids[0] = desc->indices[id + 0];
  ids[1] = desc->indices[id + 1];
}

static INLINE void
line_segments_get_position(const unsigned ivert, float position[2], void* data)
{
  const unsigned id = ivert * 2;
  const struct line_segments_desc* desc = data;
  CHK(desc != NULL);
  position[0] = desc->vertices[id + 0];
  position[1] = desc->vertices[id + 1];
}

/*******************************************************************************
 * Miscellaneous function
 ******************************************************************************/
static INLINE float
rand_canonic(void)
{
  int r;
  while((r = rand()) == RAND_MAX);
  return (float)r / (float)RAND_MAX;
}

static INLINE void
check_memory_allocator(struct mem_allocator* allocator)
{
  if(MEM_ALLOCATED_SIZE(allocator)) {
    char dump[512];
    MEM_DUMP(allocator, dump, sizeof(dump)/sizeof(char));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks\n");
  }
}

#endif /* S2D_UTILS_H */

