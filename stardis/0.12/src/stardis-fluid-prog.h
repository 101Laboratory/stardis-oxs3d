/* Copyright (C) 2018-2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef SDIS_FLUID_PROG_H
#define SDIS_FLUID_PROG_H

#include <rsys/rsys.h>
#include <rsys/str.h>

#include "stardis-prog-properties.h"

struct stardis;
struct mem_allocator;
struct program;

/*******************************************************************************
 * Fluid prog data
 ******************************************************************************/
struct fluid_prog {
  void* prog_data; /* result of the create() call */
  struct str name;
  struct str prog_name;
  size_t argc;
  char** argv;
  int is_outside; /* the fluid is used for a boundary */
  unsigned desc_id; /* id of the boundary; meaningful if is_outside */
  unsigned fluid_id;
  /* lib handle and function ptrs */
  struct program* program;
  void* (*create)
    (const struct stardis_description_create_context*, void*, size_t, char**);
  void (*release)(void*);
  double (*rho)(const struct stardis_vertex*, void*);
  double (*cp)(const struct stardis_vertex*, void*);
  double (*temp)(const struct stardis_vertex*, void*);
};

res_T
create_solver_fluid_prog
  (struct stardis* stardis,
   const struct fluid_prog* fluid_props);

res_T
create_solver_external_fluid_prog
  (struct stardis* stardis,
   const struct fluid_prog* fluid_props);

res_T
init_fluid_prog
  (struct mem_allocator* allocator,
   struct fluid_prog** dst);

void
release_fluid_prog
  (struct fluid_prog* fluid,
   struct mem_allocator* allocator);

res_T
str_print_fluid_prog
  (struct str* str,
   const struct fluid_prog* fluid);

#endif
