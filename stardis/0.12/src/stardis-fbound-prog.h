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

#ifndef SDIS_FBOUND_PROG_H
#define SDIS_FBOUND_PROG_H

#include <rsys/rsys.h>
#include <rsys/str.h>

#include "stardis-prog-properties.h"

struct stardis;
struct mem_allocator;
struct fluid_prog;
struct description;
struct program;

/*******************************************************************************
 * F boundary prog data
 ******************************************************************************/
struct f_boundary_prog {
  void* prog_data; /* result of the create() call */
  struct str name;
  struct str prog_name;
  size_t argc;
  char** argv;
  /* lib handle and function ptrs */
  struct program* program;
  void* (*create)
    (const struct stardis_description_create_context*, void*, size_t, char**);
  void (*release)(void*);
  double (*flux)(const struct stardis_interface_fragment*, void*);
  unsigned mat_id;
};

res_T
init_f_boundary_prog
  (struct mem_allocator* allocator,
   struct f_boundary_prog** dst);

void
release_f_boundary_prog
  (struct f_boundary_prog* bound,
   struct mem_allocator* allocator);

res_T
str_print_f_boundary_prog
  (struct str* str,
   const struct f_boundary_prog* b);

#endif
