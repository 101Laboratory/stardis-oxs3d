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

  CHK(s3dut_create_cuboid(NULL, 0, 0, 0, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 1, 0, 0, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 0, 1, 0, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 1, 1, 0, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 0, 0, 1, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 1, 0, 1, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 0, 1, 1, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 1, 1, 1, NULL) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 0, 0, 0, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 1, 0, 0, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 0, 1, 0, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 1, 1, 0, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 0, 0, 1, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 1, 0, 1, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 0, 1, 1, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(NULL, 1, 1, 1, &msh) == RES_OK);

  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  CHK(s3dut_create_cuboid(&allocator, 1, 1, 2, &msh) == RES_OK);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  CHK(s3dut_create_cuboid(&allocator,-1, 1, 2, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(&allocator, 1,-1, 2, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(&allocator, 1, 1,-2, &msh) == RES_BAD_ARG);
  CHK(s3dut_create_cuboid(&allocator, 1, 1, 2, &msh) == RES_OK);

  CHK(s3dut_mesh_get_data(msh, &data) == RES_OK);
  dump_mesh_data(stdout, &data);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

