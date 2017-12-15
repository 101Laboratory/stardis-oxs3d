/* Copyright (C) |Meso|Star> 2016-2017 (contact@meso-star.com)
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

#ifndef TEST_SDIS_UTILS_H
#define TEST_SDIS_UTILS_H

#include <rsys/mem_allocator.h>
#include <stdio.h>

static INLINE void
dummy_getter
  (struct sdis_device* dev,
   struct sdis_data* data,
   const struct sdis_rwalk_vertex* vert,
   double* val)
{
  (void)dev, (void)data;
  CHK(val != NULL && vert != NULL);
  *val = 1;
}

static const struct sdis_solid_shader DUMMY_SOLID_SHADER = {
  dummy_getter,
  dummy_getter,
  dummy_getter,
  dummy_getter,
  dummy_getter,
  dummy_getter
};

static const struct sdis_fluid_shader DUMMY_FLUID_SHADER = {
  dummy_getter,
  dummy_getter,
  dummy_getter
};

static INLINE void
check_memory_allocator(struct mem_allocator* allocator)
{
  if(MEM_ALLOCATED_SIZE(allocator)) {
    char dump[128];
    MEM_DUMP(allocator, dump, sizeof(dump));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks.\n");
  }
}

#endif /* TEST_SDIS_UTILS_H */
