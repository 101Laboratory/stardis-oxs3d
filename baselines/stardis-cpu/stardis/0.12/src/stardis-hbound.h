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

#ifndef SDIS_HBOUND_H
#define SDIS_HBOUND_H

#include <rsys/rsys.h>
#include <rsys/str.h>

struct mem_allocator;
struct fluid;
struct description;

/*******************************************************************************
 * H boundary type
 ******************************************************************************/
struct h_boundary {
  struct str name;
  double ref_temperature;
  double emissivity;
  double specular_fraction;
  double hc;
  double imposed_temperature;
  unsigned mat_id;
  struct fluid* possible_external_fluid; /* if H for solid */
};

res_T
init_h_boundary
  (struct mem_allocator* allocator,
   struct h_boundary** dst);

void
release_h_boundary
  (struct h_boundary* bound,
   struct mem_allocator* allocator);

res_T
str_print_h_boundary
  (struct str* str,
   const struct description* desc);

#endif
