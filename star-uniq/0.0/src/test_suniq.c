/* Copyright (C) 2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "suniq.h"

#include <rsys/double3.h>
#include <rsys/mem_allocator.h>

/* Box lower limits in X, Y and Z */
#define LX 0.0
#define LY 1.0
#define LZ 2.0

/* Box upper limits in X, Y and Z */
#define UX 1.0
#define UY 5.1
#define UZ 10.314

/* List of box triangles */
static const struct suniq_triangle box[] = {
  {{{UX, UY, LZ}, {UX, LY, LZ}, {LX, LY, LZ}}},
  {{{UX, UY, LZ}, {LX, LY, LZ}, {LX, UY, LZ}}},
  {{{LX, UY, LZ}, {LX, LY, LZ}, {LX, LY, UZ}}},
  {{{LX, UY, LZ}, {LX, LY, UZ}, {LX, UY, UZ}}},
  {{{LX, UY, UZ}, {LX, LY, UZ}, {UX, UY, UZ}}},
  {{{UX, UY, UZ}, {LX, LY, UZ}, {UX, LY, UZ}}},
  {{{UX, LY, UZ}, {UX, LY, LZ}, {UX, UY, LZ}}},
  {{{UX, LY, UZ}, {UX, UY, LZ}, {UX, UY, UZ}}},
  {{{UX, UY, UZ}, {LX, UY, LZ}, {LX, UY, UZ}}},
  {{{UX, UY, UZ}, {UX, UY, LZ}, {LX, UY, LZ}}},
  {{{UX, LY, UZ}, {LX, LY, LZ}, {UX, LY, LZ}}},
  {{{UX, LY, UZ}, {LX, LY, UZ}, {LX, LY, LZ}}}
};
static const size_t box_ntris = sizeof(box)/sizeof(struct suniq_triangle);

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
check_creation(void)
{
  struct mem_allocator allocator;
  struct suniq* suniq = NULL;

  /* Check creation API */
  CHK(suniq_create(NULL, NULL) == RES_BAD_ARG);
  CHK(suniq_create(&allocator, NULL) == RES_BAD_ARG);
  CHK(suniq_create(NULL/*optional*/, &suniq) == RES_OK);

  /* Check ref count API */
  CHK(suniq_ref_get(NULL) == RES_BAD_ARG);
  CHK(suniq_ref_get(suniq) == RES_OK);
  CHK(suniq_ref_put(NULL) == RES_BAD_ARG);
  CHK(suniq_ref_put(suniq) == RES_OK);
  CHK(suniq_ref_put(suniq) == RES_OK);

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);

  /* Check the use of a user-defined allocator */
  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);
  CHK(suniq_create(&allocator, &suniq) == RES_OK);
  CHK(MEM_ALLOCATED_SIZE(&allocator) > 0);
  CHK(suniq_ref_put(suniq) == RES_OK);
  CHK(MEM_ALLOCATED_SIZE(&allocator) == 0);

  mem_shutdown_proxy_allocator(&allocator);
}

static void
check_registration_1_triangle(void)
{
  struct suniq_triangle tri = SUNIQ_TRIANGLE_NULL;
  struct suniq* suniq = NULL;
  size_t id = 0;

  CHK(suniq_create(NULL, &suniq) == RES_OK);

  /* Check triangle registration API */
  CHK(suniq_register_triangle(NULL, &box[0], &id) == RES_BAD_ARG);
  CHK(suniq_register_triangle(suniq, NULL, &id) == RES_BAD_ARG);
  CHK(suniq_register_triangle(suniq, &box[0], NULL/*optional*/) == RES_OK);

  /* Register the same triangle again */
  CHK(suniq_register_triangle(suniq, &box[0], &id) == RES_OK);
  CHK(id == 0);

  /* Turn over the 1st triangle in the box and check that it is still registered
   * as the 1st triangle */
  d3_set(tri.vertices[0], box[0].vertices[1]);
  d3_set(tri.vertices[1], box[0].vertices[0]);
  d3_set(tri.vertices[2], box[0].vertices[2]);
  CHK(suniq_register_triangle(suniq, &tri, &id) == RES_OK);
  CHK(id == 0);

  /* Check triangle accessor API */
  CHK(suniq_get_triangle(NULL, id, &tri) == RES_BAD_ARG);
  CHK(suniq_get_triangle(suniq, 1, &tri) == RES_BAD_ARG);
  CHK(suniq_get_triangle(suniq, id, NULL) == RES_BAD_ARG);
  CHK(suniq_get_triangle(suniq, id, &tri) == RES_OK);

  /* Check that the 1st tri is saved as it was committed for the 1st time */
  CHK(d3_eq(tri.vertices[0], box[0].vertices[0]));
  CHK(d3_eq(tri.vertices[1], box[0].vertices[1]));
  CHK(d3_eq(tri.vertices[2], box[0].vertices[2]));

  /* Check the clear API */
  CHK(suniq_clear(NULL) == RES_BAD_ARG);
  CHK(suniq_clear(suniq) == RES_OK);
  CHK(suniq_get_triangle(suniq, 0, &tri) == RES_BAD_ARG); /* No more triangle */

  /* Turn over the 1st triangle in the box and register it */
  d3_set(tri.vertices[0], box[0].vertices[1]);
  d3_set(tri.vertices[1], box[0].vertices[0]);
  d3_set(tri.vertices[2], box[0].vertices[2]);
  CHK(suniq_register_triangle(suniq, &box[0], &id) == RES_OK);
  CHK(id == 0);

  CHK(suniq_ref_put(suniq) == RES_OK);
}

static void
check_registration_box(void)
{
  struct suniq_triangle tri = SUNIQ_TRIANGLE_NULL;
  struct suniq* suniq = NULL;
  size_t id = 0;
  size_t i = 0;

  CHK(suniq_create(NULL, &suniq) == RES_OK);

  /* Check the registration of the triangles in the box, where every 4th
   * triangle is inverted */
  FOR_EACH(i, 0, box_ntris) {

    if(i%4 != 0) {
      CHK(suniq_register_triangle(suniq, &box[i], &id) == RES_OK);
    } else {
      d3_set(tri.vertices[0], box[i].vertices[2]);
      d3_set(tri.vertices[1], box[i].vertices[1]);
      d3_set(tri.vertices[2], box[i].vertices[0]);
      CHK(suniq_register_triangle(suniq, &tri, &id) == RES_OK);
    }

    CHK(id == i);
  }

  /* Try to register all triangles again */
  FOR_EACH(i, 0, box_ntris) {
    CHK(suniq_register_triangle(suniq, &box[i], &id) == RES_OK);
  }

  /* Check that triangles are saved as they were first registered */
  FOR_EACH(i, 0, box_ntris) {
    CHK(suniq_get_triangle(suniq, i, &tri) == RES_OK);

    if(i%4 != 0) {
      CHK(d3_eq(tri.vertices[0], box[i].vertices[0]));
      CHK(d3_eq(tri.vertices[1], box[i].vertices[1]));
      CHK(d3_eq(tri.vertices[2], box[i].vertices[2]));
    } else {
      /* Every 4th triangle is inverted */
      CHK(d3_eq(tri.vertices[0], box[i].vertices[2]));
      CHK(d3_eq(tri.vertices[1], box[i].vertices[1]));
      CHK(d3_eq(tri.vertices[2], box[i].vertices[0]));
    }
  }

  CHK(suniq_clear(suniq) == RES_OK);

  /* Check that once cleared, everything is reset. So this time, all registered
   * triangles are returned, except all 4 triangles. This is exactly the
   * opposite of the previous situation */
  FOR_EACH(i, 0, box_ntris) {
    if(i%4 == 0) {
      CHK(suniq_register_triangle(suniq, &box[i], &id) == RES_OK);

      CHK(suniq_get_triangle(suniq, i, &tri) == RES_OK);
      CHK(d3_eq(tri.vertices[0], box[i].vertices[0]));
      CHK(d3_eq(tri.vertices[1], box[i].vertices[1]));
      CHK(d3_eq(tri.vertices[2], box[i].vertices[2]));

    } else {
      d3_set(tri.vertices[0], box[i].vertices[2]);
      d3_set(tri.vertices[1], box[i].vertices[1]);
      d3_set(tri.vertices[2], box[i].vertices[0]);
      CHK(suniq_register_triangle(suniq, &tri, &id) == RES_OK);

      CHK(suniq_get_triangle(suniq, i, &tri) == RES_OK);
      CHK(d3_eq(tri.vertices[0], box[i].vertices[2]));
      CHK(d3_eq(tri.vertices[1], box[i].vertices[1]));
      CHK(d3_eq(tri.vertices[2], box[i].vertices[0]));
    }
  }

  CHK(suniq_ref_put(suniq) == RES_OK);
}

static void
check_descriptor(void)
{
  struct suniq_desc desc = SUNIQ_DESC_NULL;
  struct suniq* suniq = NULL;
  size_t idx[3] = {0,0,0};
  double vtx[3] = {0,0,0};
  size_t i = 0;

  CHK(suniq_create(NULL, &suniq) == RES_OK);

  FOR_EACH(i, 0, box_ntris) {
    CHK(suniq_register_triangle(suniq, &box[i], NULL) == RES_OK);
  }

  /* Check descriptor API */
  CHK(suniq_get_desc(NULL, &desc) == RES_BAD_ARG);
  CHK(suniq_get_desc(suniq, NULL) == RES_BAD_ARG);
  CHK(suniq_get_desc(suniq, &desc) == RES_OK);

  /* Check the number of triangles and vertices indicated by the descriptor.
   * Note that the number of vertices cannot be known. It depends on how the
   * indexed mesh is constructed by the library. In any case, it cannot be less
   * than the number of vertices in the cube */
  CHK(desc.ntriangles == box_ntris);
  CHK(desc.nvertices >= 8);

  /* Check vertex API */
  CHK(suniq_desc_get_vertex(NULL, 0, vtx) == RES_BAD_ARG);
  CHK(suniq_desc_get_vertex(&desc, desc.nvertices, vtx) == RES_BAD_ARG);
  CHK(suniq_desc_get_vertex(&desc, 0, NULL) == RES_BAD_ARG);
  CHK(suniq_desc_get_vertex(&desc, 0, vtx) == RES_OK);

  /* Check triange indices API */
  CHK(suniq_desc_get_triangle_indices(NULL, 0, idx) == RES_BAD_ARG);
  CHK(suniq_desc_get_triangle_indices(&desc, desc.ntriangles, idx) == RES_BAD_ARG);
  CHK(suniq_desc_get_triangle_indices(&desc, 0, NULL) == RES_BAD_ARG);
  CHK(suniq_desc_get_triangle_indices(&desc, 0, idx) == RES_OK);

  /* Check descriptor data */
  FOR_EACH(i, 0, desc.ntriangles) {
    const size_t* indices;
    const double *vertex0, *vertex1, *vertex2;

    indices = desc.indices + i*3/*#indices per triangle */;
    vertex0 = desc.positions + indices[0] * 3/*#coords per vertex*/;
    vertex1 = desc.positions + indices[1] * 3/*#coords per vertex*/;
    vertex2 = desc.positions + indices[2] * 3/*#coords per vertex*/;

    CHK(d3_eq(vertex0, box[i].vertices[0]));
    CHK(d3_eq(vertex1, box[i].vertices[1]));
    CHK(d3_eq(vertex2, box[i].vertices[2]));

    CHK(suniq_desc_get_triangle_indices(&desc, i, idx) == RES_OK);
    CHK(idx[0] == indices[0]);
    CHK(idx[1] == indices[1]);
    CHK(idx[2] == indices[2]);

    CHK(suniq_desc_get_vertex(&desc, idx[0], vtx) == RES_OK);
    CHK(d3_eq(vtx, box[i].vertices[0]));
    CHK(suniq_desc_get_vertex(&desc, idx[1], vtx) == RES_OK);
    CHK(d3_eq(vtx, box[i].vertices[1]));
    CHK(suniq_desc_get_vertex(&desc, idx[2], vtx) == RES_OK);
    CHK(d3_eq(vtx, box[i].vertices[2]));
  }

  CHK(suniq_clear(suniq) == RES_OK);

  /* Check the descriptor once the registering is empty */
  CHK(suniq_get_desc(suniq, &desc) == RES_OK);
  CHK(desc.nvertices == 0);
  CHK(desc.ntriangles == 0);

  /* Check that no more triangles/vertexes can be recovered */
  CHK(suniq_desc_get_triangle_indices(&desc, 0, idx) == RES_BAD_ARG);
  CHK(suniq_desc_get_vertex(&desc, 0, vtx) == RES_BAD_ARG);

  CHK(suniq_ref_put(suniq) == RES_OK);
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(void)
{
  check_creation();
  check_registration_1_triangle();
  check_registration_box();
  check_descriptor();

  CHK(mem_allocated_size() == 0);
  return 0;
}
