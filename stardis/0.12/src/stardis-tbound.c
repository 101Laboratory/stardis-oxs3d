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
#include "stardis-tbound.h"
#include "stardis-fluid.h"
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
init_t_boundary
  (struct mem_allocator* allocator,
   struct t_boundary** dst)
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
  (*dst)->imposed_temperature = SDIS_TEMPERATURE_NONE;
  (*dst)->mat_id = UINT_MAX;
end:
  return res;
error:
  if(str_initialized) str_release(&(*dst)->name);
  if(*dst) MEM_RM(allocator, *dst);
  goto end;
}

void
release_t_boundary
  (struct t_boundary* bound,
   struct mem_allocator* allocator)
{
  ASSERT(bound && allocator);
  str_release(&bound->name);
  MEM_RM(allocator, bound);
}

res_T
str_print_t_boundary
  (struct str* str,
   const struct t_boundary* b)
{
  res_T res = RES_OK;
  ASSERT(str && b);
  ERR(str_append_printf(str,
    "T boundary for solid '%s': T=%g (using medium %u as external medium)",
    str_cget(&b->name), b->imposed_temperature, b->mat_id));
end:
  return res;
error:
  goto end;
}
