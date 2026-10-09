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

#include <rsys/double3.h>
#include <rsys/math.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3dut_mesh* msh;
  struct s3dut_mesh_data data;
  double z_range[2];
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);

  #define CR_TS s3dut_create_truncated_sphere
  z_range[0] = z_range[1] = 0;
  CHK(CR_TS(NULL, 1, 16, 8, z_range, 0, &msh) == RES_BAD_ARG);
  z_range[1] = -0.5;
  CHK(CR_TS(NULL, 1, 16, 8, z_range, 0, &msh) == RES_BAD_ARG);
  z_range[1] = +0.5;
  CHK(CR_TS(NULL, 1, 16, 8, z_range, 0, &msh) == RES_OK);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);
  CHK(CR_TS(NULL, 1, 16, 8, NULL, 0, &msh) == RES_OK);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  CHK(CR_TS(&allocator, 1, 16, 8, NULL, 0, &msh) == RES_OK);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  z_range[0] = 0;
  z_range[1] = 0.9;
  CHK(CR_TS(&allocator, 1, 16, 8, z_range, S3DUT_CAP_NEG_Z, &msh) == RES_OK);

  CHK(s3dut_mesh_get_data(msh, &data) == RES_OK);
  CHK(data.positions != NULL);
  CHK(data.indices != NULL);
  #undef CR_TS

  dump_mesh_data(stdout, &data);

  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

