/* Copyright (C) 2015, 2016, 2019, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#include "sstl.h"

#include <rsys/float3.h>
#include <rsys/mem_allocator.h>

static const float vertices[4/*#vertices*/*3/*#coords*/] = {
  0.0f, 0.0f, 0.0f,
  0.1f, 0.0f, 0.0f,
  0.0f, 0.0f, 0.1f,
  0.0f, 0.1f, 0.0f,
};
static const size_t nvertices = sizeof(vertices)/(sizeof(float)*3);

static const unsigned indices[4/*#triangles*/*3/*#ids*/] = {
  0, 1, 2,
  0, 3, 1,
  0, 2, 3,
  1, 3, 2
};
static const size_t ntriangles = sizeof(indices)/(sizeof(unsigned)*3);

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static FILE*
write_tetrahedron(struct sstl* sstl, const enum sstl_type type)
{
  struct sstl_facet facet = SSTL_FACET_NULL;
  struct sstl_writer_create_args args = SSTL_WRITER_CREATE_ARGS_DEFAULT;
  struct sstl_writer* writer = NULL;
  FILE* fp = NULL;
  size_t i = 0;

  CHK(sstl != NULL);

  CHK((fp = tmpfile()) != NULL);
  args.filename = "Tetrahedron.stl";
  args.stream = fp;
  args.type = type;
  args.solid_name = "cube corner";
  args.verbose = 3;
  CHK(sstl_writer_create(&args, &writer) == RES_OK);

  FOR_EACH(i, 0, ntriangles) {
    f3_set(facet.vertices[0], vertices+(indices[i*3/*#ids*/+0])*3/*#coords*/);
    f3_set(facet.vertices[1], vertices+(indices[i*3/*#ids*/+1])*3/*#coords*/);
    f3_set(facet.vertices[2], vertices+(indices[i*3/*#ids*/+2])*3/*#coords*/);
    CHK(sstl_write_facet(writer, &facet) == RES_OK);
  }

  CHK(sstl_writer_ref_put(writer) == RES_OK);
  rewind(fp);
  return fp;
}

static void
check_tetrahedron(struct sstl* sstl, FILE* fp)
{
  struct sstl_desc desc = SSTL_DESC_NULL;
  struct sstl_facet facet = SSTL_FACET_NULL;
  float coords[3] = {0,0,0};
  unsigned ids[3] = {0,0,0};
  size_t i = 0;

  CHK(sstl != NULL);
  CHK(fp != NULL);

  CHK(sstl_load_stream(sstl, fp, "Tetrahedron") == RES_OK);

  CHK(sstl_get_desc(NULL, &desc) == RES_BAD_ARG);
  CHK(sstl_get_desc(sstl, NULL) == RES_BAD_ARG);
  CHK(sstl_get_desc(sstl, &desc) == RES_OK);

  CHK(desc.vertices_count == nvertices);
  CHK(desc.triangles_count == ntriangles);

  CHK(sstl_desc_get_vertex_coords(NULL, 0, coords) == RES_BAD_ARG);
  CHK(sstl_desc_get_vertex_coords(&desc, nvertices, coords) == RES_BAD_ARG);
  CHK(sstl_desc_get_vertex_coords(&desc, 0, NULL) == RES_BAD_ARG);
  FOR_EACH(i, 0, desc.vertices_count) {
    CHK(sstl_desc_get_vertex_coords(&desc, i, coords) == RES_OK);
    CHK(f3_eq(coords, vertices + i*3));
  }

  CHK(sstl_desc_get_triangle_ids(NULL, 0, ids) == RES_BAD_ARG);
  CHK(sstl_desc_get_triangle_ids(&desc, ntriangles, ids) == RES_BAD_ARG);
  CHK(sstl_desc_get_triangle_ids(&desc, 0, NULL) == RES_BAD_ARG);
  FOR_EACH(i, 0, desc.triangles_count) {
    CHK(sstl_desc_get_triangle_ids(&desc, i, ids) == RES_OK);
    CHK(ids[0] == indices[i*3+0]);
    CHK(ids[1] == indices[i*3+1]);
    CHK(ids[2] == indices[i*3+2]);
  }

  CHK(sstl_desc_get_facet(NULL, 0, &facet) == RES_BAD_ARG);
  CHK(sstl_desc_get_facet(&desc, ntriangles, &facet) == RES_BAD_ARG);
  CHK(sstl_desc_get_facet(&desc, 0, NULL) == RES_BAD_ARG);
  FOR_EACH(i, 0, desc.triangles_count) {
    CHK(sstl_desc_get_facet(&desc, i, &facet) == RES_OK);
    CHK(sstl_desc_get_triangle_ids(&desc, i, ids) == RES_OK);

    CHK(sstl_desc_get_vertex_coords(&desc, ids[0], coords) == RES_OK);
    CHK(f3_eq(coords, facet.vertices[0]));
    CHK(sstl_desc_get_vertex_coords(&desc, ids[1], coords) == RES_OK);
    CHK(f3_eq(coords, facet.vertices[1]));
    CHK(sstl_desc_get_vertex_coords(&desc, ids[2], coords) == RES_OK);
    CHK(f3_eq(coords, facet.vertices[2]));
  }
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sstl* sstl = NULL;
  FILE* fp = NULL;
  (void)argc, (void)argv;

  CHK(sstl_create(NULL, NULL, 3, &sstl) == RES_OK);

  CHK((fp = write_tetrahedron(sstl, SSTL_ASCII)) != NULL);
  check_tetrahedron(sstl, fp);
  CHK(fclose(fp) == 0);

  CHK((fp = write_tetrahedron(sstl, SSTL_BINARY)) != NULL);
  check_tetrahedron(sstl, fp);
  CHK(fclose(fp) == 0);

  CHK(sstl_ref_put(sstl) == RES_OK);
  CHK(mem_allocated_size() == 0);
  return 0;
}
