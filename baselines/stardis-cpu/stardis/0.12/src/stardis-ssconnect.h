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

#ifndef SDIS_SS_CONNECT_H
#define SDIS_SS_CONNECT_H

#include <rsys/rsys.h>
#include <rsys/str.h>

struct mem_allocator;

/*******************************************************************************
 * Solid-Solid connection type
 ******************************************************************************/
struct solid_solid_connect {
  struct str name;
  double tcr;
  unsigned connection_id;
};

res_T
init_ss_connect
  (struct mem_allocator* allocator,
   struct solid_solid_connect** dst);

void
release_ss_connect
  (struct solid_solid_connect* connect,
   struct mem_allocator* allocator);

res_T
str_print_ss_connect
  (struct str* str,
   const struct solid_solid_connect* cconnect);

#endif
