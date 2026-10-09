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

#ifndef TEST_SENC2_UTILS_H
#define TEST_SENC2_UTILS_H

#include <rsys/rsys.h>
#include<rsys/mem_allocator.h>
#include <rsys/stretchy_array.h>
#include <rsys/double2.h>

#include <stdio.h>

#define OK(Expr) CHK((Expr) == RES_OK)
#define BA(Expr) CHK((Expr) == RES_BAD_ARG)
#define BO(Expr) CHK((Expr) == RES_BAD_OP)

/******************************************************************************
 * Geometry
 *****************************************************************************/
/* Distorded square */
static const double
box_vertices[4/*#vertices*/ * 2/*#coords per vertex*/] = {
  0.1, 0.0,
  1.0, 0.0,
  0.0, 1.1,
  1.0, 1.0
};
/* Need a true square for some tests */
static const double
square_vertices[4/*#vertices*/ * 2/*#coords per vertex*/] = {
  0.0, 0.0,
  1.0, 0.0,
  0.0, 1.0,
  1.0, 1.0
};
static const unsigned
nvertices = sizeof(box_vertices) / (2 * sizeof(*box_vertices));
STATIC_ASSERT(sizeof(box_vertices) == sizeof(square_vertices),
  The_2_geometries_must_have_the_same_number_of_vertices);

/* The following array lists the indices toward the 2D vertices of each
 * segment.
 *                   Y
 *    2----3         |
 *    |    |         0----X
 *    |    |
 *    0----1
 */
static const unsigned
box_indices[4/*#segments*/ * 2/*#indices per segment*/] = {
  0, 2,
  2, 3,
  3, 1,
  1, 0
};
static const unsigned
nsegments = sizeof(box_indices) / (2 * sizeof(*box_indices));

struct context {
  const double* positions;
  const unsigned* indices;
  const unsigned* front_media;
  const unsigned* back_media;
  const unsigned* properties;
  void* custom;
  double offset[2];
  double scale;
  char reverse_vrtx, reverse_med;
};
#define CONTEXT_NULL__ {\
  NULL, NULL, NULL, NULL, NULL, NULL, {0,0}, 1, 0, 0\
}

static const unsigned medium0[4] = { 0, 0, 0, 0 };
static const unsigned medium1[4] = { 1, 1, 1, 1 };
static const unsigned medium2[4] = { 2, 2, 2, 2 };
static const unsigned medium1_3[4] = { 1, 1, 3, 1 };
static const unsigned medium1_back0[4] = { 1, 1, 1, 0 };
static const unsigned medium1_front0[4] = { 1, 0, 1, 1 };

static INLINE void
get_indices(const unsigned iseg, unsigned ids[2], void* context)
{
  const struct context* ctx = context;
  ASSERT(ids && ctx);
  ids[ctx->reverse_vrtx ? 1 : 0] = ctx->indices[iseg * 2 + 0];
  ids[ctx->reverse_vrtx ? 0 : 1] = ctx->indices[iseg * 2 + 1];
}

static INLINE void
get_position(const unsigned ivert, double pos[2], void* context)
{
  const struct context* ctx = context;
  ASSERT(pos && ctx);
  pos[0] = ctx->positions[ivert * 2 + 0] * ctx->scale + ctx->offset[0];
  pos[1] = ctx->positions[ivert * 2 + 1] * ctx->scale + ctx->offset[1];
}

static INLINE void
get_media(const unsigned iseg, unsigned medium[2], void* context)
{
  const struct context* ctx = context;
  ASSERT(medium && ctx);
  medium[ctx->reverse_med ? 1 : 0] = ctx->front_media[iseg];
  medium[ctx->reverse_med ? 0 : 1] = ctx->back_media[iseg];
}

static INLINE void
get_media_from_properties(const unsigned iseg, unsigned medium[2], void* context)
{
  const struct context* ctx = context;
  ASSERT(medium && ctx);
  medium[ctx->reverse_med ? 1 : 0] = ctx->properties[3 * iseg + 0];
  medium[ctx->reverse_med ? 0 : 1] = ctx->properties[3 * iseg + 1];
}

/******************************************************************************
 * Miscellaneous
 *****************************************************************************/
static INLINE void
dump_global
  (struct senc2d_scene* scn,
   const char* name)
{
  FILE* stream;
  unsigned segments_count, vertices_count, i;

  ASSERT(scn && name);

  OK(senc2d_scene_get_vertices_count(scn, &vertices_count));
  OK(senc2d_scene_get_segments_count(scn, &segments_count));

  stream = fopen(name, "w");
  CHK(stream);
  FOR_EACH(i, 0, vertices_count) {
    double tmp[2];
    OK(senc2d_scene_get_vertex(scn, i, tmp));
    fprintf(stream, "v %g %g\n", SPLIT2(tmp));
  }
  FOR_EACH(i, 0, segments_count) {
    unsigned indices[2];
    OK(senc2d_scene_get_segment(scn, i, indices));
    fprintf(stream, "l %u %u\n", 1 + indices[0], 1 + indices[1]);
  }
  fclose(stream);
}

static INLINE void
dump_enclosure
  (struct senc2d_scene* scn,
   const unsigned enc,
   const char* name)
{
  struct senc2d_enclosure* enclosure;
  struct senc2d_enclosure_header header;
  FILE* stream;
  unsigned count, i;

  ASSERT(scn && name);

  SENC2D(scene_get_enclosure_count(scn, &count));
  ASSERT(enc < count);
  OK(senc2d_scene_get_enclosure(scn, enc, &enclosure));
  OK(senc2d_enclosure_get_header(enclosure, &header));

  stream = fopen(name, "w");
  CHK(stream);
  FOR_EACH(i, 0, header.vertices_count) {
    double tmp[2];
    OK(senc2d_enclosure_get_vertex(enclosure, i, tmp));
    fprintf(stream, "v %g %g\n", SPLIT2(tmp));
  }
  FOR_EACH(i, 0, header.primitives_count) {
    unsigned indices[2];
    OK(senc2d_enclosure_get_segment(enclosure, i, indices));
    fprintf(stream, "l %u %u\n", 1+indices[0], 1+indices[1]);
  }
  OK(senc2d_enclosure_ref_put(enclosure));
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
 * Circle functions
 *****************************************************************************/
static INLINE void
create_circle
  (const double radius,
   const unsigned nslices,
   struct context* ctx)
{
  double step_theta;
  unsigned itheta;
  unsigned islice;
  double* d = NULL;
  unsigned* u = NULL;
  ASSERT(radius > 0 && nslices >= 3 && ctx);

  step_theta = 2 * PI / (double)nslices;
  FOR_EACH(itheta, 0, nslices) {
    const double theta = (double)itheta * step_theta;
    const double x = cos(theta);
    const double y = sin(theta);
    sa_push(d, x * radius);
    sa_push(d, y * radius);
  }
  ctx->positions = d;

  FOR_EACH(islice, 0, nslices) {
    const unsigned v0 = islice;
    const unsigned v1 = ((islice + 1) % nslices);
    sa_push(u, v0);
    sa_push(u, v1);
  }
  ctx->indices = u;
}

static INLINE void
circle_release(struct context* ctx)
{
  ASSERT(ctx);
  sa_release(ctx->positions);
  sa_release(ctx->indices);
  ctx->positions = NULL;
  ctx->indices = NULL;
}

/******************************************************************************
 * Check functions
 *****************************************************************************/
/* Compare the iseg-th segment of enclosure with a segment described by seg2 & vertices2 */
static INLINE void
cmp_seg
  (const unsigned iseg,
   const struct senc2d_enclosure* enclosure,
   const unsigned seg2[2],
   const double* vertices2,
   int* seg_eq,
   int* seg_reversed)
{
  unsigned seg1[2];
  double s1[2][2];
  double s2[2][2];
  unsigned seg1_eq[2] = { 2, 2 };
  unsigned i, j;

  ASSERT(enclosure && seg2 && vertices2 && seg_eq && seg_reversed);

  OK(senc2d_enclosure_get_segment(enclosure, iseg, seg1));
  FOR_EACH(i, 0, 2) {
    OK(senc2d_enclosure_get_vertex(enclosure, seg1[i], s1[i]));
    d2_set(s2[i], vertices2 + (2 * seg2[i]));
  }
  FOR_EACH(i, 0, 2) {
    FOR_EACH(j, 0, 2) {
      if(d2_eq(s1[i], s2[j])) {
        seg1_eq[i] = j;
        break;
      }
    }
  }
  FOR_EACH(i, 0, 2) {
    if(seg1_eq[i] == 2) {
      *seg_eq = 0;
      return;
    }
    if(seg1_eq[i] == seg1_eq[(i + 1) % 2]) {
      *seg_eq = 0;
      return;
    }
  }
  /* Same 2 vertices */
  *seg_eq = 1;

  *seg_reversed = (0 != seg1_eq[0]);
  ASSERT(*seg_reversed == (1 != seg1_eq[1]));
}

#endif /* TEST_SENC2_UTILS_H */
