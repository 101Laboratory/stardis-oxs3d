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
#include "stardis-ssconnect-prog.h"
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
init_ss_connect_prog
  (struct mem_allocator* allocator,
   struct solid_solid_connect_prog** dst)
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
  (*dst)->connection_id = UINT_MAX;
end:
  return res;
error:
  if(str_initialized) {
    str_release(&(*dst)->name);
    str_release(&(*dst)->prog_name);
  }
  (*dst)->connection_id = UINT_MAX;
  if(*dst) MEM_RM(allocator, *dst);
  goto end;
}

void
release_ss_connect_prog
  (struct solid_solid_connect_prog* connect,
   struct mem_allocator* allocator)
{
  size_t i;
  ASSERT(connect && allocator);
  str_release(&connect->name);
  str_release(&connect->prog_name);
  if(connect->prog_data)
    connect->release(connect->prog_data);
  for(i = 0; i < connect->argc; i++) MEM_RM(allocator, connect->argv[i]);
  MEM_RM(allocator, connect->argv);
  /* library_close call is managed at lib_data level */
  MEM_RM(allocator, connect);
}

res_T
str_print_ss_connect_prog
  (struct str* str,
   const struct solid_solid_connect_prog* c)
{
  res_T res = RES_OK;
  ASSERT(str && c);
  ASSERT(c->argc >= 1); /* At least one argument which is the program name */

  ERR(str_append_printf(str,
    "programmed Solid-Solid connection '%s': lib='%s'",
    str_cget(&c->name), str_cget(&c->prog_name)));
  if(c->argc > 1) {
    size_t i;
    ERR(str_append_printf(str, ", provided arguments:\n"));
    for(i = 1; i < c->argc; i++) {
      ERR(str_append_printf(str, (i+1 == c->argc ? "\t%s" : "\t%s\n"), c->argv[i]));
    }
  }
end:
  return res;
error:
  goto end;
}
