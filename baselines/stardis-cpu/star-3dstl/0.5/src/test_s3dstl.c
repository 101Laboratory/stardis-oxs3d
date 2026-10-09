/* Copyright (C) 2016, 2018, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#include "s3dstl.h"

#include <rsys/logger.h>
#include<rsys/mem_allocator.h>

#include <star/s3d.h>
#include <star/sstl.h>

static void
test_load(struct s3dstl* s3dstl)
{
  FILE* file, *file2;
  struct s3d_shape* shape, *shape2;
  static const char* test0 =
    "solid my solid\n"
    "endsolid my solid";
  static const char* bad =
    "solid\n"
    "  facet normal 0.0 -1.0 0.0\n"
    "    outer loop\n"
    "      vertex 0.0 0.0 0.0\n"
    "      vertex 0.0 0.0 1.0\n"
    "    endloop\n"
    "  endfacet";
  static const char* cube[] = {
    "solid cube\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 0 0 0\n",
    "      vertex 0 1 0\n",
    "      vertex 1 1 0\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 0 0 0\n",
    "      vertex 1 1 0\n",
    "      vertex 1 0 0\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 0 0 0\n",
    "      vertex 0 0 1\n",
    "      vertex 0 1 1\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 0 0 0\n",
    "      vertex 0 1 1\n",
    "      vertex 0 1 0\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 0 0 0\n",
    "      vertex 1 0 0\n",
    "      vertex 1 0 1\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 0 0 0\n",
    "      vertex 1 0 1\n",
    "      vertex 0 0 1\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 0 0 1\n",
    "      vertex 1 0 1\n",
    "      vertex 1 1 1\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 0 0 1\n",
    "      vertex 1 1 1\n",
    "      vertex 0 1 1\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 1 0 0\n",
    "      vertex 1 1 0\n",
    "      vertex 1 1 1\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 1 0 0\n",
    "      vertex 1 1 1\n",
    "      vertex 1 0 1\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 0 1 0\n",
    "      vertex 0 1 1\n",
    "      vertex 1 1 1\n",
    "    endloop\n",
    "  endfacet\n",
    "  facet normal 0 0 0\n",
    "    outer loop\n",
    "      vertex 0 1 0\n",
    "      vertex 1 1 1\n",
    "      vertex 1 1 0\n",
    "    endloop\n",
    "  endfacet\n",
    "endsolid cube"
  };
  const size_t cube_nlines = sizeof(cube)/sizeof(const char*);
  size_t i;

  file = fopen("test_empty.stl", "w");
  CHK(file != NULL);
  fwrite(test0, sizeof(char), strlen(test0), file);
  fclose(file);

  CHK(s3dstl_load(NULL, NULL) == RES_BAD_ARG);
  CHK(s3dstl_load(s3dstl, NULL) == RES_BAD_ARG);
  CHK(s3dstl_load(NULL, "test_empty.stl") == RES_BAD_ARG);
  CHK(s3dstl_load(s3dstl, "test_none.stl") == RES_IO_ERR);
  CHK(s3dstl_load(s3dstl, "test_empty.stl") == RES_OK);

  CHK(s3dstl_get_shape(NULL, NULL) == RES_BAD_ARG);
  CHK(s3dstl_get_shape(s3dstl, NULL) == RES_BAD_ARG);
  CHK(s3dstl_get_shape(NULL, &shape) == RES_BAD_ARG);
  CHK(s3dstl_get_shape(s3dstl, &shape) == RES_OK);
  CHK(shape == NULL);

  file = tmpfile();
  FOR_EACH(i, 0, cube_nlines)
    fwrite(cube[i], sizeof(char), strlen(cube[i]), file);
  rewind(file);

  CHK(s3dstl_load_stream(NULL, NULL) == RES_BAD_ARG);
  CHK(s3dstl_load_stream(s3dstl, NULL) == RES_BAD_ARG);
  CHK(s3dstl_load_stream(NULL, file) == RES_BAD_ARG);
  CHK(s3dstl_load_stream(s3dstl, file) == RES_OK);

  CHK(s3dstl_get_shape(s3dstl, &shape) == RES_OK);
  CHK(shape != NULL);

  file2 = tmpfile();
  CHK(file2 != NULL);
  fwrite(bad, sizeof(char), strlen(bad), file2);
  rewind(file2);
  CHK(s3dstl_load_stream(s3dstl, file2) == RES_BAD_ARG);
  CHK(s3dstl_get_shape(s3dstl, &shape2) == RES_OK);
  CHK(shape == shape2);

  rewind(file);
  CHK(s3dstl_load_stream(s3dstl, file) == RES_OK);
  CHK(s3dstl_get_shape(s3dstl, &shape) == RES_OK);
  CHK(shape != NULL);

  CHK(s3dstl_clear(NULL) == RES_BAD_ARG);
  CHK(s3dstl_clear(s3dstl) == RES_OK);
  CHK(s3dstl_get_shape(s3dstl, &shape) == RES_OK);
  CHK(shape == NULL);

  CHK(fclose(file) == 0);
  CHK(fclose(file2) == 0);
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator_proxy;
  struct s3dstl* s3dstl;
  struct s3d_device* s3d;
  struct sstl* sstl;
  struct sstl* sstl2;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator);

  CHK(s3d_device_create(NULL, &allocator_proxy, 0, &s3d) == RES_OK);

  CHK(s3dstl_create(NULL, NULL, NULL, NULL, 1, NULL) == RES_BAD_ARG);
  CHK(s3dstl_create(NULL, NULL, NULL, NULL, 1, &s3dstl) == RES_BAD_ARG);
  CHK(s3dstl_create(NULL, NULL, NULL, s3d, 1, NULL) == RES_BAD_ARG);
  CHK(s3dstl_create(NULL, NULL, NULL, s3d, 1, &s3dstl) == RES_OK);

  CHK(s3dstl_ref_get(NULL) == RES_BAD_ARG);
  CHK(s3dstl_ref_get(s3dstl) == RES_OK);
  CHK(s3dstl_ref_put(NULL) == RES_BAD_ARG);
  CHK(s3dstl_ref_put(s3dstl) == RES_OK);
  CHK(s3dstl_ref_put(s3dstl) == RES_OK);

  CHK(s3dstl_create(NULL, &allocator_proxy, NULL, NULL, 1, NULL) == RES_BAD_ARG);
  CHK(s3dstl_create(NULL, &allocator_proxy, NULL, NULL, 1, &s3dstl) == RES_BAD_ARG);
  CHK(s3dstl_create(NULL, &allocator_proxy, NULL, s3d, 1, NULL) == RES_BAD_ARG);
  CHK(s3dstl_create(NULL, &allocator_proxy, NULL, s3d, 1, &s3dstl) == RES_OK);
  CHK(s3dstl_ref_put(s3dstl) == RES_OK);

  CHK(s3dstl_create
    (LOGGER_DEFAULT, &allocator_proxy, NULL, s3d, 1, &s3dstl) == RES_OK);

  CHK(s3dstl_get_sstl(NULL, NULL) == RES_BAD_ARG);
  CHK(s3dstl_get_sstl(s3dstl, NULL) == RES_BAD_ARG);
  CHK(s3dstl_get_sstl(NULL, &sstl) == RES_BAD_ARG);
  CHK(s3dstl_get_sstl(s3dstl, &sstl) == RES_OK);

  CHK(s3dstl_ref_put(s3dstl) == RES_OK);

  CHK(sstl_create(NULL, &allocator_proxy, 1, &sstl) == RES_OK);
  CHK(s3dstl_create(NULL, &allocator_proxy, sstl, s3d, 1, &s3dstl) == RES_OK);
  CHK(s3dstl_get_sstl(s3dstl, &sstl2) == RES_OK);
  CHK(sstl == sstl2);

  test_load(s3dstl);

  CHK(s3dstl_ref_put(s3dstl) == RES_OK);
  CHK(s3d_device_ref_put(s3d) == RES_OK);
  CHK(sstl_ref_put(sstl) == RES_OK);

  if(MEM_ALLOCATED_SIZE(&allocator_proxy)) {
    char dump[512];
    MEM_DUMP(&allocator_proxy, dump, sizeof(dump)/sizeof(char));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks\n");
  }
  mem_shutdown_proxy_allocator(&allocator_proxy);
  CHK(mem_allocated_size() == 0);
  return 0;
}

