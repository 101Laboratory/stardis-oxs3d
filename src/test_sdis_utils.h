/* Copyright (C) |Meso|Star> 2016-2017 (contact@meso-star.com)
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

#ifndef TEST_SDIS_UTILS_H
#define TEST_SDIS_UTILS_H

#include <rsys/mem_allocator.h>
#include <stdio.h>

/*******************************************************************************
 * Geometry
 ******************************************************************************/
static const double box_vertices[8/*#vertices*/*3/*#coords per vertex*/] = {
  0.0, 0.0, 0.0,
  1.0, 0.0, 0.0,
  0.0, 1.0, 0.0,
  1.0, 1.0, 0.0,
  0.0, 0.0, 1.0,
  1.0, 0.0, 1.0,
  0.0, 1.0, 1.0,
  1.0, 1.0, 1.0
};
static const size_t box_nvertices = sizeof(box_vertices) / sizeof(double[3]);

/* The following array lists the indices toward the 3D vertices of each
 * triangle.
 *        ,6---,7           ,6----7
 *      ,' | ,'/|         ,' | \  |
 *    2----3' / |       2',  |  \ |
 *    |',  | / ,5       |  ',4---,5
 *    |  ',|/,'         | ,' | ,'
 *    0----1'           0----1'
 *  Front, right      Back, left and
 * and Top faces       bottom faces */
static const size_t box_indices[12/*#triangles*/*3/*#indices per triangle*/] = {
  0, 2, 1, 1, 2, 3, /* Front face */
  0, 4, 2, 2, 4, 6, /* Left face*/
  4, 5, 6, 6, 5, 7, /* Back face */
  3, 7, 1, 1, 7, 5, /* Right face */
  2, 6, 3, 3, 6, 7, /* Top face */
  0, 1, 4, 4, 1, 5  /* Bottom face */
};
static const size_t box_ntriangles = sizeof(box_indices) / sizeof(size_t[3]);

/*******************************************************************************
 * Medium & interface
 ******************************************************************************/
static INLINE double
dummy_medium_getter
  (const struct sdis_rwalk_vertex* vert, struct sdis_data* data)
{
  (void)data;
  CHK(vert != NULL);
  return 1;
}

static INLINE double
dummy_interface_getter
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  (void)data;
  CHK(frag != NULL);
  return 1;
}

static const struct sdis_solid_shader DUMMY_SOLID_SHADER = {
  dummy_medium_getter,
  dummy_medium_getter,
  dummy_medium_getter,
  dummy_medium_getter,
  dummy_medium_getter,
  dummy_medium_getter
};

static const struct sdis_fluid_shader DUMMY_FLUID_SHADER = {
  dummy_medium_getter,
  dummy_medium_getter,
  dummy_medium_getter
};

static const struct sdis_interface_shader DUMMY_INTERFACE_SHADER = {
  dummy_interface_getter,
  dummy_interface_getter
};

/*******************************************************************************
 * Miscellaneous
 ******************************************************************************/
static INLINE void
check_memory_allocator(struct mem_allocator* allocator)
{
  if(MEM_ALLOCATED_SIZE(allocator)) {
    char dump[128];
    MEM_DUMP(allocator, dump, sizeof(dump));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks.\n");
  }
}

#endif /* TEST_SDIS_UTILS_H */

