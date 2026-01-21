/* Copyright (C) 2014-2017, 2021-2023 Vincent Forest (vaplv@free.fr)
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

#include "polygon.h"
#include "test_polygon_utils.h"

#include <rsys/float3.h>
#include <rsys/mem_allocator.h>

#include <string.h>

static float /* Return the area of the triangulated polygon */
check_triangulation
  (struct polygon* poly,
   const uint32_t* indices,
   const uint32_t nindices,
   const uint32_t nedges_contour)
{
  float area = 0.f;
  uint32_t i;
  /* List the counter edges retrieved after the polygon triangulation. This
   * array must be indexed by the smallest index of the edge */
  char* contour;
  ASSERT(poly && indices && nindices && nedges_contour);

  contour = mem_calloc(nedges_contour, sizeof(char));
  CHK(contour != NULL);

  memset(contour, 0, nedges_contour * sizeof(char));
  CHK(nindices % 3 == 0);
  FOR_EACH(i, 0, nindices/3) {
    const uint32_t* tri = indices + (i*3);
    float e0[3], e1[3], N[3];
    float v0[3], v1[3], v2[3];
    float len;
    CHK(polygon_vertex_get(poly, tri[0], v0) == RES_OK);
    CHK(polygon_vertex_get(poly, tri[1], v1) == RES_OK);
    CHK(polygon_vertex_get(poly, tri[2], v2) == RES_OK);
    /* Compute the triangle area and add it to the overall polygon area */
    f3_sub(e0, v1, v0);
    f3_sub(e1, v2, v0);
    len = f3_normalize(N, f3_cross(N, e0, e1));
    CHK(eq_eps(len, 0.f, 1.e-6f) == 0);
    area += len * 0.5f;
    /* Update the contour edge flag */
    if(tri[1] == (tri[0] + 1) % nedges_contour) {
      CHK(contour[tri[0]] == 0);
      contour[tri[0]] = 1;
    }
    if(tri[2] == (tri[1] + 1) % nedges_contour) {
      CHK(contour[tri[1]] == 0);
      contour[tri[1]] = 1;
    }
    if(tri[0] == (tri[2] + 1) % nedges_contour) {
      CHK(contour[tri[2]] == 0);
      contour[tri[2]] = 1;
    }
  }
  FOR_EACH(i, 0, nedges_contour) /* All the contour edges may be found */
    CHK(contour[i] == 1);

  mem_rm(contour);
  return area;
}

int
main(int argc, char** argv)
{
  /* Input polygon contour :
   *   1-------------2     5---6
   *   |             |     |   |
   *   |   10----9   |     |   |
   *   |   |     |   3-----4   |
   *   |   |     |             |
   *   0---11    8-------------7 */
  const float vertices[] = {
    0.f, 0.f, 0.f,
    0.f, 1.f, 0.f,
    1.f, 1.f, 0.f,
    1.f, 0.25f, 0.f,
    1.5f, 0.25f, 0.f,
    1.5f, 1.f, 0.f,
    1.75f, 1.f, 0.f,
    1.75f, 0.f, 0.f,
    0.75f, 0.f, 0.f,
    0.75f, 0.75f, 0.f,
    0.25f, 0.75f, 0.f,
    0.25f, 0.f, 0.f
  };
  struct mem_allocator allocator_proxy;
  struct polygon* poly;
  const uint32_t* indices;
  float pos[3];
  uint32_t nindices;
  uint32_t ivertex, nvertices;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator);

  CHK(polygon_create(NULL, NULL) == RES_BAD_ARG);
  CHK(polygon_create(&allocator_proxy, NULL) == RES_BAD_ARG);
  CHK(polygon_create(NULL, &poly) == RES_OK);

  CHK(polygon_ref_get(NULL) == RES_BAD_ARG);
  CHK(polygon_ref_get(poly) == RES_OK);
  CHK(polygon_ref_put(NULL) == RES_BAD_ARG);
  CHK(polygon_ref_put(poly) == RES_OK);
  CHK(polygon_ref_put(poly) == RES_OK);

  CHK(polygon_create(&allocator_proxy, &poly) == RES_OK);

  CHK(polygon_vertices_count_get(NULL, NULL) == RES_BAD_ARG);
  CHK(polygon_vertices_count_get(poly, NULL) == RES_BAD_ARG);
  CHK(polygon_vertices_count_get(NULL, &nvertices) == RES_BAD_ARG);
  CHK(polygon_vertices_count_get(poly, &nvertices) == RES_OK);
  CHK(nvertices == 0);

  CHK(polygon_vertex_add(NULL, NULL) == RES_BAD_ARG);
  CHK(polygon_vertex_add(poly, NULL) == RES_BAD_ARG);
  CHK(polygon_vertex_add(NULL, vertices + 3) == RES_BAD_ARG);
  CHK(polygon_vertex_add(poly, vertices + 3) == RES_OK);

  CHK(polygon_vertices_count_get(poly, &nvertices) == RES_OK);
  CHK(nvertices == 1);

  /* The last vertex is equal to the new one => skip it */
  CHK(polygon_vertex_add(poly, vertices + 3) == RES_OK);
  CHK(polygon_vertices_count_get(poly, &nvertices) == RES_OK);
  CHK(nvertices == 1);

  CHK(polygon_vertex_add(poly, vertices + 6) == RES_OK);
  CHK(polygon_vertices_count_get(poly, &nvertices) == RES_OK);
  CHK(nvertices == 2);

  /* The new vertex is aligned with the 2 previous one => replace the last
   * vertex by the new one */
  CHK(polygon_vertex_add(poly, vertices + 15) == RES_OK);
  CHK(polygon_vertices_count_get(poly, &nvertices) == RES_OK);
  CHK(nvertices == 2);

  CHK(polygon_vertex_get(NULL, UINT32_MAX, NULL) == RES_BAD_ARG);
  CHK(polygon_vertex_get(poly, UINT32_MAX, NULL) == RES_BAD_ARG);
  CHK(polygon_vertex_get(NULL, 0, NULL) == RES_BAD_ARG);
  CHK(polygon_vertex_get(poly, 0, NULL) == RES_BAD_ARG);
  CHK(polygon_vertex_get(NULL, UINT32_MAX, pos) == RES_BAD_ARG);
  CHK(polygon_vertex_get(poly, UINT32_MAX, pos) == RES_BAD_ARG);
  CHK(polygon_vertex_get(NULL, 0, pos) == RES_BAD_ARG);
  CHK(polygon_vertex_get(poly, 0, pos) == RES_OK);
  CHK(f3_eq_eps(pos, vertices + 3, 1.e-6f) == 1);

  CHK(polygon_vertex_get(poly, 1, pos) == RES_OK);
  CHK(f3_eq_eps(pos, vertices + 15, 1.e-6f) == 1);

  CHK(polygon_vertex_get(poly, 2, pos) == RES_BAD_ARG);

  CHK(polygon_clear(NULL) == RES_BAD_ARG);
  CHK(polygon_clear(poly) == RES_OK);

  CHK(polygon_vertices_count_get(poly, &nvertices) == RES_OK);
  CHK(nvertices == 0);

  FOR_EACH(ivertex, 0, sizeof(vertices)/(3*sizeof(float)))
    CHK(polygon_vertex_add(poly, vertices + ivertex * 3) == RES_OK);

  CHK(polygon_vertices_count_get(poly, &nvertices) == RES_OK);
  CHK(nvertices == sizeof(vertices)/(3*sizeof(float)));

  FOR_EACH(ivertex, 0, sizeof(vertices)/(3*sizeof(float))) {
    CHK(polygon_vertex_get(poly, ivertex, pos) == RES_OK);
    CHK(f3_eq_eps(pos, vertices + ivertex*3, 1.e-6f) == 1);
  }

  CHK(polygon_triangulate(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(polygon_triangulate(poly, NULL, NULL) == RES_BAD_ARG);
  CHK(polygon_triangulate(NULL, &indices, NULL) == RES_BAD_ARG);
  CHK(polygon_triangulate(poly, &indices, NULL) == RES_BAD_ARG);
  CHK(polygon_triangulate(NULL, NULL, &nindices) == RES_BAD_ARG);
  CHK(polygon_triangulate(poly, NULL, &nindices) == RES_BAD_ARG);
  CHK(polygon_triangulate(NULL, &indices, &nindices) == RES_BAD_ARG);

  /* Check full triangulation */
  CHK(polygon_triangulate(poly, &indices, &nindices) == RES_OK);
  CHK(nindices == 30);
  CHK(eq_eps(check_triangulation(poly, indices, nindices, 12), 1.f, 1.e-6f));

  /* After the triangulation the input polygon may be unchanged */
  CHK(polygon_vertices_count_get(poly, &nvertices) == RES_OK);
  CHK(nvertices == sizeof(vertices)/(3*sizeof(float)));
  FOR_EACH(ivertex, 0, sizeof(vertices)/(3*sizeof(float))) {
    CHK(polygon_vertex_get(poly, ivertex, pos) == RES_OK);
    CHK(f3_eq_eps(pos, vertices + ivertex*3, 1.e-6f) == 1);
  }

  /* Check that the input polygon can be retriangulated */
  CHK(polygon_triangulate(poly, &indices, &nindices) == RES_OK);
  CHK(nindices == 30);
  CHK(eq_eps(check_triangulation(poly, indices, nindices, 12), 1.f, 1.e-6f));

  /* One can triangulate empty polygon */
  CHK(polygon_clear(poly) == RES_OK);
  CHK(polygon_triangulate(poly, &indices, &nindices) == RES_OK);
  CHK(nindices == 0);

  /* Check the triangulation of an updated polygon */
  CHK(polygon_vertex_add(poly, vertices + 0) == RES_OK);
  CHK(polygon_vertex_add(poly, vertices + 3) == RES_OK);
  CHK(polygon_triangulate(poly, &indices, &nindices) == RES_OK);
  CHK(nindices == 0);
  CHK(polygon_vertex_add(poly, vertices + 6) == RES_OK);
  CHK(polygon_triangulate(poly, &indices, &nindices) == RES_OK);
  CHK(nindices == 3);
  CHK(eq_eps(check_triangulation(poly, indices, nindices, 3), 0.5f, 1.e-6f));
  CHK(polygon_vertex_add(poly, vertices + 9) == RES_OK);
  CHK(polygon_vertex_add(poly, vertices + 12) == RES_OK);
  CHK(polygon_vertex_add(poly, vertices + 15) == RES_OK);
  CHK(polygon_vertex_add(poly, vertices + 18) == RES_OK);
  CHK(polygon_vertex_add(poly, vertices + 21) == RES_OK);
  CHK(polygon_vertex_add(poly, vertices + 21) == RES_OK);
  CHK(polygon_triangulate(poly, &indices, &nindices) == RES_OK);
  CHK(nindices == 18);
  CHK(eq_eps(check_triangulation(poly, indices, nindices, 8), 1.375f, 1.e-6f));

  CHK(polygon_ref_put(poly) == RES_OK);

  check_memory_allocator(&allocator_proxy);
  mem_shutdown_proxy_allocator(&allocator_proxy);
  CHK(mem_allocated_size() == 0);
  return 0;
}

