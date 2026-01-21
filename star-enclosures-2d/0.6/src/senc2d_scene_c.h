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

#ifndef SENC2D_SCENE_C_H
#define SENC2D_SCENE_C_H

#include "senc2d_internal_types.h"
#include "senc2d_enclosure_data.h"
#include "senc2d_side_range.h"
#include "senc2d.h"

#include <rsys/ref_count.h>
#include <rsys/dynamic_array.h>
#include <rsys/hash_table.h>

struct mem_allocator;
struct senc2d_scene;

struct segment_comp {
  /* The connex component in which each side is. */
  component_id_t component[2];
};

static void
segment_comp_init(struct mem_allocator* alloc, struct segment_comp* seg) {
  int i;
  (void)alloc;
  ASSERT(seg);
  FOR_EACH(i, 0, 2) seg->component[i] = COMPONENT_NULL__;
}

#define DARRAY_NAME segment_comp
#define DARRAY_DATA struct segment_comp
#define DARRAY_FUNCTOR_INIT segment_comp_init
#include <rsys/dynamic_array.h>

struct segment_enc {
  /* The enclosure in which each side is. */
  enclosure_id_t enclosure[2];
};

#ifndef NDEBUG
static void
segment_enc_init(struct mem_allocator* alloc, struct segment_enc* seg) {
  int i;
  (void)alloc;
  ASSERT(seg);
  FOR_EACH(i, 0, 2) seg->enclosure[i] = ENCLOSURE_NULL__;
}
#define DARRAY_FUNCTOR_INIT segment_enc_init
#endif

#define DARRAY_NAME segment_enc
#define DARRAY_DATA struct segment_enc
#include <rsys/dynamic_array.h>

/* Segment edge struct and basic functions */
struct seg_edge {
  vrtx_id_t vrtx0, vrtx1;
};

static FINLINE int
edge_ok(const struct seg_edge* edge) {
  return(edge
    && edge->vrtx0 <= VRTX_MAX__
    && edge->vrtx1 <= VRTX_MAX__
    && edge->vrtx0 < edge->vrtx1);
}

static FINLINE void
set_edge
  (const vrtx_id_t vrtx0,
   const vrtx_id_t vrtx1,
   struct seg_edge* edge,
   uchar* reversed)
{
  ASSERT(edge && reversed && vrtx0 != vrtx1);
  ASSERT(*reversed == UCHAR_MAX); /* Should not be already set. */
  if(vrtx0 < vrtx1) {
    edge->vrtx0 = vrtx0;
    edge->vrtx1 = vrtx1;
    *reversed = 0; /* Non reversed edge */
  } else {
    edge->vrtx0 = vrtx1;
    edge->vrtx1 = vrtx0;
    *reversed = 1; /* Reversed edge */
  }
  ASSERT(edge_ok(edge));
}

static FINLINE int
edge_eq(const struct seg_edge* e1, const struct seg_edge* e2)
{
  ASSERT(edge_ok(e1) && edge_ok(e2));
  return e1->vrtx0 == e2->vrtx0 && e1->vrtx1 == e2->vrtx1;
}

/* Information kept during the building of side groups. */
struct segside {
  /* Rank of the segside facing this segside through its edges */
  side_id_t facing_side_id[2];
  /* Id of this segside's medium */
  medium_id_t medium;

  /* Implicit information that we don't need to store:
   * - segment_id
   * - side
   * This is due to the memory layout of the elt darray:
   * front(seg_0), back(seg_0), front(seg_1), back(seg_1), ... */
};

/* Frontier vertex type */
struct frontier_vertex {
  seg_id_t seg;
  vrtx_id_t vrtx;
};

#define DARRAY_NAME frontier_vertex
#define DARRAY_DATA struct frontier_vertex
#include <rsys/dynamic_array.h>

union double2 {
  struct {
    double x, y;
  } pos;
  double vec[2];
};
#define DARRAY_NAME position
#define DARRAY_DATA union double2
#include <rsys/dynamic_array.h>
/* Segment information.
 * Depending on lifespan, information is kept in different places:
 * - segment_in for user provided information (kept in scene)
 * - segment_comp for information describing components (kept in struct descriptor)
 * - segment_tmp for tmp information (kept until segment_comp is ready) */
struct segment_in {
  /* Ids of the segment's vertices */
  vrtx_id_t vertice_id[2];
  /* Ids of this segment's media */
  medium_id_t medium[2];
};

static FINLINE void
segment_in_init(struct mem_allocator* alloc, struct segment_in* seg) {
  int i;
  (void)alloc;
  ASSERT(seg);
  FOR_EACH(i, 0, 2) seg->vertice_id[i] = VRTX_NULL__;
  FOR_EACH(i, 0, 2) seg->medium[i] = SENC2D_UNSPECIFIED_MEDIUM;
}

#define DARRAY_NAME segment_in
#define DARRAY_DATA struct segment_in
#define DARRAY_FUNCTOR_INIT segment_in_init
#include <rsys/dynamic_array.h>

static FINLINE int
vrtx_eq(const union double2* v1, const union double2* v2)
{
  ASSERT(v1 && v2);
  return (v1->pos.x == v2->pos.x && v1->pos.y == v2->pos.y);
}

#define HTABLE_NAME vrtx
#define HTABLE_KEY union double2
#define HTABLE_DATA vrtx_id_t
#define HTABLE_KEY_FUNCTOR_EQ vrtx_eq
#include <rsys/hash_table.h>

union vrtx_id2 {
  struct {
    vrtx_id_t v0, v1;
  } pos;
  vrtx_id_t vec[2];
};

static FINLINE char /* Return 1 if reversed */
seg_make_key(union vrtx_id2* k, const vrtx_id_t v[2])
{
  ASSERT(v);
  ASSERT(v[0] != v[1]);
  if(v[0] < v[1]) {
    k->vec[0] = v[0];
    k->vec[1] = v[1];
    return 0;
  } else {
    k->vec[0] = v[1];
    k->vec[1] = v[0];
    return 1;
  }
}

static FINLINE int
seg_key_eq(const union vrtx_id2* k1, const union vrtx_id2* k2)
{
  ASSERT(k1 && k2);
  ASSERT(k1->vec[0] < k1->vec[1]);
  ASSERT(k2->vec[0] < k2->vec[1]);
  return (k1->vec[0] == k2->vec[0]) && (k1->vec[1] == k2->vec[1]);
}

#define HTABLE_NAME seg
#define HTABLE_KEY union vrtx_id2
#define HTABLE_DATA seg_id_t
#define HTABLE_KEY_FUNCTOR_EQ seg_key_eq
#include <rsys/hash_table.h>

#define DARRAY_NAME seg_id
#define DARRAY_DATA seg_id_t
#include <rsys/dynamic_array.h>

struct descriptor {
  enclosure_id_t enclosures_count;
  /* Store by-segment enclosures */
  struct darray_segment_enc segments_enc;
  /* Store enclosures */
  struct darray_enclosure enclosures;
  struct darray_enc_ids_array enc_ids_array_by_medium;
  /* Store frontiers */
  struct darray_frontier_vertex frontiers;
  /* Store overlapping segments */
  struct darray_seg_id overlapping_ids;
};

struct senc2d_scene {
  /* Front / Back sides convention */
  int convention;
  /* Segment information as given by user */
  struct darray_segment_in segments_in;
  /* Vertex information as given by user */
  struct darray_position vertices;

  /* Keep sizes */
  seg_id_t nsegs; /* Seg count */
  vrtx_id_t nverts; /* Vrtx count */
  medium_id_t next_medium_idx;
  /* media_use 0 is for SENC2D_UNSPECIFIED_MEDIUM, n+1 is for n */
  struct darray_side_range media_use;

  /* The descriptor of the analyze */
  struct descriptor analyze;

  ref_T ref;
  struct senc2d_device* dev;
};

#endif /* SENC2D_SCENE_C_H */
