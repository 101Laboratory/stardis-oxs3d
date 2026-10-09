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

#ifndef SENC3D_SCNENE_ANALYZE_C_H
#define SENC3D_SCNENE_ANALYZE_C_H

#include "senc3d_internal_types.h"
#include "senc3d_side_range.h"
#include "senc3d_scene_c.h"
#include "senc3d.h"

#include <rsys/mem_allocator.h>
#include <rsys/hash_table.h>
#include <rsys/double3.h>

struct senc3d_scene;

static FINLINE void
init_trgside(struct mem_allocator* alloc, struct trgside* data)
{
  int i;
  ASSERT(data); (void)alloc;
  FOR_EACH(i, 0, 3) data->facing_side_id[i] = SIDE_NULL__;
  data->medium = MEDIUM_NULL__;
}

#define DARRAY_NAME side_id
#define DARRAY_DATA side_id_t
#include <rsys/dynamic_array.h>

/* Descriptors for connex component.
 * Define lists of trg sides starting from a given head.
 * Also keeps the maximum z info of the component
 * and the list of media found  */
struct cc_descriptor {
  /* Twice the area of the component */
  double _2area;
  /* Six times the signed volume of the component */
  double _6volume;
  /* The component's bounding Box */
  double bbox[3][2];
  /* Media used by this component */
  uchar* media;
  unsigned media_count;
  /* Does this component is an outer border of an enclosure or an inner one? */
  int is_outer_border;
  vrtx_id_t max_z_vrtx_id; /* id of the vrtx with max z value */
  side_id_t side_count;
  /* Used when grouping components to form enclosures */
  component_id_t cc_id;
  component_id_t cc_group_root;
  enclosure_id_t enclosure_id;
  /* Range of sides member of this component */
  struct side_range side_range;
};
extern const struct cc_descriptor CC_DESCRIPTOR_NULL;

static FINLINE void
cc_descriptor_init
  (struct mem_allocator* alloc,
   struct cc_descriptor* data)
{
  ASSERT(data);
  (void)alloc;
  *data = CC_DESCRIPTOR_NULL;
}

static FINLINE void
ptr_component_descriptor_init
  (struct mem_allocator* alloc,
   struct cc_descriptor** data)
{
  (void)alloc;
  ASSERT(data);
  *data = NULL;
}

#define DARRAY_NAME ptr_component_descriptor
#define DARRAY_DATA struct cc_descriptor*
#define DARRAY_FUNCTOR_INIT ptr_component_descriptor_init
#include <rsys/dynamic_array.h>

/* Need allocator to free array elts: cannot rely on standard
 * darray release stuff */
static FINLINE void
custom_darray_ptr_component_descriptor_release
  (struct darray_ptr_component_descriptor* array)
{
  size_t c, cc_count;
  struct cc_descriptor** components;
  if(!array) return;
  cc_count = darray_ptr_component_descriptor_size_get(array);
  components = darray_ptr_component_descriptor_data_get(array);
  FOR_EACH(c, 0, cc_count) {
    if(!components[c]) continue;
    MEM_RM(array->allocator, components[c]->media);
    MEM_RM(array->allocator, components[c]);
  }
  darray_ptr_component_descriptor_release(array);
}

/* Triangle information.
 * Depending on lifespan, information is kept in different places:
 * - triangle_in for user provided information (kept in scene)
 * - triangle_tmp for tmp information (kept until triangle_comp is ready)
 * - triangle_comp for information describing components (kept until
 *   triangle_enc is ready)
 * - triangle_enc for information describing enclosures (kept in
 *   struct descriptor). */
struct triangle_tmp {
  /* Are the edges of the triangle defined in the same order than
   * the edges they are linked to? */
  uchar reversed_edge[3];
  double max_z;
};

#ifndef NDEBUG
static FINLINE void
triangle_tmp_init(struct mem_allocator* alloc, struct triangle_tmp* trg) {
  int i;
  (void)alloc;
  ASSERT(trg);
  FOR_EACH(i, 0, 3) trg->reversed_edge[i] = UCHAR_MAX;
  trg->max_z = -DBL_MAX;
}
#define DARRAY_FUNCTOR_INIT triangle_tmp_init
#endif

#define DARRAY_NAME triangle_tmp
#define DARRAY_DATA struct triangle_tmp
#include <rsys/dynamic_array.h>

#define HTABLE_NAME edge_id
#define HTABLE_KEY struct trg_edge
#define HTABLE_DATA edge_id_t
#define HTABLE_KEY_FUNCTOR_EQ edge_eq
#include <rsys/hash_table.h>

struct neighbour_info {
  double angle;
  trg_id_t trg_id;
  /* Rank of the edge in the triangle (in [0 2]) */
  uchar common_edge_rank;
  /* Does the geometrical normal point towards the next neighbour
   * (if not, it points towards the previous one)? */
  uchar normal_toward_next_neighbour;
};
#define DARRAY_NAME neighbour
#define DARRAY_DATA struct neighbour_info
#include <rsys/dynamic_array.h>

struct edge_neighbourhood {
  struct trg_edge edge;
  struct darray_neighbour neighbours;
};
static void
neighbourhood_init
  (struct mem_allocator* alloc,
   struct edge_neighbourhood* n)
{
  ASSERT(n);
  darray_neighbour_init(alloc, &n->neighbours);
}
static res_T
neighbourhood_copy
  (struct edge_neighbourhood* dst,
   const struct edge_neighbourhood* src)
{
  ASSERT(src && dst);
  dst->edge = src->edge;
  return darray_neighbour_copy(&dst->neighbours, &src->neighbours);
}
static void
neighbourhood_release(struct edge_neighbourhood* n) {
  ASSERT(n);
  darray_neighbour_release(&n->neighbours);
}
static res_T
neighbourhood_copy_and_release
  (struct edge_neighbourhood* dst,
   struct edge_neighbourhood* src)
{
  ASSERT(src && dst);
  dst->edge = src->edge;
  return darray_neighbour_copy_and_release(&dst->neighbours, &src->neighbours);
}
#define DARRAY_NAME neighbourhood
#define DARRAY_DATA struct edge_neighbourhood
#define DARRAY_FUNCTOR_INIT neighbourhood_init
#define DARRAY_FUNCTOR_COPY neighbourhood_copy
#define DARRAY_FUNCTOR_RELEASE neighbourhood_release
#define DARRAY_FUNCTOR_COPY_AND_RELEASE neighbourhood_copy_and_release
#include <rsys/dynamic_array.h>

extern LOCAL_SYM res_T
scene_analyze(struct senc3d_scene* scene);

#endif /* SENC3D_SCNENE_ANALYZE_C_H */
