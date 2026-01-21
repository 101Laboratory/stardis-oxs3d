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

#ifndef SENC2D_SCNENE_ANALYZE_C_H
#define SENC2D_SCNENE_ANALYZE_C_H

#include "senc2d_internal_types.h"
#include "senc2d.h"

#include<rsys/mem_allocator.h>
#include <rsys/hash_table.h>
#include <rsys/double2.h>

struct senc2d_scene;

static FINLINE void
init_segside(struct mem_allocator* alloc, struct segside* data)
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
 * Define lists of seg sides starting from a given head.
 * Also keeps the maximum z info of the component
 * and the list of media found  */
struct cc_descriptor {
  /* The area of the component */
  double area;
  /* Twice the signed volume of the component */
  double _2volume;
  /* Does this component is an outer border of an enclosure or an inner one? */
  int is_outer_border;
  vrtx_id_t max_y_vrtx_id; /* id of the vrtx with max z value */
  side_id_t side_count;
  /* Used when grouping components to form enclosures */
  component_id_t cc_id;
  component_id_t cc_group_root;
  enclosure_id_t enclosure_id;
  /* Range of sides member of this component */
  struct side_range side_range;
  /* Media used by this component */
  uchar* media;
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

/* Segment information.
 * Depending on lifespan, information is kept in different places:
 * - segment_in for user provided information (kept in scene)
 * - segment_tmp for tmp information (kept until segment_comp is ready)
 * - segment_comp for information describing components (kept until
 *   segment_enc is ready)
 * - segment_enc for information describing enclosures (kept in
 *   struct descriptor). */
struct segment_tmp {
  double max_y;
};

#ifndef NDEBUG
static FINLINE void
segment_tmp_init(struct mem_allocator* alloc, struct segment_tmp* seg) {
  (void)alloc;
  ASSERT(seg);
  seg->max_y = -DBL_MAX;
}
#define DARRAY_FUNCTOR_INIT segment_tmp_init
#endif

#define DARRAY_NAME segment_tmp
#define DARRAY_DATA struct segment_tmp
#include <rsys/dynamic_array.h>

struct neighbour_info {
  double angle;
  seg_id_t seg_id;
  /* Rank of the vertex in the segment (in [0 1]) */
  uchar common_vertex_rank;
  /* Does the geometrical normal point towards the next neighbour
   * (if not, it points towards the previous one)? */
  uchar normal_toward_next_neighbour;
};
#define DARRAY_NAME neighbour
#define DARRAY_DATA struct neighbour_info
#include <rsys/dynamic_array.h>

#define DARRAY_NAME neighbourhood
#define DARRAY_DATA struct darray_neighbour
#define DARRAY_FUNCTOR_INIT darray_neighbour_init
#define DARRAY_FUNCTOR_COPY darray_neighbour_copy
#define DARRAY_FUNCTOR_RELEASE darray_neighbour_release
#define DARRAY_FUNCTOR_COPY_AND_RELEASE darray_neighbour_copy_and_release
#include <rsys/dynamic_array.h>

extern LOCAL_SYM res_T
scene_analyze(struct senc2d_scene* scene);

#endif /* SENC2D_SCNENE_ANALYZE_C_H */
