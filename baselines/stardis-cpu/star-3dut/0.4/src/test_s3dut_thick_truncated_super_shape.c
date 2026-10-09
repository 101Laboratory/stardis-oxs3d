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
  struct s3dut_super_formula f0, f1;
  double z_range[2];
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);

  f0.A = 1; f0.B = 1; f0.M = 5; f0.N0 = 1; f0.N1 = 1; f0.N2 = 2;
  f1.A = 1; f1.B = 1; f1.M = 5; f1.N0 = 1; f1.N1 = 1; f1.N2 = 3;

  #define CREATE s3dut_create_thick_truncated_super_shape
  CHK(CREATE(NULL, NULL, NULL, 0, -1, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, -1, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, -1, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, -1, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, -1, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, -1, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, -1, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, -1, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, 0, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, 0, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, 0, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, 0, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, 0, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, 0, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, 0, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, 0, 0, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, -1, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, -1, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, -1, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, -1, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, -1, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, -1, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, -1, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, -1, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, 0, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, 0, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, 0, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, 0, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, 0, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, 0, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, 0, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, 0, 3, 0, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, -1, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, -1, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, -1, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, -1, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, -1, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, -1, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, -1, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, -1, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, 0, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, 0, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, 0, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, 0, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, 0, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, 0, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, 0, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, 0, 0, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, -1, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, -1, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, -1, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, -1, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, -1, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, -1, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, -1, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, -1, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, 0, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, 0, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, 0, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, 0, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, 0, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, 0, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, 0, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, 0, 3, 2, NULL, 0, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, -1, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, -1, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, -1, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, -1, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, -1, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, -1, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, -1, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, -1, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, 0, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, 0, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, 0, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, 0, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, 0, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, 0, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, 0, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, 0, 0, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, -1, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, -1, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, -1, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, -1, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, -1, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, -1, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, -1, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, -1, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, 0, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, 0, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, 0, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, 0, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, 0, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, 0, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, 0, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, 0, 3, 0, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, -1, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, -1, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, -1, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, -1, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, -1, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, -1, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, -1, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, -1, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, 0, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, 0, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, 0, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, 0, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, 0, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, 0, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, 0, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, 0, 0, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, -1, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, -1, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, -1, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, -1, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, -1, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, -1, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, -1, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, -1, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 0, 0, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 0, 0, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 0, 0, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 0, 0, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, NULL, 1, 0, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, NULL, 1, 0, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &f1, 1, 0, 3, 2, NULL, 0, &msh) == RES_BAD_ARG);
  CHK(CREATE(NULL, &f0, &f1, 1, 0, 3, 2, NULL, 0, &msh) == RES_OK);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  CHK(CREATE(&allocator, &f0, &f1, 1, 0, 3, 2, NULL, 0, &msh) == RES_OK);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  z_range[0] = -0.1, z_range[1] = 0.5;
  CHK(CREATE(&allocator, &f0, &f1, 1, 0.3, 32, 16, z_range, S3DUT_CAP_NEG_Z,
    &msh) == RES_OK);

  CHK(s3dut_mesh_get_data(msh, &data) == RES_OK);
  CHK(data.positions != NULL);
  CHK(data.indices != NULL);

  dump_mesh_data(stdout, &data);

  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

