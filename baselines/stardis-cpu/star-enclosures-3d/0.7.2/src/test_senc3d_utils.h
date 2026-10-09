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

#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <rsys/rsys.h>
#include <rsys/mem_allocator.h>
#include <rsys/double3.h>

#include <stdio.h>

#include <star/senc3d.h>

#define OK(Expr) CHK((Expr) == RES_OK)
#define BA(Expr) CHK((Expr) == RES_BAD_ARG)
#define BO(Expr) CHK((Expr) == RES_BAD_OP)

/******************************************************************************
 * Geometry
 *****************************************************************************/
/* Distorded cube */
static const double box_vertices[8/*#vertices*/*3/*#coords per vertex*/] = {
  0.1, 0.0, 0.0,
  1.0, 0.0, 0.0,
  0.0, 1.0, 0.0,
  1.0, 1.0, 0.0,
  0.0, 0.0, 1.1,
  1.0, 0.0, 1.0,
  0.0, 1.0, 1.0,
  1.0, 1.1, 1.0
};
/* Need a true cube for some tests */
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
static const unsigned nvertices = sizeof(box_vertices) / (3*sizeof(double));
STATIC_ASSERT(sizeof(box_vertices) == sizeof(cube_vertices),
  The_2_geometries_must_have_the_same_number_of_vertices);

/* The following array lists the indices toward the 3D vertices of each
 * triangle.
 *        ,2---,3           ,2----3
 *      ,' | ,'/|         ,'/| \  |
 *    6----7' / |       6' / |  \ |        Y
 *    |',  | / ,1       | / ,0---,1        |
 *    |  ',|/,'         |/,' | ,'          o--X
 *    4----5'           4----5'           /
 *  Front, right      Back, left and     Z
 * and Top faces       bottom faces */
static const unsigned
box_indices[12/*#triangles*/*3/*#indices per triangle*/] = {
  0, 2, 1, 1, 2, 3, /* Front face */
  0, 4, 2, 2, 4, 6, /* Left face*/
  4, 5, 6, 6, 5, 7, /* Back face */
  3, 7, 1, 1, 7, 5, /* Right face */
  2, 6, 3, 3, 6, 7, /* Top face */
  0, 1, 4, 4, 1, 5  /* Bottom face */
};
static const unsigned
ntriangles = sizeof(box_indices) / (3 * sizeof(*box_indices));

struct context {
  const double* positions;
  const unsigned* indices;
  const unsigned* front_media;
  const unsigned* back_media;
  const unsigned* properties;
  void* custom;
  double offset[3];
  double scale;
  char reverse_vrtx, reverse_med;
};
#define CONTEXT_NULL__ {\
  NULL, NULL, NULL, NULL, NULL, NULL, {0,0,0}, 1, 0, 0\
}

static const unsigned medium0[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const unsigned medium1[12] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const unsigned medium2[12] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };
static const unsigned medium1_3[12] = { 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1 };
static const unsigned medium1_back0[12] = { 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1 };
static const unsigned medium1_front0[12] = { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

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
  ASSERT(pos && ctx && ctx->scale != 0);
  pos[0] = ctx->positions[ivert * 3 + 0] * ctx->scale + ctx->offset[0];
  pos[1] = ctx->positions[ivert * 3 + 1] * ctx->scale + ctx->offset[1];
  pos[2] = ctx->positions[ivert * 3 + 2] * ctx->scale + ctx->offset[2];
}

static INLINE void
get_media(const unsigned itri, unsigned medium[2], void* context)
{
  const struct context* ctx = context;
  ASSERT(medium && ctx);
  medium[ctx->reverse_med ? 1 : 0] = ctx->front_media[itri];
  medium[ctx->reverse_med ? 0 : 1] = ctx->back_media[itri];
}

static INLINE void
get_media_from_properties(const unsigned itri, unsigned medium[2], void* context)
{
  const struct context* ctx = context;
  ASSERT(medium && ctx);
  medium[ctx->reverse_med ? 1 : 0] = ctx->properties[3 * itri + 0];
  medium[ctx->reverse_med ? 0 : 1] = ctx->properties[3 * itri + 1];
}

/******************************************************************************
 * Miscellaneous
 *****************************************************************************/
static INLINE void
dump_global
  (struct senc3d_scene* scn,
   const char* name)
{
  FILE* stream;
  unsigned triangles_count, vertices_count, i;

  ASSERT(scn && name);

  OK(senc3d_scene_get_vertices_count(scn, &vertices_count));
  OK(senc3d_scene_get_triangles_count(scn, &triangles_count));

  stream = fopen(name, "w");
  CHK(stream);
  FOR_EACH(i, 0, vertices_count) {
    double tmp[3];
    OK(senc3d_scene_get_vertex(scn, i, tmp));
    fprintf(stream, "v %g %g %g\n", SPLIT3(tmp));
  }
  FOR_EACH(i, 0, triangles_count) {
    unsigned indices[3];
    OK(senc3d_scene_get_triangle(scn, i, indices));
    fprintf(stream, "f %u %u %u\n",
      1 + indices[0], 1 + indices[1], 1 + indices[2]);
  }
  fclose(stream);
}

static INLINE void
dump_enclosure
  (struct senc3d_scene* scn,
   const unsigned enc,
   const char* name)
{
  struct senc3d_enclosure* enclosure;
  struct senc3d_enclosure_header header;
  FILE* stream;
  unsigned count, i;

  ASSERT(scn && name);

  SENC3D(scene_get_enclosure_count(scn, &count));
  ASSERT(enc < count);
  OK(senc3d_scene_get_enclosure(scn, enc, &enclosure));
  OK(senc3d_enclosure_get_header(enclosure, &header));

  stream = fopen(name, "w");
  CHK(stream);
  FOR_EACH(i, 0, header.vertices_count) {
    double tmp[3];
    OK(senc3d_enclosure_get_vertex(enclosure, i, tmp));
    fprintf(stream, "v %g %g %g\n", SPLIT3(tmp));
  }
  FOR_EACH(i, 0, header.primitives_count) {
    unsigned indices[3];
    OK(senc3d_enclosure_get_triangle(enclosure, i, indices));
    fprintf(stream, "f %u %u %u\n",
      1+indices[0], 1+indices[1], 1+indices[2]);
  }
  OK(senc3d_enclosure_ref_put(enclosure));
  fclose(stream);
}

static INLINE void
check_memory_allocator(struct mem_allocator* allocator)
{
  if(MEM_ALLOCATED_SIZE(allocator)) {
    char dump[1024];
    MEM_DUMP(allocator, dump, sizeof(dump));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks.\n");
  }
}

/******************************************************************************
 * Check functions
 *****************************************************************************/
/* Compare the itri-th triangle of enclosure with a triangle described by trg2 & vertices2 */
static INLINE void
cmp_trg
  (const unsigned itri,
   const struct senc3d_enclosure* enclosure,
   const unsigned trg2[3],
   const double* vertices2,
   int* trg_eq,
   int* trg_reversed)
{
  unsigned trg1[3];
  double t1[3][3];
  double t2[3][3];
  unsigned trg1_eq[3] = { 3, 3, 3 };
  unsigned i, j, fst_vrtx = 3;

  ASSERT(enclosure && trg2 && vertices2 && trg_eq && trg_reversed);

  OK(senc3d_enclosure_get_triangle(enclosure, itri, trg1));
  FOR_EACH(i, 0, 3) {
    OK(senc3d_enclosure_get_vertex(enclosure, trg1[i], t1[i]));
    d3_set(t2[i], vertices2 + (3 * trg2[i]));
  }
  FOR_EACH(i, 0, 3) {
    FOR_EACH(j, 0, 3) {
      if(d3_eq(t1[i], t2[j])) {
        trg1_eq[i] = j;
        if(i == 0) fst_vrtx = j;
        break;
      }
    }
  }
  FOR_EACH(i, 0, 3) {
    if(trg1_eq[i] == 3) {
      *trg_eq = 0;
      return;
    }
    if(trg1_eq[i] == trg1_eq[(i + 1) % 3]
      || trg1_eq[i] == trg1_eq[(i + 2) % 3]) {
      *trg_eq = 0;
      return;
    }
  }
  /* Same 3 vertices */
  ASSERT(fst_vrtx != 3);
  *trg_eq = 1;

  *trg_reversed = (trg1_eq[1] != (fst_vrtx + 1) % 3);
  ASSERT(*trg_reversed != (trg1_eq[1] != (fst_vrtx + 2) % 3));
}

#endif /* TEST_UTILS_H */
