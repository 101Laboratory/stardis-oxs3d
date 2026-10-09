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
#include "stardis-description.h"
#include "stardis-solid.h"
#include "stardis-solid-prog.h"
#include "stardis-fluid.h"
#include "stardis-fluid-prog.h"
#include "stardis-hbound.h"
#include "stardis-hbound-prog.h"
#include "stardis-hfbound.h"
#include "stardis-hfbound-prog.h"
#include "stardis-tbound.h"
#include "stardis-tbound-prog.h"
#include "stardis-fbound.h"
#include "stardis-fbound-prog.h"
#include "stardis-sfconnect.h"
#include "stardis-sfconnect-prog.h"
#include "stardis-ssconnect.h"
#include "stardis-ssconnect-prog.h"
#include "stardis-program.h"

#include <rsys/rsys.h>
#include <rsys/mem_allocator.h>
#include <rsys/str.h>

#include <sdis.h>

#include <limits.h>

res_T
init_description
  (struct mem_allocator* alloc,
   struct description* desc)
{
  ASSERT(desc);
  (void)alloc;
  desc->type = DESCRIPTION_TYPE_COUNT__;
  desc->d.fluid = NULL;
  return RES_OK;
}

void
release_description
  (struct description* desc,
   struct mem_allocator* allocator)
{
  ASSERT(desc && allocator);
  switch (desc->type) {
  case DESC_MAT_SOLID:
    release_solid(desc->d.solid, allocator);
    break;
  case DESC_MAT_SOLID_PROG:
    release_solid_prog(desc->d.solid_prog, allocator);
    break;
  case DESC_MAT_FLUID:
    release_fluid(desc->d.fluid, allocator);
    break;
  case DESC_MAT_FLUID_PROG:
    release_fluid_prog(desc->d.fluid_prog, allocator);
    break;
  case DESC_BOUND_H_FOR_SOLID:
  case DESC_BOUND_H_FOR_FLUID:
    release_h_boundary(desc->d.h_boundary, allocator);
    break;
  case DESC_BOUND_H_FOR_SOLID_PROG:
  case DESC_BOUND_H_FOR_FLUID_PROG:
    release_h_boundary_prog(desc->d.h_boundary_prog, allocator);
    break;
  case DESC_BOUND_HF_FOR_SOLID:
    release_hf_boundary(desc->d.hf_boundary, allocator);
    break;
  case DESC_BOUND_HF_FOR_SOLID_PROG:
    release_hf_boundary_prog(desc->d.hf_boundary_prog, allocator);
    break;
  case DESC_BOUND_T_FOR_SOLID:
    release_t_boundary(desc->d.t_boundary, allocator);
    break;
  case DESC_BOUND_T_FOR_SOLID_PROG:
    release_t_boundary_prog(desc->d.t_boundary_prog, allocator);
    break;
  case DESC_BOUND_F_FOR_SOLID:
    release_f_boundary(desc->d.f_boundary, allocator);
    break;
  case DESC_BOUND_F_FOR_SOLID_PROG:
    release_f_boundary_prog(desc->d.f_boundary_prog, allocator);
    break;
  case DESC_SOLID_FLUID_CONNECT:
    release_sf_connect(desc->d.sf_connect, allocator);
    break;
  case DESC_SOLID_FLUID_CONNECT_PROG:
    release_sf_connect_prog(desc->d.sf_connect_prog, allocator);
    break;
  case DESC_SOLID_SOLID_CONNECT:
    release_ss_connect(desc->d.ss_connect, allocator);
    break;
  case DESC_SOLID_SOLID_CONNECT_PROG:
    release_ss_connect_prog(desc->d.ss_connect_prog, allocator);
    break;
  case DESC_PROGRAM:
   release_program(desc->d.program, allocator);
   break;
  default:
    FATAL("error:" STR(__FILE__) ":" STR(__LINE__)": Invalid type.\n");
  }
}

res_T
str_print_description
  (struct str* str,
   const unsigned rank,
   const struct description* desc)
{
  res_T res = RES_OK;
  ASSERT(str && desc);
  str_clear(str);
  ERR(str_printf(str, "Description %u: ", rank));
  switch (desc->type) {
  case DESC_MAT_SOLID:
    ERR(str_print_solid(str, desc->d.solid));
    break;
  case DESC_MAT_SOLID_PROG:
    ERR(str_print_solid_prog(str, desc->d.solid_prog));
    break;
  case DESC_MAT_FLUID:
    ERR(str_print_fluid(str, desc->d.fluid));
    break;
  case DESC_MAT_FLUID_PROG:
    ERR(str_print_fluid_prog(str, desc->d.fluid_prog));
    break;
  case DESC_BOUND_T_FOR_SOLID:
    ERR(str_print_t_boundary(str, desc->d.t_boundary));
    break;
  case DESC_BOUND_T_FOR_SOLID_PROG:
    ERR(str_print_t_boundary_prog(str, desc->d.t_boundary_prog));
    break;
  case DESC_BOUND_H_FOR_SOLID:
  case DESC_BOUND_H_FOR_FLUID:
    ERR(str_print_h_boundary(str, desc));
    break;
  case DESC_BOUND_H_FOR_SOLID_PROG:
  case DESC_BOUND_H_FOR_FLUID_PROG:
    ERR(str_print_h_boundary_prog(str, desc));
    break;
  case DESC_BOUND_HF_FOR_SOLID:
    ERR(str_print_hf_boundary(str, desc));
    break;
  case DESC_BOUND_HF_FOR_SOLID_PROG:
    ERR(str_print_hf_boundary_prog(str, desc));
    break;
  case DESC_BOUND_F_FOR_SOLID:
    ERR(str_print_f_boundary(str, desc->d.f_boundary));
    break;
  case DESC_BOUND_F_FOR_SOLID_PROG:
    ERR(str_print_f_boundary_prog(str, desc->d.f_boundary_prog));
    break;
  case DESC_SOLID_FLUID_CONNECT:
    ERR(str_print_sf_connect(str, desc->d.sf_connect));
    break;
  case DESC_SOLID_FLUID_CONNECT_PROG:
    ERR(str_print_sf_connect_prog(str, desc->d.sf_connect_prog));
    break;
  case DESC_SOLID_SOLID_CONNECT:
    ERR(str_print_ss_connect(str, desc->d.ss_connect));
    break;
  case DESC_SOLID_SOLID_CONNECT_PROG:
    ERR(str_print_ss_connect_prog(str, desc->d.ss_connect_prog));
    break;
  case DESC_PROGRAM:
    ERR(str_print_program(str, desc->d.program));
    break;
  default:
    FATAL("error:" STR(__FILE__) ":" STR(__LINE__)": Invalid type.\n");
  }
end:
  return res;
error:
  goto end;
}

const struct str*
get_description_name
  (const struct description* desc)
{
  ASSERT(desc);
  switch (desc->type) {
  case DESC_MAT_SOLID:
    return &desc->d.solid->name;
  case DESC_MAT_SOLID_PROG:
    return &desc->d.solid_prog->name;
  case DESC_MAT_FLUID:
    return &desc->d.fluid->name;
  case DESC_MAT_FLUID_PROG:
    return &desc->d.fluid_prog->name;
  case DESC_BOUND_T_FOR_SOLID:
    return &desc->d.t_boundary->name;
  case DESC_BOUND_T_FOR_SOLID_PROG:
    return &desc->d.t_boundary_prog->name;
  case DESC_BOUND_H_FOR_SOLID:
  case DESC_BOUND_H_FOR_FLUID:
    return &desc->d.h_boundary->name;
  case DESC_BOUND_H_FOR_SOLID_PROG:
  case DESC_BOUND_H_FOR_FLUID_PROG:
    return &desc->d.h_boundary_prog->name;
  case DESC_BOUND_HF_FOR_SOLID:
    return &desc->d.hf_boundary->name;
  case DESC_BOUND_HF_FOR_SOLID_PROG:
    return &desc->d.hf_boundary_prog->name;
  case DESC_BOUND_F_FOR_SOLID:
    return &desc->d.f_boundary->name;
  case DESC_BOUND_F_FOR_SOLID_PROG:
    return &desc->d.f_boundary_prog->name;
  case DESC_SOLID_FLUID_CONNECT:
    return &desc->d.sf_connect->name;
  case DESC_SOLID_FLUID_CONNECT_PROG:
    return &desc->d.sf_connect_prog->name;
  case DESC_SOLID_SOLID_CONNECT:
    return &desc->d.ss_connect->name;
  case DESC_SOLID_SOLID_CONNECT_PROG:
    return &desc->d.ss_connect_prog->name;
  case DESC_PROGRAM:
    return &desc->d.program->name;
  default:
    FATAL("error:" STR(__FILE__) ":" STR(__LINE__)": Invalid type.\n");
  }
}

void
description_get_medium_id
  (const struct description* desc,
   unsigned* id)
{
  ASSERT(desc && id);
  switch (desc->type) {
  case DESC_MAT_SOLID:
    *id = desc->d.solid->solid_id;
    return;
  case DESC_MAT_SOLID_PROG:
    *id = desc->d.solid_prog->solid_id;
    return;
  case DESC_MAT_FLUID:
    *id = desc->d.fluid->fluid_id;
    return;
  case DESC_MAT_FLUID_PROG:
    *id = desc->d.fluid_prog->fluid_id;
    return;
  case DESC_BOUND_H_FOR_SOLID:
  case DESC_BOUND_H_FOR_FLUID:
    *id = desc->d.h_boundary->mat_id;
    return;
  case DESC_BOUND_H_FOR_SOLID_PROG:
  case DESC_BOUND_H_FOR_FLUID_PROG:
    *id = desc->d.h_boundary_prog->mat_id;
    return;
  case DESC_BOUND_HF_FOR_SOLID:
    *id = desc->d.hf_boundary->mat_id;
    return;
  case DESC_BOUND_HF_FOR_SOLID_PROG:
    *id = desc->d.hf_boundary_prog->mat_id;
    return;
  case DESC_BOUND_T_FOR_SOLID:
    *id = desc->d.t_boundary->mat_id;
    return;
  case DESC_BOUND_T_FOR_SOLID_PROG:
    *id = desc->d.t_boundary_prog->mat_id;
    return;
  case DESC_BOUND_F_FOR_SOLID:
    *id = desc->d.f_boundary->mat_id;
    return;
  case DESC_BOUND_F_FOR_SOLID_PROG:
    *id = desc->d.f_boundary_prog->mat_id;
    return;
  case DESC_SOLID_FLUID_CONNECT: /* No medium linked to SF */
  case DESC_SOLID_SOLID_CONNECT: /* No medium linked to SS */
  case DESC_SOLID_FLUID_CONNECT_PROG: /* No medium linked to SF */
  case DESC_SOLID_SOLID_CONNECT_PROG: /* No medium linked to SS */
  case DESC_PROGRAM: /* No medium linked to PRORGRAM */
  default:
    FATAL("error:" STR(__FILE__) ":" STR(__LINE__)": Invalid type.\n");
  }
}
