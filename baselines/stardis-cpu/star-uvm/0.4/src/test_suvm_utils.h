/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_SUVM_UTILS_H
#define TEST_SUVM_UTILS_H

#include <rsys/mem_allocator.h>
#include <stdlib.h>

static INLINE float
rand_canonic(void)
{
  int r;
  while((r = rand()) == RAND_MAX);
  return (float)r / (float)RAND_MAX;
}

static INLINE void
dump_volumic_mesh
  (FILE* stream,
   const size_t* tetras,
   const size_t ntetras,
   const double* pos,
   const size_t npos)
{
  size_t i;

  FOR_EACH(i, 0, npos) {
    fprintf(stream, "v %g %g %g\n",
      pos[i*3+0],
      pos[i*3+1],
      pos[i*3+2]);
  }

  FOR_EACH(i, 0, ntetras) {
    fprintf(stream, "f %lu %lu %lu\n",
      (unsigned long)tetras[i*4+0]+1,
      (unsigned long)tetras[i*4+1]+1,
      (unsigned long)tetras[i*4+2]+1);
    fprintf(stream, "f %lu %lu %lu\n",
      (unsigned long)tetras[i*4+0]+1,
      (unsigned long)tetras[i*4+3]+1,
      (unsigned long)tetras[i*4+1]+1);
    fprintf(stream, "f %lu %lu %lu\n",
      (unsigned long)tetras[i*4+1]+1,
      (unsigned long)tetras[i*4+3]+1,
      (unsigned long)tetras[i*4+2]+1);
    fprintf(stream, "f %lu %lu %lu\n",
      (unsigned long)tetras[i*4+2]+1,
      (unsigned long)tetras[i*4+3]+1,
      (unsigned long)tetras[i*4+0]+1);
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

#endif /* TEST_SUVM_UTILS_H */

