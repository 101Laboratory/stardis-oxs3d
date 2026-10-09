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
#include "stardis-ssconnect.h"

#include <rsys/rsys.h>
#include <rsys/mem_allocator.h>
#include <rsys/str.h>

#include <sdis.h>

#include <limits.h>

/*******************************************************************************
 * Public Functions
 ******************************************************************************/
res_T
init_ss_connect
  (struct mem_allocator* allocator,
   struct solid_solid_connect** dst)
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
  str_initialized = 1;
  (*dst)->connection_id = UINT_MAX;
end:
  return res;
error:
  if(str_initialized) str_release(&(*dst)->name);
  if(*dst) MEM_RM(allocator, *dst);
  goto end;
}

void
release_ss_connect
  (struct solid_solid_connect* connect,
   struct mem_allocator* allocator)
{
  ASSERT(connect && allocator);
  str_release(&connect->name);
  MEM_RM(allocator, connect);
}

res_T
str_print_ss_connect
  (struct str* str,
   const struct solid_solid_connect* connect)
{
  res_T res = RES_OK;
  ASSERT(str && connect);
  ERR(str_append_printf(str,
    "Solid-Solid connection '%s': contact resistance=%g",
    str_cget(&connect->name), connect->tcr));
end:
  return res;
error:
  goto end;
}

