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
#include <rsys/float3.h>

/*******************************************************************************
 * Single segment test
 ******************************************************************************/
static void
test_single_segment(struct s2d_device* dev)
{
  struct s2d_vertex_data vdata = S2D_VERTEX_DATA_NULL;
  struct line_segments_desc desc;
  struct s2d_hit hit = S2D_HIT_NULL;
  struct s2d_hit hit3 = S2D_HIT_NULL;
  struct s2d_scene* scn = NULL;
  struct s2d_scene_view* view = NULL;
  struct s2d_shape* shape = NULL;
  struct s2d_attrib attr;
  float vertices[4];
  float v0[2], v1[2];
  float pos[2];
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

  CHK(s2d_scene_view_ref_put(view) == RES_OK);
  CHK(s2d_shape_ref_put(shape) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);

  /* Check accuracy on a configuration whose analytic distance is known */
  FOR_EACH(a, 0, 16) {
    const float amplitude = exp2f((float)a);
    const float eps = 1e-6f * amplitude;
    FOR_EACH(i, 0, 1000) {
      float A[2], B[2], AB[2], N[2];
      int n;

      /* Randomly generate a segment AB */
      FOR_EACH(n, 0, 2)
        A[n] = (rand_canonic() - 0.5f) * amplitude;
      do {
        FOR_EACH(n, 0, 2) B[n] = (rand_canonic() - 0.5f) * amplitude;
      } while (f2_eq_eps(A, B, eps));

      f2_sub(AB, B, A);
      f2(N, -AB[1], AB[0]);
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
        float u, h, l;
        float dir[3], range[2] = { 0, FLT_MAX };

        /* Randomly generate a pos not on the segment
         * with know position wrt the problem: pos = A + u.AB + k.N */
        u = 1.2f * rand_canonic() - 0.1f;
        h = (2 * rand_canonic() - 1) * amplitude;
        f2_add(proj, A, f2_mulf(proj, AB, u));
        f2_add(pos, proj, f2_mulf(pos, N, h));

        /* Raytrace from pos towards proj */
        f2_mulf(dir, N, (h > 0 ? -1.f : 1.f));
        CHK(s2d_scene_view_trace_ray(view, pos, dir, range, NULL, &hit)
          == RES_OK);
        dir[2] = rand_canonic() - 0.5f;
        l = f3_normalize(dir, dir);
        CHK(s2d_scene_view_trace_ray_3d(view, pos, dir, range, NULL, &hit3)
          == RES_OK);

        /* Check result */
        if(u < 0 || u > 1) {
          if(!S2D_HIT_NONE(&hit) || !S2D_HIT_NONE(&hit3))
            CHK(u >= -FLT_EPSILON && u <= 1 + FLT_EPSILON);
        } else {
          if(S2D_HIT_NONE(&hit) || S2D_HIT_NONE(&hit3))
            CHK(u <= FLT_EPSILON || u >= 1 - FLT_EPSILON);
        }
        if(!S2D_HIT_NONE(&hit)) {
          CHK(eq_epsf(hit.distance, fabsf(h), eps));
          CHK(s2d_primitive_get_attrib(&hit.prim, S2D_POSITION, hit.u, &attr)
            == RES_OK);
          CHK(f2_eq_eps(attr.value, proj, eps));
        }
        if(!S2D_HIT_NONE(&hit3)) {
          CHK(eq_epsf(hit3.distance, l * fabsf(h), eps));
          CHK(s2d_primitive_get_attrib(&hit3.prim, S2D_POSITION, hit3.u, &attr)
            == RES_OK);
          CHK(f2_eq_eps(attr.value, proj, eps));
        }
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
  float pos[3] = { 0 }, dir[3] = { 1 }, range[2] = { 0, 1 };

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_TRACE, &view) == RES_OK);

  CHK(s2d_scene_view_trace_ray(NULL, pos, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_trace_ray(view, NULL, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_trace_ray(view, pos, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_trace_ray(view, pos, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_trace_ray(view, pos, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_trace_ray(view, pos, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit));

  CHK(s2d_scene_view_trace_ray_3d(NULL, pos, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_trace_ray_3d(view, NULL, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_trace_ray_3d(view, pos, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_trace_ray_3d(view, pos, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(s2d_scene_view_trace_ray_3d(view, pos, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_trace_ray_3d(view, pos, dir, range, NULL, &hit) == RES_OK);
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

  CHK(s2d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
