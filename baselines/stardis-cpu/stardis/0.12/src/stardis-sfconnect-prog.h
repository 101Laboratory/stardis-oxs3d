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

#ifndef SDIS_SF_CONNECT_PROG_H
#define SDIS_SF_CONNECT_PROG_H

#include <rsys/rsys.h>
#include <rsys/str.h>

#include "stardis-prog-properties.h"

struct stardis;
struct mem_allocator;
struct program;

/*******************************************************************************
 * Solid-Fluid prog data
 ******************************************************************************/
struct solid_fluid_connect_prog {
  void* prog_data; /* result of the create() call */
  struct str name;
  struct str prog_name;
  size_t argc;
  char** argv;
  int is_outside; /* the fluid is used for a boundary */
  unsigned desc_id; /* id of the boundary; meaningful if is_outside */
  unsigned connection_id;
  /* lib handle and function ptrs */
  struct program* program;
  void* (*create)
    (const struct stardis_description_create_context*, void*, size_t, char**);
  void (*release)(void*);
  double (*ref_temp)(const struct stardis_interface_fragment*, void*);
  double (*emissivity)
    (const struct stardis_interface_fragment*, const unsigned src_id, void*);
  double (*alpha)
    (const struct stardis_interface_fragment*, const unsigned src_id, void*);
  double (*hc)(const struct stardis_interface_fragment*, void*);
  double (*flux)(const struct stardis_interface_fragment*, void*);
  double (*hmax)(void*);
  double* (*t_range)(void*, double trange[2]);
};

res_T
create_solver_sf_connect_prog
  (struct stardis* stardis,
   const struct solid_fluid_connect_prog* connect);

res_T
init_sf_connect_prog
  (struct mem_allocator* allocator,
   struct solid_fluid_connect_prog** dst);

void
release_sf_connect_prog
  (struct solid_fluid_connect_prog* connect,
   struct mem_allocator* allocator);

res_T
str_print_sf_connect_prog
  (struct str* str,
   const struct solid_fluid_connect_prog* connect);

double
sf_connect_prog_get_hmax
  (const struct solid_fluid_connect_prog* connect);

#endif
