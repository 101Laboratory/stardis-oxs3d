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

#ifndef SENC3D_SCENE_C_H
#define SENC3D_SCENE_C_H

#include "senc3d_internal_types.h"
#include "senc3d_enclosure_data.h"
#include "senc3d_side_range.h"
#include "senc3d.h"

#include <rsys/ref_count.h>
#include <rsys/dynamic_array.h>
#include <rsys/hash_table.h>

struct mem_allocator;
struct senc3d_scene;

struct triangle_comp {
  /* The connex component in which each side is. */
  component_id_t component[2];
};

static void
triangle_comp_init(struct mem_allocator* alloc, struct triangle_comp* trg) {
  int i;
  (void)alloc;
  ASSERT(trg);
  FOR_EACH(i, 0, 2) trg->component[i] = COMPONENT_NULL__;
}

#define DARRAY_NAME triangle_comp
#define DARRAY_DATA struct triangle_comp
#define DARRAY_FUNCTOR_INIT triangle_comp_init
#include <rsys/dynamic_array.h>

struct triangle_enc {
  /* The enclosure in which each side is. */
  enclosure_id_t enclosure[2];
};

#ifndef NDEBUG
static void
triangle_enc_init(struct mem_allocator* alloc, struct triangle_enc* trg) {
  int i;
  (void)alloc;
  ASSERT(trg);
  FOR_EACH(i, 0, 2) trg->enclosure[i] = ENCLOSURE_NULL__;
}
#define DARRAY_FUNCTOR_INIT triangle_enc_init
#endif

#define DARRAY_NAME triangle_enc
#define DARRAY_DATA struct triangle_enc
#include <rsys/dynamic_array.h>

/* Triangle edge struct and basic functions */
struct trg_edge {
  vrtx_id_t vrtx0, vrtx1;
};

static FINLINE int
edge_ok(const struct trg_edge* edge) {
  return(edge
    && edge->vrtx0 <= VRTX_MAX__
    && edge->vrtx1 <= VRTX_MAX__
    && edge->vrtx0 < edge->vrtx1);
}

static FINLINE void
set_edge
  (const vrtx_id_t vrtx0,
   const vrtx_id_t vrtx1,
   struct trg_edge* edge,
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
edge_eq(const struct trg_edge* e1, const struct trg_edge* e2)
{
  ASSERT(edge_ok(e1) && edge_ok(e2));
  return e1->vrtx0 == e2->vrtx0 && e1->vrtx1 == e2->vrtx1;
}

/* Information kept during the building of side groups. */
struct trgside {
  /* Rank of the trgside facing this trgside through its edges */
  side_id_t facing_side_id[3];
  /* Id of this trgside's medium */
  medium_id_t medium;

  /* Implicit information that we don't need to store:
   * - triangle_id
   * - side
   * This is due to the memory layout of the elt darray:
   * front(trg_0), back(trg_0), front(trg_1), back(trg_1), ... */
};

/* Frontier edge type */
struct frontier_edge {
  trg_id_t trg;
  vrtx_id_t vrtx0, vrtx1;
};

#define DARRAY_NAME frontier_edge
#define DARRAY_DATA struct frontier_edge
#include <rsys/dynamic_array.h>

union double3 {
  struct {
    double x, y, z;
  } pos;
  double vec[3];
};
#define DARRAY_NAME position
#define DARRAY_DATA union double3
#include <rsys/dynamic_array.h>
/* Triangle information.
 * Depending on lifespan, information is kept in different places:
 * - triangle_in for user provided information (kept in scene)
 * - triangle_comp for information describing components (kept in struct descriptor)
 * - triangle_tmp for tmp information (kept until triangle_comp is ready) */
struct triangle_in {
  /* Ids of the triangle's vertices */
  vrtx_id_t vertice_id[3];
  /* Ids of this triangle's media */
  medium_id_t medium[2];
};

static FINLINE void
triangle_in_init(struct mem_allocator* alloc, struct triangle_in* trg) {
  int i;
  (void)alloc;
  ASSERT(trg);
  FOR_EACH(i, 0, 3) trg->vertice_id[i] = VRTX_NULL__;
  FOR_EACH(i, 0, 2) trg->medium[i] = SENC3D_UNSPECIFIED_MEDIUM;
}

#define DARRAY_NAME triangle_in
#define DARRAY_DATA struct triangle_in
#define DARRAY_FUNCTOR_INIT triangle_in_init
#include <rsys/dynamic_array.h>

static FINLINE void
triangle_in_flip(struct triangle_in* trg) {
  vrtx_id_t v;
  medium_id_t m;
  ASSERT(trg);
  v = trg->vertice_id[1];
  trg->vertice_id[1] = trg->vertice_id[2];
  trg->vertice_id[2] = v;
  m = trg->medium[0];
  trg->medium[0] = trg->medium[1];
  trg->medium[1] = m;
}

static FINLINE int
vrtx_eq(const union double3* v1, const union double3* v2)
{
  ASSERT(v1 && v2);
  return (v1->pos.x == v2->pos.x
    && v1->pos.y == v2->pos.y
    && v1->pos.z == v2->pos.z);
}

#define HTABLE_NAME vrtx
#define HTABLE_KEY union double3
#define HTABLE_DATA vrtx_id_t
#define HTABLE_KEY_FUNCTOR_EQ vrtx_eq
#include <rsys/hash_table.h>

union vrtx_id3 {
  struct {
    vrtx_id_t v0, v1, v2;
  } pos;
  vrtx_id_t vec[3];
};

static FINLINE char /* Return 1 if reversed */
trg_make_key(union vrtx_id3* k, const vrtx_id_t t[3])
{
  ASSERT(t);
  ASSERT(t[0] != t[1] && t[0] != t[2] && t[1] != t[2]);
  if(t[0] < t[2]) {
    if(t[0] < t[1]) {
      k->vec[0] = t[0];
      if(t[1] < t[2]) {
        k->vec[1] = t[1];
        k->vec[2] = t[2];
        return 0;
      } else {
        k->vec[1] = t[2];
        k->vec[2] = t[1];
        return 1;
      }
    } else {
      k->vec[0] = t[1];
      if(t[0] < t[2]) {
        k->vec[1] = t[0];
        k->vec[2] = t[2];
        return 1;
      } else {
        k->vec[1] = t[2];
        k->vec[2] = t[0];
        return 0;
      }
    }
  } else if(t[2] < t[1]) {
    k->vec[0] = t[2];
    if(t[0] < t[1]) {
      k->vec[1] = t[0];
      k->vec[2] = t[1];
      return 0;
    } else {
      k->vec[1] = t[1];
      k->vec[2] = t[0];
      return 1;
    }
  } else {
    k->vec[0] = t[1];
    if(t[0] < t[2]) {
      k->vec[1] = t[0];
      k->vec[2] = t[2];
      return 1;
    } else {
      k->vec[1] = t[2];
      k->vec[2] = t[0];
      return 0;
    }
  }
}

static FINLINE int
trg_key_eq(const union vrtx_id3* k1, const union vrtx_id3* k2)
{
  ASSERT(k1 && k2);
  ASSERT(k1->vec[0] < k1->vec[1] && k1->vec[1] < k1->vec[2]);
  ASSERT(k2->vec[0] < k2->vec[1] && k2->vec[1] < k2->vec[2]);
  return (k1->vec[0] == k2->vec[0])
    && (k1->vec[1] == k2->vec[1])
    && (k1->vec[2] == k2->vec[2]);
}

#define HTABLE_NAME trg
#define HTABLE_KEY union vrtx_id3
#define HTABLE_DATA trg_id_t
#define HTABLE_KEY_FUNCTOR_EQ trg_key_eq
#include <rsys/hash_table.h>

#define DARRAY_NAME trg_id
#define DARRAY_DATA trg_id_t
#include <rsys/dynamic_array.h>

struct descriptor {
  enclosure_id_t enclosures_count;
  /* Store by-triangle enclosures */
  struct darray_triangle_enc triangles_enc;
  /* Store enclosures */
  struct darray_enclosure enclosures;
  struct darray_enc_ids_array enc_ids_array_by_medium;
  /* Store frontiers */
  struct darray_frontier_edge frontiers;
  /* Store overlapping triangles */
  struct darray_trg_id overlapping_ids;
};

struct senc3d_scene {
  /* Front / Back sides convention */
  int convention;
  /* Triangle information as given by user */
  struct darray_triangle_in triangles_in;
  /* Vertex information as given by user */
  struct darray_position vertices;

  /* Keep sizes */
  trg_id_t ntris; /* Trg count */
  vrtx_id_t nverts; /* Vrtx count */
  /* media_use 0 is for SENC3D_UNSPECIFIED_MEDIUM, n+1 is for n */
  struct darray_side_range media_use;

  /* The descriptor of the analyze */
  struct descriptor analyze;

  ref_T ref;
  struct senc3d_device* dev;
};

#endif /* SENC3D_SCENE_C_H */
