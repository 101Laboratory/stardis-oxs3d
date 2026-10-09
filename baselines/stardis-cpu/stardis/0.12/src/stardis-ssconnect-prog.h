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

#ifndef SDIS_SS_CONNECT_PROG_H
#define SDIS_SS_CONNECT_PROG_H

#include <rsys/rsys.h>
#include <rsys/str.h>

#include "stardis-prog-properties.h"

struct stardis;
struct mem_allocator;
struct program;

/*******************************************************************************
 * Solid-Solid prog data
 ******************************************************************************/
struct solid_solid_connect_prog {
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
  double (*tcr)(const struct stardis_interface_fragment*, void*);
  unsigned connection_id;
};

res_T
create_solver_ss_connect_prog
  (struct stardis* stardis,
   const struct solid_solid_connect_prog* connect);

res_T
init_ss_connect_prog
  (struct mem_allocator* allocator,
   struct solid_solid_connect_prog** dst);

void
release_ss_connect_prog
  (struct solid_solid_connect_prog* connect,
   struct mem_allocator* allocator);

res_T
str_print_ss_connect_prog
  (struct str* str,
   const struct solid_solid_connect_prog* connect);

#endif

