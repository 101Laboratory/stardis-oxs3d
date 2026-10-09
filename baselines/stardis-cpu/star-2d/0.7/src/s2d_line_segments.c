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

#include "s2d_c.h"
#include "s2d_device_c.h"
#include "s2d_line_segments.h"

#include <rsys/float2.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static FINLINE float
line_compute_segment_length
  (struct line_segments* lines,
   const size_t isegment)
{
  const uint32_t* ids;
  const float* pos;
  const float* v0;
  const float* v1;
  const size_t id = isegment*2/*#ids per segment*/;
  float tmp[2];
  ASSERT(lines && isegment < line_segments_get_nsegments(lines));

  ids = line_segments_get_ids(lines);
  pos = line_segments_get_pos(lines);
  v0 = pos + ids[id+0]*2/*#coords*/;
  v1 = pos + ids[id+1]*2/*#coords*/;
  return f2_len(f2_sub(tmp, v1, v0));
}

static void
line_setup_indices
  (struct line_segments* lines,
   const unsigned nsegments,
   void (*get_indices)(const unsigned isegment, unsigned ids[2], void*),
   const unsigned nverts,
   void* data)
{
  uint32_t* indices;
  unsigned isegment;
  unsigned nsegments_prev;
  unsigned nverts_new;
  res_T res = RES_OK;
  ASSERT(lines && nsegments && nverts);

  nsegments_prev = (unsigned)line_segments_get_nsegments(lines);
  ASSERT(get_indices != S2D_KEEP || nsegments == nsegments_prev);

  if(get_indices == S2D_KEEP)
    return;

  if(nsegments == nsegments_prev) {
    lines->update_mask |= (INDEX_BUFFER & !lines->resize_mask);
  } else {
    lines->resize_mask |=  INDEX_BUFFER;
    lines->update_mask &= !INDEX_BUFFER;
  }

  if(lines->indices) { /* Release the old index buffer */
    index_buffer_ref_put(lines->indices);
    lines->indices = NULL;
  }

  /* Allocate the new index buffer */
  res = index_buffer_create(lines->dev->allocator, &lines->indices);
  if(res != RES_OK) FATAL("Insufficient memory\n");
  res = darray_u32_resize(&lines->indices->data, nsegments * 2/*# segmet ids*/);
  if(res != RES_OK) FATAL("Insufficient memory\n");

  /* Setup the lines indices */
  indices = line_segments_get_ids(lines);
  nverts_new = 0;
  FOR_EACH(isegment, 0, nsegments) {
    uint32_t* ids = indices + isegment*2/*#ids per segment*/;
    STATIC_ASSERT(sizeof(unsigned) == sizeof(uint32_t), Unexpected_Type);
    get_indices(isegment, ids, data);
    nverts_new = MMAX(nverts_new, ids[0]);
    nverts_new = MMAX(nverts_new, ids[1]);
  }
  /* Transform nverts from the last vertex id to vertices count */
  if(nverts_new > nverts)
    FATAL("Out of bound indexation\n");
}

static void
line_setup_positions
  (struct line_segments* lines,
   const unsigned nverts,
   struct s2d_vertex_data* attr,
   void* data)
{
  float* positions;
  unsigned ivert, nverts_prev;
  res_T res;
  ASSERT(lines && nverts && attr && attr->usage == S2D_POSITION);

  if(attr->get == S2D_KEEP) {
    ASSERT(lines->attribs[S2D_POSITION]);
    ASSERT(darray_float_size_get(&lines->attribs[S2D_POSITION]->data) == nverts*2);
    return;
  }

  nverts_prev = (unsigned)line_segments_get_nverts(lines);
  if(nverts == nverts_prev) {
    lines->update_mask |= (VERTEX_BUFFER & ~lines->resize_mask);
  } else {
    lines->resize_mask |=  VERTEX_BUFFER;
    lines->update_mask &= ~VERTEX_BUFFER;
  }

  /* Release the old vertex buffer */
  if(lines->attribs[S2D_POSITION]) {
    vertex_buffer_ref_put(lines->attribs[S2D_POSITION]);
    lines->attribs[S2D_POSITION] = NULL;
  }

  /* Allocate the vertex positions */
  res = vertex_buffer_create(lines->dev->allocator, &lines->attribs[S2D_POSITION]);
  if(res != RES_OK) FATAL("Insufficient memory\n");
  res = darray_float_resize(&lines->attribs[S2D_POSITION]->data, nverts*2);
  if(res != RES_OK) FATAL("Insufficient memory\n");
  lines->attribs_type[S2D_POSITION] = S2D_FLOAT2;

  /* Setup the vertex positions */
  positions = darray_float_data_get(&lines->attribs[S2D_POSITION]->data);
  if(attr->type == S2D_FLOAT2) {
    FOR_EACH(ivert, 0, nverts) {
      attr->get(ivert, positions + ivert*2/*# coords per vertex*/, data);
    }
  } else {
    FOR_EACH(ivert, 0, nverts) {
      float pos[3];
      unsigned ipos = ivert * 2/*# coords per vertex*/;
      attr->get(ivert, pos, data);
      switch(attr->type) {
        case S2D_FLOAT:
          positions[ipos + 0] = pos[0];
          positions[ipos + 1] = 0.f;
          break;
        case S2D_FLOAT2:
          positions[ipos + 0] = pos[0];
          positions[ipos + 1] = pos[1];
          break;
        case S2D_FLOAT3:
          positions[ipos + 0] = pos[0] / pos[2];
          positions[ipos + 1] = pos[1] / pos[2];
          break;
        default: FATAL("Unreachable code\n"); break;
      }
    }
  }
}

static void
line_setup_attribs
  (struct line_segments* lines,
   const unsigned nverts,
   const struct s2d_vertex_data* attr,
   void* data)
{
  float* attr_data;
  unsigned dim;
  unsigned ivert;
  res_T res;
  ASSERT(lines && nverts && attr);
  ASSERT(attr->usage!=S2D_POSITION && (unsigned)attr->usage<S2D_ATTRIBS_COUNT__);

  dim = s2d_type_get_dimension(attr->type);
  if(attr->get == S2D_KEEP) {
    ASSERT(lines->attribs_type[attr->usage] == attr->type);
    ASSERT(lines->attribs[attr->usage]);
    ASSERT(darray_float_size_get(&lines->attribs[attr->usage]->data) == nverts*dim);
    return;
  }

  if(lines->attribs[attr->usage]) { /* Release the previous vertex buffer */
    vertex_buffer_ref_put(lines->attribs[attr->usage]);
    lines->attribs[attr->usage] = NULL;
  }

  /* Allocate the new vertex buffer */
  res = vertex_buffer_create(lines->dev->allocator, &lines->attribs[attr->usage]);
  if(res != RES_OK) FATAL("Insufficient memory\n");
  res = darray_float_resize(&lines->attribs[attr->usage]->data, nverts * dim);
  if(res != RES_OK) FATAL("Insufficient memory\n");

  /* Setup the vertex attrib */
  attr_data = darray_float_data_get(&lines->attribs[attr->usage]->data);
  FOR_EACH(ivert, 0, nverts) {
    attr->get(ivert, attr_data, data);
    attr_data += dim;
  }
}

static void
line_release(ref_T* ref)
{
  struct line_segments* lines;
  struct s2d_device* dev;
  ASSERT(ref);

  lines = CONTAINER_OF(ref, struct line_segments, ref);
  line_segments_clear(lines);
  dev = lines->dev;
  darray_float_release(&lines->cdf);
  MEM_RM(dev->allocator, lines);
  S2D(device_ref_put(dev));
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
line_segments_create(struct s2d_device* dev, struct line_segments** out_lines)
{
  struct line_segments* lines = NULL;
  res_T res = RES_OK;
  ASSERT(dev && out_lines);

  lines = (struct line_segments*)MEM_CALLOC
    (dev->allocator, 1, sizeof(struct line_segments));
  if(!lines) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&lines->ref);
  S2D(device_ref_get(dev));
  lines->dev = dev;
  darray_float_init(dev->allocator, &lines->cdf);

exit:
  *out_lines = lines;
  return res;
error:
  if(lines) {
    line_segments_ref_put(lines);
    lines = NULL;
  }
  goto exit;
}

void
line_segments_ref_get(struct line_segments* lines)
{
  ASSERT(lines);
  ref_get(&lines->ref);
}

void
line_segments_ref_put(struct line_segments* lines)
{
  ASSERT(lines);
  ref_put(&lines->ref, line_release);
}

void
line_segments_clear(struct line_segments* lines)
{
  size_t iattr;
  ASSERT(lines);
  if(lines->indices) {
    index_buffer_ref_put(lines->indices);
    lines->indices = NULL;
  }
  FOR_EACH(iattr, 0, S2D_ATTRIBS_COUNT__) {
    if(lines->attribs[iattr]) {
      vertex_buffer_ref_put(lines->attribs[iattr]);
      lines->attribs[iattr] = NULL;
    }
  }
  lines->resize_mask = 0;
  lines->update_mask = 0;
  darray_float_clear(&lines->cdf);
}

size_t
line_segments_get_nsegments(const struct line_segments* lines)
{
  size_t nids;
  ASSERT(lines);
  if(!lines->indices)
    return 0;
  nids = darray_u32_size_get(&lines->indices->data);
  ASSERT(nids % 2 == 0); /* 2 vertices per segment */
  return nids / 2/* #vertices per segement */;
}

size_t
line_segments_get_nverts(const struct line_segments* lines)
{
  size_t ncoords;
  ASSERT(lines);
  if(!lines->attribs[S2D_POSITION])
    return 0;
  ASSERT(lines->attribs_type[S2D_POSITION] == S2D_FLOAT2);
  ncoords = darray_float_size_get(&lines->attribs[S2D_POSITION]->data);
  ASSERT(ncoords % 2 == 0);
  return ncoords / 2/* #coords per vertices */;
}

uint32_t*
line_segments_get_ids(struct line_segments* lines)
{
  ASSERT(lines && lines->indices);
  return darray_u32_data_get(&lines->indices->data);
}

float*
line_segments_get_pos(struct line_segments* lines)
{
  ASSERT(lines && lines->attribs[S2D_POSITION]);
  ASSERT(lines->attribs_type[S2D_POSITION] == S2D_FLOAT2);
  return darray_float_data_get(&lines->attribs[S2D_POSITION]->data);
}

float*
line_segments_get_attr
  (struct line_segments* lines,
   const enum s2d_attrib_usage usage)
{
  ASSERT(lines && usage < S2D_ATTRIBS_COUNT__ && lines->attribs[usage]);
  return darray_float_data_get(&lines->attribs[usage]->data);
}

res_T
line_segments_compute_cdf(struct line_segments* lines)
{
  size_t iseg, nsegs;
  float length = 0.f;
  res_T res = RES_OK;
  ASSERT(lines);

  darray_float_clear(&lines->cdf);

  nsegs = line_segments_get_nsegments(lines);
  if(!nsegs) goto exit;

  res = darray_float_resize(&lines->cdf, nsegs);
  if(res != RES_OK) goto error;

  FOR_EACH(iseg, 0, nsegs) {
    length += line_compute_segment_length(lines, iseg);
    darray_float_data_get(&lines->cdf)[iseg] = length;
  }

exit:
  return res;
error:
  darray_float_clear(&lines->cdf);
  goto exit;
}

float
line_segments_compute_length(struct line_segments* lines)
{
  size_t iseg, nsegs;
  float length = 0.f;
  ASSERT(lines);

  nsegs = line_segments_get_nsegments(lines);
  if(!nsegs) return 0.f;

  FOR_EACH(iseg, 0, nsegs)
    length += line_compute_segment_length(lines, iseg);

  return length;
}

float
line_segments_compute_area
  (struct line_segments* lines,
   const char flip_contour)
{
  const uint32_t* ids;
  const float* pos;
  size_t iseg, nsegs;
  double area2 = 0.f;
  ASSERT(lines);

  nsegs = line_segments_get_nsegments(lines);
  if(!nsegs) return 0.f;

  ids = line_segments_get_ids(lines);
  pos = line_segments_get_pos(lines);

  /* Build a triangle whose base is the contour segment and its appex is the
   * origin. Then compute the area of the triangle and add or sub it from the
   * overall area whether the normal point toward or backward the appex */
  FOR_EACH(iseg, 0, nsegs) {
    const size_t id = iseg * 2/*#ids per segment*/;
    const float* v0 = pos + ids[id+0]*2/*#coords*/;
    const float* v1 = pos + ids[id+1]*2/*#coords*/;
    double tmp;
    float dx, dy, N[2], C;

    dx = v1[0] - v0[0];
    dy = v1[1] - v0[1];

    if(flip_contour) {
      N[0] = -dy;
      N[1] = dx;
    } else {
      N[0] = dy;
      N[1] = -dx;
    }
    C = -f2_dot(N, v0); /* N.v0 + C = 0 */

    tmp = v0[0]*v1[1] - v0[1]*v1[0]; /* Cross product */
    tmp = fabs(tmp); /* 2 * area of the triangle */
    area2 += C > 0 ? tmp : -tmp;
  }
  return (float)(area2 * 0.5);
}

res_T
line_segments_setup_indexed_vertices
  (struct line_segments* lines,
   const unsigned nsegments,
   void (*get_indices)(const unsigned isegment, unsigned ids[2], void* ctx),
   const unsigned nverts,
   struct s2d_vertex_data attribs[],
   const unsigned nattribs,
   void* data)
{
  unsigned iattr = 0;
  int has_position = 0;
  res_T res = RES_OK;
  ASSERT(lines);

  if(!nsegments || !nverts || !attribs || !nattribs) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check indices description */
  if(get_indices == S2D_KEEP) {
    if(!lines->indices) { /* No indice was previously set */
      res = RES_BAD_ARG;
      goto error;
    } else {
      const size_t nsegments_prev = line_segments_get_nsegments(lines);
      if(nsegments_prev != nsegments) { /* Inconsistant data */
        res = RES_BAD_ARG;
        goto error;
      }
    }
  }

  /* Check the vertex data description */
  iattr = 0;
  has_position = 0;
  FOR_EACH(iattr, 0, nattribs) {
    if((unsigned)attribs[iattr].usage >= S2D_ATTRIBS_COUNT__) {
      res = RES_BAD_ARG;
      goto error;
    }
    if(attribs[iattr].get == S2D_KEEP) {
      const enum s2d_attrib_usage attr_usage = attribs[iattr].usage;
      const enum s2d_type type = attribs[iattr].type;
      if(!lines->attribs[attr_usage]) { /* The vertex attrib was not set */
        res = RES_BAD_ARG;
        goto error;
      } else {
        const enum s2d_type type_prev = lines->attribs_type[attr_usage];
        const struct darray_float* attr = &lines->attribs[attr_usage]->data;
        size_t nverts_prev = darray_float_size_get(attr);
        nverts_prev /= s2d_type_get_dimension(type_prev);
        if(type_prev != type || nverts_prev != nverts) { /* Inconsistant data */
          res = RES_BAD_ARG;
          goto error;
        }
      }
    }
    if(attribs[iattr].usage == S2D_POSITION)
      has_position = 1;
  }

  if(!has_position) { /* The vertex must have a position */
    res = RES_BAD_ARG;
    goto error;
  }

  line_setup_indices(lines, nsegments, get_indices, nverts, data);

  /* Setup vertex data */
  FOR_EACH(iattr, 0, nattribs) {
    if(attribs[iattr].usage == S2D_POSITION) {
      line_setup_positions(lines, nverts, attribs + iattr, data);
    } else {
      line_setup_attribs(lines, nverts, attribs + iattr, data);
    }
  }

exit:
  return res;
error:
  goto exit;
}

void
line_segments_compute_aabb
  (struct line_segments* lines,
   float lower[2],
   float upper[2])
{
  float* pos;
  size_t ivert, nverts;
  ASSERT(lines && lower && upper);

  f2_splat(lower, FLT_MAX);
  f2_splat(upper,-FLT_MAX);

  nverts = line_segments_get_nverts(lines);
  if(!nverts) return;

  pos = line_segments_get_pos(lines);
  FOR_EACH(ivert, 0, nverts) {
    const size_t ipos = ivert * 2/*#coords per vertex*/;
    f2_min(lower, lower, pos + ipos);
    f2_max(upper, upper, pos + ipos);
  }
}

void
line_segments_copy_indexed_vertices
  (const struct line_segments* src,
   struct line_segments* dst)
{
  size_t nsegments_src;
  size_t nsegments_dst;
  size_t nverts_src;
  size_t nverts_dst;
  int i;
  ASSERT(src && dst);

  nsegments_src = line_segments_get_nsegments(src);
  nsegments_dst = line_segments_get_nsegments(dst);
  nverts_src = line_segments_get_nsegments(src);
  nverts_dst = line_segments_get_nsegments(dst);

  /* Setup indexe buffer masks */
  if(nsegments_src == nsegments_dst) {
    dst->update_mask = (INDEX_BUFFER & !dst->resize_mask);
  } else {
    dst->resize_mask |=  INDEX_BUFFER;
    dst->update_mask &= !INDEX_BUFFER;
  }

  /* Release the previous index buffer of dst */
  if(dst->indices) {
    index_buffer_ref_put(dst->indices);
    dst->indices = NULL;
  }
  /* Get a reference onto the index buffer of src */
  if(src->indices) {
    index_buffer_ref_get(src->indices);
    dst->indices = src->indices;
  }

  /* Setup the vertex buffer masks */
  if(nverts_src == nverts_dst) {
    dst->update_mask = (VERTEX_BUFFER & ~dst->resize_mask);
  } else {
    dst->resize_mask |=  VERTEX_BUFFER;
    dst->update_mask &= ~VERTEX_BUFFER;
  }

  FOR_EACH(i, 0, S2D_ATTRIBS_COUNT__) {
    /* Release the previous vertex buffers of dst */
    if(dst->attribs[i]) {
      vertex_buffer_ref_put(dst->attribs[i]);
      dst->attribs[i] = NULL;
    }
    /* Get a reference onto the vertex buffers of src */
    if(src->attribs[i]) {
      vertex_buffer_ref_get(src->attribs[i]);
      dst->attribs[i] = src->attribs[i];
      dst->attribs_type[i] = src->attribs_type[i];
    }
  }
}

