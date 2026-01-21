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

#ifndef TEST_S3DUT_UTILS_H
#define TEST_S3DUT_UTILS_H

#include "s3dut.h"

#include<rsys/mem_allocator.h>
#include <stdio.h>

static INLINE void
dump_mesh_data(FILE* stream, const struct s3dut_mesh_data* data)
{
  size_t i;
  CHK(data != NULL);

  FOR_EACH(i, 0, data->nvertices) {
    fprintf(stream, "v %g %g %g\n", SPLIT3(data->positions + i*3));
  }
  FOR_EACH(i, 0, data->nprimitives) {
    fprintf(stream, "f %lu %lu %lu\n",
      (unsigned long)(data->indices[i*3+0] + 1),
      (unsigned long)(data->indices[i*3+1] + 1),
      (unsigned long)(data->indices[i*3+2] + 1));
  }
}

static INLINE void
check_memory_allocator(struct mem_allocator* allocator)
{
  if(MEM_ALLOCATED_SIZE(allocator)) {
    char dump[512];
    MEM_DUMP(allocator, dump, sizeof(dump)/sizeof(char));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks\n");
  }
}

#endif /* TEST_S3DUT_UTILS_H */

