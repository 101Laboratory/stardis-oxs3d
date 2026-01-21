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

#ifndef STARDIS_PROGRAM_H
#define STARDIS_PROGRAM_H

#include "stardis-app.h"
#include "stardis-prog-properties.h"

#include <rsys/rsys.h>
#include <rsys/str.h>

/* Forward declarations */
struct mem_allocator;

/* type to store data for libraries involved in programmed descriptions */
struct program {
  void* prog_data; /* result of the create() call */
  struct str name;
  struct str lib_path;
  size_t argc;
  char** argv;
  /* lib handle and function ptrs */
  void* lib_handle;
  const char* (*get_copyright_notice)(void*);
  const char* (*get_license_short)(void*);
  const char* (*get_license_text)(void*);
  void* (*create)(struct stardis_program_context*, size_t, char**);
  enum stardis_return_status (*finalize)(void*);
  void (*release)(void*);
};

res_T
init_program
  (struct mem_allocator* allocator,
   struct program** dst);

void
release_program
  (struct program* program,
   struct mem_allocator* allocator);

res_T
str_print_program
  (struct str* str,
   const struct program* program);

#endif
