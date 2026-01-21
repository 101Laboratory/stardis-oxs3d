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

#include "stardis-app.h"
#include "stardis-fbound-prog.h"
#include "stardis-prog-properties.h"
#include "stardis-intface.h"

#include <rsys/rsys.h>
#include <rsys/mem_allocator.h>
#include <rsys/str.h>

#include <sdis.h>

#include <limits.h>

/*******************************************************************************
 * Public Functions
 ******************************************************************************/

res_T
init_f_boundary_prog
  (struct mem_allocator* allocator,
   struct f_boundary_prog** dst)
{
  res_T res = RES_OK;
  int str_initialized = 0;
  ASSERT(allocator && dst && *dst == NULL);
  *dst = MEM_CALLOC(allocator, 1, sizeof(**dst));
  if(! *dst) {
    res = RES_MEM_ERR;
    goto error;
  }
  str_init(allocator, &(*dst)->name);
  str_init(allocator, &(*dst)->prog_name);
  str_initialized = 1;
  (*dst)->mat_id = UINT_MAX;
end:
  return res;
error:
  if(str_initialized) {
    str_release(&(*dst)->name);
    str_release(&(*dst)->prog_name);
  }
  if(*dst) MEM_RM(allocator, *dst);
  goto end;
}

void
release_f_boundary_prog
  (struct f_boundary_prog* bound,
   struct mem_allocator* allocator)
{
  size_t i;
  ASSERT(bound && allocator);
  str_release(&bound->name);
  str_release(&bound->prog_name);
  if(bound->prog_data)
    bound->release(bound->prog_data);
  for(i = 0; i < bound->argc; i++) MEM_RM(allocator, bound->argv[i]);
  MEM_RM(allocator, bound->argv);
  /* library_close call is managed at lib_data level */
  MEM_RM(allocator, bound);
}

res_T
str_print_f_boundary_prog
  (struct str* str,
   const struct f_boundary_prog* b)
{
  res_T res = RES_OK;
  ASSERT(str && b);
  ASSERT(b->argc >= 1); /* At least one argument which is the program name */

  ERR(str_append_printf(str,
    "programmed F boundary for SOLID '%s': lib='%s', "
    "(using medium %u as external medium)",
    str_cget(&b->name), str_cget(&b->prog_name), b->mat_id));
  if(b->argc > 1) {
    size_t i;
    ERR(str_append_printf(str, ", provided arguments:\n"));
    for(i = 1; i < b->argc; i++) {
      ERR(str_append_printf(str, (i+1 == b->argc ? "\t%s" : "\t%s\n"), b->argv[i]));
    }
  }
end:
  return res;
error:
  goto end;
}

