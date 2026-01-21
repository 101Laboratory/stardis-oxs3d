/* Copyright (C) 2013-2023, 2025 Vincent Forest (vaplv@free.fr)
 *
 * The RSys library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The RSys library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the RSys library. If not, see <http://www.gnu.org/licenses/>. */

#ifndef LIBRARY_H
#define LIBRARY_H

#include "rsys.h"

BEGIN_DECLS

RSYS_API void* /* Library handle */
library_open
  (const char* filename);

RSYS_API void*
library_get_symbol
  (void* lib,
   const char* symbol);

RSYS_API res_T
library_close
  (void* handle);

END_DECLS

#endif /* LIBRARY_H */
