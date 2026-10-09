/* Copyright (C) 2014-2017, 2020-2023 Vincent Forest (vaplv@free.fr)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#ifndef AW_C_H
#define AW_C_H

#include "aw.h"

extern LOCAL_SYM res_T
parse_doubleX
  (double* dblX,
   const unsigned int count_min,
   const unsigned int count_max,
   const double range_min,
   const double range_max,
   const double default_value,
   char** tk_ctxt);

extern LOCAL_SYM res_T
setup_default_logger
  (struct mem_allocator* allocator,
   struct logger* logger,
   const char* prefix_info, /* May be NULL */
   const char* prefix_error, /* May be NULL */
   const char* prefix_warning); /* May be NULL */

#endif /* AW_C_H */

