/* Copyright (C) 2019, 2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_SG3D_UTILS_H
#define TEST_SG3D_UTILS_H

#include <star/sg3d.h>

#include <rsys/mem_allocator.h>
#include <rsys/double3.h>

#include <stdio.h>

#define OK(Cond) CHK((Cond) == RES_OK)
#define BA(Cond) CHK((Cond) == RES_BAD_ARG)
#define ME(Cond) CHK((Cond) == RES_MEM_ERR)


/******************************************************************************
 * Memory allocator
 *****************************************************************************/
static INLINE void
check_memory_allocator(struct mem_allocator* allocator)
{
  if(MEM_ALLOCATED_SIZE(allocator)) {
    char dump[80192];
    MEM_DUMP(allocator, dump, sizeof(dump));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks.\n");
  }
}

/******************************************************************************
 * Geometry
 *****************************************************************************/
/* 3D cube */
static const double cube_vertices[8/*#vertices*/ * 3/*#coords per vertex*/] = {
  0.0, 0.0, 0.0,
  1.0, 0.0, 0.0,
  0.0, 1.0, 0.0,
  1.0, 1.0, 0.0,
  0.0, 0.0, 1.0,
  1.0, 0.0, 1.0,
  0.0, 1.0, 1.0,
  1.0, 1.0, 1.0
};
static const unsigned nvertices = sizeof(cube_vertices) / (3*sizeof(double));
/* Distorded cube */
static const double box_vertices[8/*#vertices*/ * 3/*#coords per vertex*/] = {
  0.1, 0.0, 0.0,
  1.0, 0.0, 0.0,
  0.0, 1.0, 0.0,
  1.0, 1.0, 0.0,
  0.0, 0.0, 1.1,
  1.0, 0.0, 1.0,
  0.0, 1.0, 1.0,
  1.0, 1.1, 1.0
};

/* The following array lists the indices toward the 3D vertices of each
 * triangle.
 *        ,2---,3           ,2----3
 *      ,' | ,'/|         ,'/| \  |
 *    6----7' / |       6' / |  \ |        Y
 *    |',  | / ,1       | / ,0---,1        |
 *    |  ',|/,'         |/,' | ,'          o--X
 *    4----5'           4----5'           /
 *  Front, right      Back, left and     Z
 * and Top faces       bottom faces
 *
 * The right-handed geometrical normal is outside the cube */
static const unsigned
cube_indices[12/*#triangles*/ * 3/*#indices per triangle*/] = {
  0, 2, 1, 1, 2, 3, /* Front face */
  0, 4, 2, 2, 4, 6, /* Left face*/
  4, 5, 6, 6, 5, 7, /* Back face */
  3, 7, 1, 1, 7, 5, /* Right face */
  2, 6, 3, 3, 6, 7, /* Top face */
  0, 1, 4, 4, 1, 5  /* Bottom face */
};
static const unsigned
ntriangles = sizeof(cube_indices) / (3 * sizeof(*cube_indices));

struct context {
  const double* positions;
  const unsigned* indices;
  const unsigned* front_media;
  const unsigned* back_media;
  const unsigned* intface;
  void* custom;
  double offset[3];
  double scale[3];
  char reverse_vrtx, reverse_med;
};
#define CONTEXT_NULL__ {\
  NULL, NULL, NULL, NULL, NULL, NULL, {0,0,0}, {1, 1, 1}, 0, 0\
}

static const unsigned medium0[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const unsigned medium1[12] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const unsigned medium2[12] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static const unsigned medium1_3[12] = { 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1 };
static const unsigned medium1_back0[12] = { 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1 };
static const unsigned medium1_front0[12] = { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

static const unsigned intface0[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const unsigned intface1[12] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

static INLINE void
get_indices(const unsigned itri, unsigned ids[3], void* context)
{
  const struct context* ctx = context;
  ASSERT(ids && ctx);
  ids[0] = ctx->indices[itri * 3 + 0];
  ids[ctx->reverse_vrtx ? 2 : 1] = ctx->indices[itri * 3 + 1];
  ids[ctx->reverse_vrtx ? 1 : 2] = ctx->indices[itri * 3 + 2];
}

static INLINE void
get_position(const unsigned ivert, double pos[3], void* context)
{
  const struct context* ctx = context;
  double tmp[3];
  ASSERT(pos && ctx);
  d3_add(pos, d3_mul(tmp, ctx->positions + ivert * 3, ctx->scale), ctx->offset);
}

static INLINE void
get_properties
  (const unsigned itri,
   unsigned property[SG3D_PROP_TYPES_COUNT__],
   void* context)
{
  const struct context* ctx = context;
  ASSERT(property && ctx);
  property[ctx->reverse_med ? SG3D_BACK : SG3D_FRONT] = ctx->front_media[itri];
  property[ctx->reverse_med ? SG3D_FRONT : SG3D_BACK] = ctx->back_media[itri];
  property[SG3D_INTFACE] = ctx->intface[itri];
}

#endif /* TEST_SG3D_UTILS_H */
