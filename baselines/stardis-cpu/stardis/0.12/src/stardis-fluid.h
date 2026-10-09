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

#ifndef SDIS_FLUID_H
#define SDIS_FLUID_H

#include <sdis.h>

#include <rsys/rsys.h>
#include <rsys/str.h>

struct stardis;
struct mem_allocator;

/*******************************************************************************
 * Fluid data
 ******************************************************************************/
struct fluid {
  struct str name;
  double rho; /* Volumic mass */
  double cp; /* Calorific capacity */
  double tinit;
  double imposed_temperature;
  double t0; /* End time of tinit */
  int is_outside; /* the fluid is used for a boundary */
  int is_green; /* green computation (nothing to do with fluid itself) */
  unsigned desc_id;
  unsigned fluid_id;
};

res_T
create_solver_fluid
  (struct stardis* stardis,
   const struct fluid* fluid_props);

res_T
init_fluid(struct mem_allocator* allocator, struct fluid** dst);

void
release_fluid
  (struct fluid* desc,
   struct mem_allocator* allocator);

res_T
str_print_fluid(struct str* str, const struct fluid* s);

#endif
