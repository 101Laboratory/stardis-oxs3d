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
#include "s2d_device_c.h"
#include "s2d_geometry.h"
#include "s2d_line_segments.h"
#include "s2d_scene_view_c.h"

#include <rsys/float2.h>
#include <rsys/double2.h>

struct point_query_context {
  struct RTCPointQueryContext rtc;
  struct s2d_scene_view* scnview;
  float radius; /* Submitted radius */
  void* data; /* Per point query defined data */
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE float*
closest_point_segment
  (const float p_f[2], /* Position */
   const float v0_f[2], /* 1st segment vertex */
   const float v1_f[2], /* 2nd segment vertex */
   float closest_pt_f[2], /* Closest position */
   float* s_f) /* Parametric coordinate of the closest point onto [v0, v1] */
{
  double v[2]; /* Vector from v0 to p */
  double E[2]; /* v0 -> v1 vector */
  double p[2], v0[2], v1[2], closest_pt[2];
  double segment_len2; /* Square length of the [v0, v1] */
  double dst_x_seglen; /* |p' v0| x |v0 v1|
                       * p' is the orthogonal projection of p onto [v0, v1] */
  double s;
  ASSERT(p_f && v0_f && v1_f);

  d2_set_f2(v0, v0_f);
  d2_set_f2(v1, v1_f);
  d2_set_f2(p, p_f);
  d2_sub(E, v1, v0);

  /* Orthogonally project the point onto the segment */
  d2_sub(v, p, v0);
  dst_x_seglen = d2_dot(v, E);

  /* Check if the closest point is the segment vertex 'v0' */
  if(dst_x_seglen <= 0) {
    *s_f = 0;
    return f2_set(closest_pt_f, v0_f);
  }
  /* Check if the closest point is the segment vertex 'v1' */
  segment_len2 = d2_dot(E, E);
  if(dst_x_seglen >= segment_len2) {
    *s_f = 1;
    return f2_set(closest_pt_f, v1_f);
  }

  /* The closest point is on the segment */
  s = dst_x_seglen / segment_len2;
  d2_add(closest_pt, d2_muld(closest_pt, E, s), v0);
  *s_f = (float)s;
  ASSERT(*s_f == CLAMP(*s_f, 0, 1));
  return f2_set_d2(closest_pt_f, closest_pt);
}

static bool
closest_point_line_segments
  (struct RTCPointQueryFunctionArguments* args,
   struct geometry* geom,
   const float radius,
   void* query_data)
{
  struct s2d_hit hit = S2D_HIT_NULL;
  struct s2d_hit* out_hit = NULL;
  struct hit_filter* filter = NULL;
  const uint32_t* ids = NULL;
  float v0[2], v1[2]; /* Segment vertices */
  float N[2]; /* Segment normal */
  float query_pos[2]; /* Submitted position */
  float range[2];
  float closest_point[2]; /* Computed closest point */
  float vec[2]; /* Vector from query pos to the closest point */
  float dst; /* Distance to the closest point */
  float s; /* Parametric coordinate of the closest point */
  ASSERT(args && geom);
  ASSERT(args->primID < line_segments_get_nsegments(geom->lines));
  ASSERT(radius >=0);

  /* Fetch the line segments indices */
  ids = line_segments_get_ids(geom->lines) + args->primID*2/*#indices per segment*/;

  /* Fetch segments vertices */
  ASSERT(geom->lines->attribs_type[S2D_POSITION] == S2D_FLOAT2);
  f2_set(v0, line_segments_get_pos(geom->lines)+ids[0]*2/*#coords per vertex */);
  f2_set(v1, line_segments_get_pos(geom->lines)+ids[1]*2/*#coords per vertex */);

  query_pos[0] = args->query->x;
  query_pos[1] = args->query->y;

  /* Compute the closest point on the segment from the submitted query_pos */
  closest_point_segment(query_pos, v0, v1, closest_point, &s);

  f2_sub(vec, closest_point, query_pos);
  dst = f2_len(vec);
  if(dst >= args->query->radius) return 0;

  /* Compute the segment normal (left hand convention) */
  N[0] = v1[1] - v0[1];
  N[1] = v0[0] - v1[0];

  /* Flip the geometry normal wrt the flip line_segments flag */
  if(geom->flip_contour) f2_minus(N, N);

  /* Setup the hit */
  hit.prim.mesh__ = geom;
  hit.prim.prim_id = args->primID;
  hit.prim.geom_id = geom->name;
  hit.prim.scene_prim_id = hit.prim.prim_id + geom->scene_prim_id_offset;
  hit.normal[0] = N[0];
  hit.normal[1] = N[1];
  hit.u = s;
  hit.distance = dst;

  range[0] = 0;
  range[1] = radius;

  /* `vec' is the direction along which the closest point was found. Submit it
   * to the filter function as the direction of the computed hit. */
  filter = &geom->lines->filter;
  if(filter->func
  && filter->func(&hit, query_pos, vec, range, query_data, filter->data)) {
    return 0; /* This point is filtered. Discard it! */
  }

  /* Update output data */
  out_hit = args->userPtr;
  *out_hit = hit;
  args->query->radius = dst; /* Shrink the query radius */

  return 1; /* Notify that the query radius was updated */
}

static bool
closest_point(struct RTCPointQueryFunctionArguments* args)
{
  struct point_query_context* ctx = NULL;
  struct geometry* geom = NULL;
  ASSERT(args);

  ctx = CONTAINER_OF(args->context, struct point_query_context, rtc);

  /* Instance are not supported by Star-2D */
  ASSERT(args->context->instStackSize == 0);

  geom = scene_view_geometry_from_embree_id(ctx->scnview, args->geomID);
  return closest_point_line_segments(args, geom, ctx->radius, ctx->data);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s2d_scene_view_closest_point
  (struct s2d_scene_view* scnview,
   const float pos[2],
   const float radius,
   void* query_data,
   struct s2d_hit* hit)
{
  struct RTCPointQuery query;
  struct point_query_context query_ctx;

  if(!scnview || !pos || radius <= 0 || !hit)
    return RES_BAD_ARG;

  if((scnview->mask & S2D_TRACE) == 0) {
    log_error(scnview->scn->dev,
      "%s: the S2D_TRACE flag is not active onto the submitted scene view.\n",
      FUNC_NAME);
    return RES_BAD_OP;
  }

  *hit = S2D_HIT_NULL;

  /* Initialise the point query */
  query.x = pos[0];
  query.y = pos[1];
  query.z = 0;
  query.radius = radius;
  query.time = FLT_MAX; /* Invalid fields */

  /* Initialise the point query context */
  rtcInitPointQueryContext(&query_ctx.rtc);
  query_ctx.scnview = scnview;
  query_ctx.radius = radius;
  query_ctx.data = query_data;

  /* Here we go! */
  rtcPointQuery(scnview->rtc_scn, &query, &query_ctx.rtc, closest_point, hit);
  return RES_OK;
}
