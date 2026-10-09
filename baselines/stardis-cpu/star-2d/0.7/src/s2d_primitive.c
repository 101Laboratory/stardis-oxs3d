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
#include "s2d_geometry.h"
#include "s2d_line_segments.h"

#include <rsys/float2.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static int
check_primitive(const struct s2d_primitive* prim)
{
  return prim
      && prim->geom_id != S2D_INVALID_ID
      && prim->prim_id != S2D_INVALID_ID
      && prim->mesh__ != NULL;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s2d_primitive_get_attrib
  (const struct s2d_primitive* prim,
   const enum s2d_attrib_usage usage,
   const float s,
   struct s2d_attrib* attrib)
{
  const uint32_t* ids;
  struct geometry* geom = NULL;
  res_T res = RES_OK;

  if(!check_primitive(prim)
  || !attrib
  || (usage != S2D_GEOMETRY_NORMAL && (unsigned)usage >= S2D_ATTRIBS_COUNT__)) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Unnormalized barycentric coordinates */
  if(s < 0.f || s > 1.f) {
    res = RES_BAD_ARG;
    goto error;
  }

  geom = (struct geometry*)prim->mesh__;

  /* The line segments haven't tge required attrib */
  if(usage != S2D_GEOMETRY_NORMAL && !geom->lines->attribs[usage]) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Out of bound primitive index */
  if(prim->prim_id >= line_segments_get_nsegments(geom->lines)) {
    res = RES_BAD_ARG;
    goto error;
  }

  ids = line_segments_get_ids(geom->lines) + prim->prim_id * 2/*#segment ids*/;
  attrib->usage = usage;

  if(usage == S2D_POSITION || usage == S2D_GEOMETRY_NORMAL) {
    const float* v0;
    const float* v1;
    const float* pos;

    attrib->type = S2D_FLOAT2;
    pos = line_segments_get_pos(geom->lines);
    v0 = pos + ids[0] * 2;
    v1 = pos + ids[1] * 2;

    if(usage == S2D_POSITION) {
      float tmp[2];
      f2_mulf(attrib->value, v1, s);
      f2_add(attrib->value, attrib->value, f2_mulf(tmp, v0, 1.f-s));
    } else { ASSERT(usage == S2D_GEOMETRY_NORMAL);
      const float dx = v1[0] - v0[0];
      const float dy = v1[1] - v0[1];
      /* Build the segment normal with respect to edge orientation.
       * Default is clock wise */
      if(geom->flip_contour) {
        attrib->value[0] = -dy;
        attrib->value[1] = dx;
      } else {
        attrib->value[0] = dy;
        attrib->value[1] = -dx;
      }
    }
  } else {
    FATAL("Unimplemented attribute getter.\n");
  }

exit:
  return res;
error:
  goto exit;
}

res_T
s2d_primitive_sample
  (const struct s2d_primitive* prim,
   const float u,
   float* s)
{
  if(!check_primitive(prim) || !s)
    return RES_BAD_ARG;

  if(u < 0.f || u >= 1.f)
    return RES_BAD_ARG;

  /* Only line segments primitives are currently supported. So the "u"
   * canonical variable is directly the parametric coordinate of the sample
   * onto the segment */
  *s = u;
  return RES_OK;
}

res_T
s2d_primitive_compute_length(const struct s2d_primitive* prim, float* length)
{
  const uint32_t* ids;
  const float* pos;
  const float* v0, *v1;
  float tmp[2];
  struct geometry* geom;

  if(!check_primitive(prim) || !length)
    return RES_BAD_ARG;

  geom = (struct geometry*)prim->mesh__;
  pos = line_segments_get_pos(geom->lines);
  ids = line_segments_get_ids(geom->lines) + prim->prim_id * 2/*#segment ids*/;
  v0 = pos + ids[0] * 2/*#coords*/;
  v1 = pos + ids[1] * 2/*#coords*/;
  *length = f2_len(f2_sub(tmp, v1, v0));
  return RES_OK;
}

res_T
s2d_segment_get_vertex_attrib
  (const struct s2d_primitive* prim,
   const size_t ivertex, /* in [0..2[ */
   const enum s2d_attrib_usage usage,
   struct s2d_attrib* attrib) /* Resulting attrib */
{
  struct geometry* geom;
  const uint32_t* ids;

  if(!check_primitive(prim) || ivertex > 1
  || (unsigned)usage >= S2D_ATTRIBS_COUNT__ || !attrib) {
    return RES_BAD_ARG;
  }

  geom = (struct geometry*)prim->mesh__;
  ASSERT(prim->geom_id == geom->name);

  /* The segment have not the required attrib */
  if(!geom->lines->attribs[usage]) {
    return RES_BAD_ARG;
  }

  /* Out of bound primitive index */
  if(prim->prim_id >= line_segments_get_nsegments(geom->lines)) {
    return RES_BAD_ARG;
  }

  ids = line_segments_get_ids(geom->lines) + prim->prim_id*2/*#segment ids*/;
  attrib->usage = usage;

  if(usage != S2D_POSITION) {
    const float* attr;
    unsigned i, dim;
    attrib->type = geom->lines->attribs_type[usage];
    /* Fetch attrib data */
    dim = s2d_type_get_dimension(attrib->type);
    attr = line_segments_get_attr(geom->lines, usage) + ids[ivertex]*dim;
    FOR_EACH(i, 0, dim) attrib->value[i] = attr[i];
  } else {
    const float* pos;
    attrib->type = S2D_FLOAT2;
    /* Fetch data */
    pos = line_segments_get_pos(geom->lines) + ids[ivertex]*2;
    f2_set(attrib->value, pos);
  }
  return RES_OK;
}

