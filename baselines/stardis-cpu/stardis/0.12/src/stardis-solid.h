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

#ifndef SDIS_SOLID_H
#define SDIS_SOLID_H

#include <rsys/rsys.h>
#include <rsys/str.h>

struct stardis;
struct mem_allocator;

/*******************************************************************************
 * Solid data
 ******************************************************************************/
struct solid {
  struct str name;
  double lambda; /* Conductivity */
  double rho; /* Volumic mass */
  double cp; /* Calorific capacity */
  double delta; /* Numerical parameter */
  double tinit; /* Initial temperature */
  double imposed_temperature; /* Impose a T; SDIS_TEMPERATURE_NONE if unset */
  double vpower;
  double t0; /* End time of tinit */
  int is_outside; /* the solid is used for a boundary */
  int is_green; /* green computation (nothing to do with solid itself) */
  unsigned desc_id;
  unsigned solid_id;
};

res_T
init_solid(struct mem_allocator* allocator, struct solid** dst);

void
release_solid
  (struct solid* desc,
   struct mem_allocator* allocator);

res_T
str_print_solid(struct str* str, const struct solid* solid);

res_T
create_solver_solid
  (struct stardis* stardis,
   const struct solid* solid_props);

#endif
