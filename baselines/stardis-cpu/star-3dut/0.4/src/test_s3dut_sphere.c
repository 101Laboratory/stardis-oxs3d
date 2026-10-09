/* Copyright (C) 2016, 2017, 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#include "s3dut.h"
#include "test_s3dut_utils.h"

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3dut_mesh* msh;
  struct s3dut_mesh_data data;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);

  CHK(s3dut_create_sphere(NULL, 0, 0, 0, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 1, 0, 0, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 0, 3, 0, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 1, 3, 0, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 0, 0, 2, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 1, 0, 2, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 0, 3, 2, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 1, 3, 2, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 0, 0, 0, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 1, 0, 0, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 0, 3, 0, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 1, 3, 0, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 0, 0, 2, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 1, 0, 2, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 0, 3, 2, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(NULL, 1, 3, 2, &msh) == RES_OK);

  CHK(s3dut_mesh_ref_get(NULL) == RES_BAD_ARG);
  CHK(s3dut_mesh_ref_get(msh) == RES_OK);
  CHK(s3dut_mesh_ref_put(NULL) == RES_BAD_ARG);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  CHK(s3dut_create_sphere(&allocator, 1, 3, 2, &msh) == RES_OK);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  CHK(s3dut_create_sphere(&allocator, 1, 2, 2, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(&allocator, 1, 3, 1, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(&allocator,-1, 3, 2, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_sphere(&allocator, 1, 32, 16, &msh) == RES_OK);

  CHK(s3dut_mesh_get_data(NULL, NULL) == RES_BAD_ARG);
  CHK(s3dut_mesh_get_data(msh, NULL) == RES_BAD_ARG);
  CHK(s3dut_mesh_get_data(NULL, &data) == RES_BAD_ARG);
  CHK(s3dut_mesh_get_data(msh, &data) == RES_OK);
  CHK(data.positions != NULL);
  CHK(data.indices != NULL);
  CHK(data.nvertices >= (32*(16-1)+2));
  CHK(data.nprimitives == (32*(16-2)*2) + 2*32);

  dump_mesh_data(stdout, &data);

  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

