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

#ifndef SDIS_SOLID_PROG_H
#define SDIS_SOLID_PROG_H

#include <rsys/rsys.h>
#include <rsys/str.h>

#include "stardis-prog-properties.h"

struct stardis;
struct mem_allocator;
struct program;

/*******************************************************************************
 * Solid prog data
 ******************************************************************************/
struct solid_prog {
  void* prog_data; /* result of the create() call */
  struct str name;
  struct str prog_name;
  size_t argc;
  char** argv;
  int is_outside; /* the solid is used for a boundary */
  unsigned desc_id; /* id of the boundary; meaningful if is_outside */
  unsigned solid_id;
  /* lib handle and function ptrs */
  struct program* program;
  void* (*create)
    (const struct stardis_description_create_context*, void*, size_t, char**);
  void (*release)(void*);
  double (*lambda)(const struct stardis_vertex*, void*);
  double (*rho)(const struct stardis_vertex*, void*);
  double (*cp)(const struct stardis_vertex*, void*);
  double (*delta)(const struct stardis_vertex*, void*);
  double (*temp)(const struct stardis_vertex*, void*);
  double (*vpower)(const struct stardis_vertex*, void*);
  double (*t_range)(void* data, double t_range[2]);

  /* User-defined function for sampling a conductive path */
  int
  (*sample_path)
    (struct sdis_scene*,
     struct ssp_rng*,
     struct stardis_path*,
     void*);
};

res_T
create_solver_solid_prog
  (struct stardis* stardis,
   const struct solid_prog* solid_props);

res_T
init_solid_prog
  (struct mem_allocator* allocator,
   struct solid_prog** dst);

void
release_solid_prog
  (struct solid_prog* solid,
   struct mem_allocator* allocator);

res_T
str_print_solid_prog
  (struct str* str,
   const struct solid_prog* s);

#endif
