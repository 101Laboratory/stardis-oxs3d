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

#include "s2d.h"
#include "s2d_c.h"
#include "s2d_device_c.h"
#include "s2d_line_segments.h"
#include "s2d_shape_c.h"

#include<rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
shape_release(ref_T* ref)
{
  struct s2d_shape* shape;
  struct s2d_device* dev;
  ASSERT(ref);
  shape = CONTAINER_OF(ref, struct s2d_shape, ref);
  dev = shape->dev;

  /* The shape should not be attached */
  ASSERT(is_list_empty(&shape->scene_attachment));
  line_segments_ref_put(shape->lines);
  flist_name_del(&dev->names, shape->id);
  MEM_RM(dev->allocator, shape);
  S2D(device_ref_put(dev));
}

/*******************************************************************************
 * Exported s2d_shape functions
 ******************************************************************************/
res_T
s2d_shape_create_line_segments
  (struct s2d_device* dev, struct s2d_shape** out_shape)
{
  struct s2d_shape* shape = NULL;
  res_T res = RES_OK;
  if(!dev || !out_shape) {
    res = RES_BAD_ARG;
    goto error;
  }

  shape = (struct s2d_shape*)
    MEM_CALLOC(dev->allocator, 1, sizeof(struct s2d_shape));
  if(!shape) {
    res = RES_MEM_ERR;
    goto error;
  }
  list_init(&shape->scene_attachment);
  S2D(device_ref_get(dev));
  shape->dev = dev;
  ref_init(&shape->ref);
  shape->id = flist_name_add(&dev->names);
  shape->is_enabled = 1;
  shape->flip_contour = 0;

  res = line_segments_create(dev, &shape->lines);
  if(res != RES_OK) goto error;

exit:
  if(out_shape) *out_shape = shape;
  return res;
error:
  if(shape) {
    S2D(shape_ref_put(shape));
    shape = NULL;
  }
  goto exit;
}

res_T
s2d_shape_ref_get(struct s2d_shape* shape)
{
  if(!shape) return RES_BAD_ARG;
  ref_get(&shape->ref);
  return RES_OK;
}

res_T
s2d_shape_ref_put(struct s2d_shape* shape)
{
  if(!shape) return RES_BAD_ARG;
  ref_put(&shape->ref, shape_release);
  return RES_OK;
}

res_T
s2d_shape_get_id(const struct s2d_shape* shape, unsigned* id)
{
  if(!shape || !id) return RES_BAD_ARG;
  *id = shape->id.index;
  return RES_OK;
}

res_T
s2d_shape_enable(struct s2d_shape* shape, const char enable)
{
  if(!shape) return RES_BAD_ARG;
  shape->is_enabled = enable;
  return RES_OK;
}

res_T
s2d_shape_is_enabled(const struct s2d_shape* shape, char* is_enabled)
{
  if(!shape || !is_enabled) return RES_BAD_ARG;
  *is_enabled = shape->is_enabled;
  return RES_OK;
}

res_T
s2d_shape_is_attached(const struct s2d_shape* shape, char* is_attached)
{
  if(!shape || !is_attached) return RES_BAD_ARG;
  *is_attached = !is_list_empty(&shape->scene_attachment);
  return RES_OK;
}

res_T
s2d_shape_flip_contour(struct s2d_shape* shape)
{
  if(!shape) return RES_BAD_ARG;
  shape->flip_contour ^= 1;
  return RES_OK;
}

res_T
s2d_line_segments_setup_indexed_vertices
  (struct s2d_shape* shape,
   const unsigned nsegments,
   void (*get_indices)
    (const unsigned isegment, unsigned ids[2], void* ctx),
   const unsigned nverts,
   struct s2d_vertex_data attribs[],
   const unsigned nattribs,
   void* data)
{
  if(!shape) return RES_BAD_ARG;
  return line_segments_setup_indexed_vertices
    (shape->lines, nsegments, get_indices, nverts, attribs, nattribs, data);
}

res_T
s2d_line_segments_copy(const struct s2d_shape* src, struct s2d_shape* dst)
{
  if(!src || !dst) return RES_BAD_ARG;
  if(src == dst) return RES_OK;
  dst->flip_contour = src->flip_contour;
  dst->is_enabled = src->is_enabled;
  line_segments_copy_indexed_vertices(src->lines, dst->lines);
  return RES_OK;
}

res_T
s2d_line_segments_get_vertices_count
  (const struct s2d_shape* shape, unsigned* nverts)
{
  if(!shape || !nverts) return RES_BAD_ARG;
  *nverts = (unsigned)line_segments_get_nverts(shape->lines);
  return RES_OK;
}

res_T
s2d_line_segments_get_vertex_attrib
  (const struct s2d_shape* shape,
   const unsigned ivert,
   const enum s2d_attrib_usage usage,
   struct s2d_attrib* attrib)
{
  const float* data;
  unsigned i, dim;

  if(!shape
  || (unsigned)usage >= S2D_ATTRIBS_COUNT__
  || !shape->lines->attribs[usage]
  || !attrib
  || ivert >= (unsigned)line_segments_get_nverts(shape->lines))
    return RES_BAD_ARG;

  attrib->usage = usage;
  attrib->type = shape->lines->attribs_type[usage];

  dim = s2d_type_get_dimension(attrib->type);
  data = line_segments_get_attr(shape->lines, usage) + ivert * dim;
  FOR_EACH(i, 0, dim) attrib->value[i] = data[i];
  return RES_OK;
}

res_T
s2d_line_segments_get_segments_count
  (const struct s2d_shape* shape, unsigned* nsegments)
{
  if(!shape || !nsegments) return RES_BAD_ARG;
  *nsegments = (unsigned)line_segments_get_nsegments(shape->lines);
  return RES_OK;
}

res_T
s2d_line_segments_get_segment_indices
  (const struct s2d_shape* shape,
   const unsigned isegment,
   unsigned ids[2])
{
  const unsigned* data;
  if(!shape
  || !ids
  || isegment >= (unsigned)line_segments_get_nsegments(shape->lines))
    return RES_BAD_ARG;

  data = line_segments_get_ids(shape->lines) + isegment * 2/*#ids per segment*/;
  ids[0] = data[0];
  ids[1] = data[1];
  return RES_OK;
}

res_T
s2d_line_segments_set_hit_filter_function
  (struct s2d_shape* shape,
   s2d_hit_filter_function_T func,
   void* data)
{
  if(!shape) return RES_BAD_ARG;
  shape->lines->filter.func = func;
  shape->lines->filter.data = data;
  return RES_OK;
}

res_T
s2d_line_segments_get_hit_filter_data(struct s2d_shape* shape, void** data)
{
  if(!shape || !data) return RES_BAD_ARG;
  *data = shape->lines->filter.data;
  return RES_OK;
}

