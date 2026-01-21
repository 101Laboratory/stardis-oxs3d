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

#define _POSIX_C_SOURCE 200112L /* nextafterf, exp2f, fabsf */

#include "s2d.h"
#include "test_s2d_utils.h"

#include <rsys/float2.h>

#define ON_VERTEX_EPSILON 1.e-4f
#define POSITION_EPSILON 1.e-3f

struct closest_pt {
  float pos[2];
  float normal[2];
  float dst;
  unsigned iprim;
  unsigned igeom;
};

#define CLOSEST_PT_NULL__ {                                                    \
  {0,0},                                                                       \
  {0,0},                                                                       \
  FLT_MAX,                                                                     \
  S2D_INVALID_ID,                                                              \
  S2D_INVALID_ID,                                                              \
}

static const struct closest_pt CLOSEST_PT_NULL = CLOSEST_PT_NULL__;

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
/* Function that computes the point onto the segment that ensures the minimum
 * distance between the submitted `pos' and the segment. Use this routine to
 * cross check the result of the s2d_scene_view_closest_point function */
static float*
closest_point_segment
  (const float p[2], /* Input pos */
   const float a[2], /* 1st segment vertex */
   const float b[2], /* 2nd segment vertex */
   float pt[2]) /* Closest point */
{
  float v[2];
  float E[2];
  float dst;
  float len;

  f2_sub(v, p, a);
  f2_sub(E, b, a);
  len = f2_len(E);
  dst = f2_dot(v, E) / len;

  if(dst <= 0) return f2_set(pt, a);
  if(dst >= len) return f2_set(pt, b);

  f2_normalize(E, E);
  f2_mulf(pt, E, dst);
  f2_add(pt, pt, a);
  return pt;
}

static void
closest_point_line_segments
  (const float pos[2],
   const float* verts,
   const unsigned* ids,
   const unsigned nsegs,
   const unsigned geom_id,
   const unsigned prim_to_filter,
   struct closest_pt* pt)
{
  unsigned iseg;
  CHK(pos && verts && ids && pt);

  *pt = CLOSEST_PT_NULL;
  pt->igeom = geom_id;
  pt->dst = FLT_MAX;

  /* Find the closest point on the mesh */
  FOR_EACH(iseg, 0, nsegs) {
    float v0[2];
    float v1[2];
    float closest_pt[2];
    float vec[2];
    float dst;

    if(iseg == prim_to_filter) continue;

    f2_set(v0, verts+ids[iseg*2+0]*2);
    f2_set(v1, verts+ids[iseg*2+1]*2);

    closest_point_segment(pos, v0, v1, closest_pt);
    dst = f2_len(f2_sub(vec, closest_pt, pos));

    if(dst < pt->dst) {
      f2_set(pt->pos, closest_pt);
      pt->dst = dst;
      pt->iprim = iseg;
      pt->normal[0] = v1[1] - v0[1];
      pt->normal[1] = -v1[0] + v0[0];
      f2_normalize(pt->normal, pt->normal);
    }
  }
}

static INLINE int
hit_on_vertex(const struct s2d_hit* hit)
{
  struct s2d_attrib v0, v1, pos;
  float E[2];
  float hit_pos[2];
  float segment_len;
  float hit_len0;
  float hit_len1;
  ASSERT(hit && !S2D_HIT_NONE(hit));

  /* Rertieve the segment vertices */
  S2D(segment_get_vertex_attrib(&hit->prim, 0, S2D_POSITION, &v0));
  S2D(segment_get_vertex_attrib(&hit->prim, 1, S2D_POSITION, &v1));

  /* Compute the length of the segment */
  segment_len = f2_len(f2_sub(E, v1.value, v0.value));

  /* Compute the hit position */
  CHK(s2d_primitive_get_attrib(&hit->prim, S2D_POSITION, hit->u, &pos) == RES_OK);
  f2_set(hit_pos, pos.value);

  /* Compute the length from hit position to segment vertices */
  hit_len0 = f2_len(f2_sub(E, v0.value, hit_pos));
  hit_len1 = f2_len(f2_sub(E, v1.value, hit_pos));

  if(hit_len0 / segment_len < ON_VERTEX_EPSILON
  || hit_len1 / segment_len < ON_VERTEX_EPSILON)
    return 1;
  return 0;
}

static void
check_closest_point
  (const struct s2d_hit* hit,
   const struct closest_pt* pt)
{
  struct s2d_attrib attr;
  float N[2];

  CHK(hit && pt);
  CHK(!S2D_HIT_NONE(hit));

  CHK(s2d_primitive_get_attrib
    (&hit->prim, S2D_POSITION, hit->u, &attr) == RES_OK);
  f2_normalize(N, hit->normal);

  if(!hit_on_vertex(hit)) {
    CHK(hit->prim.prim_id == pt->iprim);
  }

  if(hit->prim.prim_id == pt->iprim
  && hit->prim.geom_id == pt->igeom) {
    /* Due to numerical inaccuracies and/or the arbitrary order in which
     * primitives are treated, 2 points on different primitive can have the
     * same distance from the query position while their respective
     * coordinates are not equal wrt POSITION_EPSILON. To avoid wrong
     * assertion, we thus check the position returned by Star-2D against the
     * manually computed position only if these positions lies on the same
     * primitive */
    CHK(f2_eq_eps(pt->pos, attr.value, POSITION_EPSILON));
    CHK(f2_eq_eps(pt->normal, N, 1.e-4f));
  }
  CHK(eq_epsf(hit->distance, pt->dst, 1.e-3f));
}

/*******************************************************************************
 * Square test
 ******************************************************************************/
struct square_filter_context {
  float query_pos[2];
  float query_radius;
  unsigned prim_to_filter;
};

static int
square_filter
  (const struct s2d_hit* hit,
   const float org[2],
   const float dir[2],
   const float range[2],
   void* query_data,
   void* filter_data)
{
  struct square_filter_context* ctx = query_data;
  struct s2d_attrib attr;
  float pos[3];
  float vec[3];

  CHK(hit && org && dir && range && !S2D_HIT_NONE(hit));
  CHK((intptr_t)filter_data == (intptr_t)0xDECAFBAD);
  CHK(f2_normalize(vec, dir) != 0);

  f2_add(pos, org, f2_mulf(pos, vec, hit->distance));
  CHK(s2d_primitive_get_attrib
    (&hit->prim, S2D_POSITION, hit->u, &attr) == RES_OK);
  CHK(f2_eq_eps(attr.value, pos, POSITION_EPSILON));

  if(!query_data) return 0;

  CHK(f2_eq_eps(ctx->query_pos, org, POSITION_EPSILON));
  CHK(range[0] == 0);
  CHK(range[1] == ctx->query_radius);

  return ctx->prim_to_filter == hit->prim.prim_id;
}

static void
check_closest_point_square
  (const float pos[2],
   unsigned igeom,
   unsigned prim_to_filter,
   struct s2d_hit* hit)
{
  struct closest_pt pt = CLOSEST_PT_NULL;
  closest_point_line_segments
    (pos, square_verts, square_ids, square_nsegs, igeom, prim_to_filter, &pt);
  check_closest_point(hit, &pt);
}

static void
test_square(struct s2d_device* dev)
{
  struct square_filter_context filter_ctx;
  struct s2d_vertex_data vdata = S2D_VERTEX_DATA_NULL;
  struct line_segments_desc desc;
  struct s2d_hit hit = S2D_HIT_NULL;
  struct s2d_scene* scn = NULL;
  struct s2d_scene_view* view = NULL;
  struct s2d_shape* shape = NULL;
  void* ptr = (void*)((intptr_t)0xDECAFBAD);
  float low[2], upp[2], mid[2];
  float pos[2];
  unsigned igeom;
  size_t i;

  /* Create the Star-2D scene */
  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);
  CHK(s2d_shape_get_id(shape, &igeom) == RES_OK);
  CHK(s2d_line_segments_set_hit_filter_function
    (shape, square_filter, ptr) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);

  vdata.usage = S2D_POSITION;
  vdata.type = S2D_FLOAT2;
  vdata.get = line_segments_get_position;

  /* Setup the square */
  desc.vertices = square_verts;
  desc.indices = square_ids;
  CHK(s2d_line_segments_setup_indexed_vertices(shape, square_nsegs,
    line_segments_get_ids, square_nverts, &vdata, 1, &desc) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &view) == RES_OK);
  CHK(s2d_scene_view_get_aabb(view, low, upp) == RES_OK);
  mid[0] = (low[0] + upp[0]) * 0.5f;
  mid[1] = (low[1] + upp[1]) * 0.5f;

  /* Check the closest point query on the square */
  FOR_EACH(i, 0, 10000) {
    /* Randomly generate a point in a bounding box that is 2 times the size of
     * the square AABB */
    pos[0] = mid[0] + (rand_canonic() * 2 - 1) * (upp[0] - low[0]);
    pos[1] = mid[1] + (rand_canonic() * 2 - 1) * (upp[1] - low[1]);
    CHK(s2d_scene_view_closest_point(view, pos, (float)INF, NULL, &hit) == RES_OK);
    check_closest_point_square(pos, igeom, S2D_INVALID_ID, &hit);
  }

  /* Check closest point query filtering */
  filter_ctx.prim_to_filter = 2;
  FOR_EACH(i, 0, 10000) {
    /* Randomly generate a point in a bounding box that is 2 times the size of
     * the square AABB */
    pos[0] = mid[0] + (rand_canonic() * 2 - 1) * (upp[0] - low[0]);
    pos[1] = mid[1] + (rand_canonic() * 2 - 1) * (upp[1] - low[1]);

    f2_set(filter_ctx.query_pos, pos);
    filter_ctx.query_radius = (float)INF;

    CHK(s2d_scene_view_closest_point
      (view, pos, (float)INF, &filter_ctx, &hit) == RES_OK);
    check_closest_point_square(pos, igeom, filter_ctx.prim_to_filter, &hit);
  }

  /* Clean up */
  CHK(s2d_shape_ref_put(shape) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);
  CHK(s2d_scene_view_ref_put(view) == RES_OK);
}

/*******************************************************************************
 * Single segment test
 ******************************************************************************/
static void
test_single_segment(struct s2d_device* dev)
{
  struct s2d_vertex_data vdata = S2D_VERTEX_DATA_NULL;
  struct line_segments_desc desc;
  struct s2d_hit hit = S2D_HIT_NULL;
  struct s2d_scene* scn = NULL;
  struct s2d_scene_view* view = NULL;
  struct s2d_shape* shape = NULL;
  struct s2d_attrib attr;
  float vertices[4];
  float v0[2], v1[2];
  float low[2], upp[2], mid[2];
  float pos[2];
  float closest_pos[2];
  size_t a, i, j;
  unsigned indices[2] = {0, 1};

  f2(vertices+0, -0.5f, -0.3f);
  f2(vertices+2, -0.4f,  0.2f);

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);

  vdata.usage = S2D_POSITION;
  vdata.type = S2D_FLOAT2;
  vdata.get = line_segments_get_position;
  desc.vertices = vertices;
  desc.indices = indices;
  CHK(s2d_line_segments_setup_indexed_vertices
    (shape, 1, line_segments_get_ids, 2, &vdata, 1, &desc) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &view) == RES_OK);

  line_segments_get_position(0, v0, &desc);
  line_segments_get_position(1, v1, &desc);

  /* Compute the segment AABB */
  low[0] = MMIN(v0[0], v1[0]);
  low[1] = MMIN(v0[1], v1[1]);
  upp[0] = MMAX(v0[0], v1[0]);
  upp[1] = MMAX(v0[1], v1[1]);
  mid[0] = (low[0] + upp[0]) * 0.5f;
  mid[1] = (low[1] + upp[1]) * 0.5f;

  FOR_EACH(i, 0, 10000) {
    /* Randomly generate a point in a bounding box that is 10 times the size of
     * the segment AABB */
    pos[0] = mid[0] + (rand_canonic() * 2 - 1) * (upp[0] - low[0]) * 5.f;
    pos[1] = mid[1] + (rand_canonic() * 2 - 1) * (upp[1] - low[1]) * 5.f;

    CHK(s2d_scene_view_closest_point(view, pos, (float)INF, NULL, &hit) == RES_OK);
    CHK(!S2D_HIT_NONE(&hit));
    CHK(s2d_primitive_get_attrib(&hit.prim, S2D_POSITION, hit.u, &attr) == RES_OK);

    /* Cross check the closest point query result */
    closest_point_segment(pos, v0, v1, closest_pos);
    CHK(f2_eq_eps(closest_pos, attr.value, 1.e-4f));
  }

  FOR_EACH(i, 0, 10000) {
    float radius;

    /* Randomly generate a point in a bounding box that is 10 times the size of
     * the segment AABB */
    pos[0] = mid[0] + (rand_canonic() * 2 - 1) * (upp[0] - low[0]) * 5.f;
    pos[1] = mid[1] + (rand_canonic() * 2 - 1) * (upp[1] - low[1]) * 5.f;

    CHK(s2d_scene_view_closest_point(view, pos, (float)INF, NULL, &hit)
      == RES_OK);
    CHK(!S2D_HIT_NONE(&hit));

    /* Check that the radius is an exclusive upper bound */
    radius = hit.distance;
    CHK(s2d_scene_view_closest_point(view, pos, radius, NULL, &hit) == RES_OK);
    CHK(S2D_HIT_NONE(&hit));
    radius = nextafterf(radius, FLT_MAX);
    CHK(s2d_scene_view_closest_point(view, pos, radius, NULL, &hit) == RES_OK);
    CHK(!S2D_HIT_NONE(&hit));
    CHK(hit.distance == nextafterf(radius, 0.f));
  }

  CHK(s2d_scene_view_ref_put(view) == RES_OK);
  CHK(s2d_shape_ref_put(shape) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);

  /* Check accuracy on a configuration whose analytic distance is known */
  FOR_EACH(a, 0, 19) {
    const float amplitude = exp2f((float)a);
    const float eps = 1e-6f * amplitude;
    FOR_EACH(i, 0, 1000) {
      float A[2], B[2], AB[2], N[2], hit_N[2];
      int n;

      /* Randomly generate a segment AB */
      FOR_EACH(n, 0, 2) 
        A[n] = (rand_canonic() - 0.5f) * amplitude;  
      do {
        FOR_EACH(n, 0, 2) B[n] = (rand_canonic() - 0.5f) * amplitude;
      } while (f2_eq_eps(A, B, eps));
      
      f2_sub(AB, B, A);
      f2(N, AB[1], -AB[0]); /* Left hand convention */
      f2_normalize(N, N);

      f2_set(vertices + 0, A);
      f2_set(vertices + 2, B);

      CHK(s2d_scene_create(dev, &scn) == RES_OK);
      CHK(s2d_shape_create_line_segments(dev, &shape) == RES_OK);
      CHK(s2d_scene_attach_shape(scn, shape) == RES_OK);

      vdata.usage = S2D_POSITION;
      vdata.type = S2D_FLOAT2;
      vdata.get = line_segments_get_position;
      desc.vertices = vertices;
      desc.indices = indices;
      CHK(s2d_line_segments_setup_indexed_vertices
        (shape, 1, line_segments_get_ids, 2, &vdata, 1, &desc) == RES_OK);

      CHK(s2d_scene_view_create(scn, S2D_TRACE, &view) == RES_OK);

      FOR_EACH(j, 0, 1000) {
        float proj[2]; /* Projection of pos on the line */
        float u, h, tmp[2];

        /* Randomly generate a pos not on the segment
         * with know position wrt the problem: pos = A + u.AB + k.N */
        u = 3 * rand_canonic() - 1;
        h = (2 * rand_canonic() - 1) * amplitude;
        f2_add(proj, A, f2_mulf(proj, AB, u));
        f2_add(pos, proj, f2_mulf(pos, N, h));

        /* Compute closest point */
        CHK(s2d_scene_view_closest_point(view, pos, (float)INF, NULL, &hit)
          == RES_OK);
        CHK(!S2D_HIT_NONE(&hit));
        CHK(s2d_primitive_get_attrib(&hit.prim, S2D_POSITION, hit.u, &attr)
          == RES_OK);

        /* Check result */
        if(u <= 0) {
          const float d = f2_len(f2_sub(tmp, pos, A));
          CHK(f2_eq_eps(attr.value, A, eps));
          CHK(eq_epsf(hit.distance, d, eps));
        }
        else if(u >= 1) {
          const float d = f2_len(f2_sub(tmp, pos, B));
          CHK(f2_eq_eps(attr.value, B, eps));
          CHK(eq_epsf(hit.distance, d, eps));
        } else {
          CHK(f2_eq_eps(attr.value, proj, eps));
          CHK(eq_epsf(hit.distance, fabsf(h), eps));
        }
        f2_normalize(hit_N, hit.normal);
        CHK(f2_eq_eps(N, hit_N, FLT_EPSILON));
      }

      CHK(s2d_scene_view_ref_put(view) == RES_OK);
      CHK(s2d_shape_ref_put(shape) == RES_OK);
      CHK(s2d_scene_ref_put(scn) == RES_OK);
    }
  }
}

/*******************************************************************************
 * Miscellaneous test
 ******************************************************************************/
static void
test_api(struct s2d_device* dev)
{
  struct s2d_hit hit = S2D_HIT_NULL;
  struct s2d_scene* scn = NULL;
  struct s2d_scene_view* view = NULL;
  float pos[3] = {0};

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_TRACE, &view) == RES_OK);

  CHK(s2d_scene_view_closest_point(NULL, pos, 1.f, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_closest_point(view, NULL, 1.f, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_closest_point(view, pos, 0.f, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_closest_point(view, pos, 1.f, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_closest_point(view, pos, 1.f, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit));

  CHK(s2d_scene_view_ref_put(view) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_SAMPLE, &view) == RES_OK);
  CHK(s2d_scene_view_closest_point(view, pos, 1.f, NULL, &hit) == RES_BAD_OP);

  CHK(s2d_scene_view_ref_put(view) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);
}

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s2d_device* dev = NULL;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(s2d_device_create(NULL, &allocator, 1, &dev) == RES_OK);

  test_api(dev);
  test_single_segment(dev);
  test_square(dev);

  CHK(s2d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
