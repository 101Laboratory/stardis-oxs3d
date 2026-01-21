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

#ifndef STARDIS_DESCRIPTION_H
#define STARDIS_DESCRIPTION_H

#include <rsys/rsys.h>
#include <rsys/dynamic_array.h>
#include <rsys/str.h>

#include <sdis.h>

#include <limits.h>

/* Forward declarations */
struct mem_allocator;

/* Different types of descriptions */
enum description_type {
  DESC_MAT_SOLID,
  DESC_MAT_FLUID,
  DESC_BOUND_H_FOR_FLUID,
  DESC_BOUND_H_FOR_SOLID,
  DESC_BOUND_T_FOR_SOLID,
  DESC_BOUND_F_FOR_SOLID,
  DESC_SOLID_FLUID_CONNECT,
  DESC_SOLID_SOLID_CONNECT,
  DESC_MAT_SOLID_PROG,
  DESC_MAT_FLUID_PROG,
  DESC_BOUND_H_FOR_FLUID_PROG,
  DESC_BOUND_H_FOR_SOLID_PROG,
  DESC_BOUND_HF_FOR_SOLID,
  DESC_BOUND_HF_FOR_SOLID_PROG,
  DESC_BOUND_T_FOR_SOLID_PROG,
  DESC_BOUND_F_FOR_SOLID_PROG,
  DESC_SOLID_FLUID_CONNECT_PROG,
  DESC_SOLID_SOLID_CONNECT_PROG,
  DESC_PROGRAM,
  DESCRIPTION_TYPE_COUNT__
};

#define DESC_IS_H(D) \
  ((D)->type == DESC_BOUND_H_FOR_SOLID || (D)->type == DESC_BOUND_H_FOR_FLUID \
    || (D)->type == DESC_BOUND_H_FOR_SOLID_PROG \
    || (D)->type == DESC_BOUND_H_FOR_FLUID_PROG)
#define DESC_IS_HF(D) \
  ((D)->type == DESC_BOUND_HF_FOR_SOLID\
    || (D)->type == DESC_BOUND_HF_FOR_SOLID_PROG)
#define DESC_IS_T(D) \
  ((D)->type == DESC_BOUND_T_FOR_SOLID \
    || (D)->type == DESC_BOUND_T_FOR_SOLID_PROG)
#define DESC_IS_F(D) \
  ((D)->type == DESC_BOUND_F_FOR_SOLID \
    || (D)->type == DESC_BOUND_F_FOR_SOLID_PROG)
#define DESC_IS_SOLID(D) \
  ((D)->type == DESC_MAT_SOLID || (D)->type == DESC_MAT_SOLID_PROG)
#define DESC_IS_FLUID(D) \
  ((D)->type == DESC_MAT_FLUID || (D)->type == DESC_MAT_FLUID_PROG)
#define DESC_IS_MEDIUM(D) \
  (DESC_IS_SOLID(D) || DESC_IS_FLUID(D))
#define DESC_IS_BOUNDARY(D) \
 (DESC_IS_H(D) || DESC_IS_T(D) || DESC_IS_F(D))
#define DESC_IS_SOLID_FLUID(D) \
  ((D)->type == DESC_SOLID_FLUID_CONNECT \
    || (D)->type == DESC_SOLID_FLUID_CONNECT_PROG)
#define DESC_IS_SOLID_SOLID(D) \
  ((D)->type == DESC_SOLID_SOLID_CONNECT \
    || (D)->type == DESC_SOLID_SOLID_CONNECT_PROG)
#define DESC_IS_CONNECTION(D) \
  (DESC_IS_SOLID_FLUID(D) || DESC_IS_SOLID_SOLID(D))
#define DESC_IS_PROG(D) \
  ((D)->type == DESC_MAT_SOLID_PROG || (D)->type == DESC_MAT_FLUID_PROG \
    || (D)->type == DESC_BOUND_H_FOR_FLUID_PROG \
    || (D)->type == DESC_BOUND_H_FOR_SOLID_PROG \
    || (D)->type == DESC_BOUND_HF_FOR_SOLID_PROG \
    || (D)->type == DESC_BOUND_T_FOR_SOLID_PROG \
    || (D)->type == DESC_BOUND_F_FOR_SOLID_PROG \
    || (D)->type == DESC_SOLID_FLUID_CONNECT_PROG \
    || (D)->type == DESC_SOLID_SOLID_CONNECT_PROG)

/******************************************************************************/

struct fluid;
struct fluid_prog;
struct solid;
struct solid_prog;
struct t_boundary;
struct t_boundary_prog;
struct f_boundary;
struct f_boundary_prog;
struct h_boundary;
struct h_boundary_prog;
struct hf_boundary;
struct hf_boundary_prog;
struct solid_fluid_connect;
struct solid_fluid_connect_prog;
struct solid_solid_connect;
struct solid_solid_connect_prog;
struct program;

struct description {
  enum description_type type;
  union {
    struct fluid* fluid;
    struct fluid_prog* fluid_prog;
    struct solid* solid;
    struct solid_prog* solid_prog;
    struct t_boundary* t_boundary;
    struct t_boundary_prog* t_boundary_prog;
    struct f_boundary* f_boundary;
    struct f_boundary_prog* f_boundary_prog;
    struct h_boundary* h_boundary;
    struct h_boundary_prog* h_boundary_prog;
    struct hf_boundary* hf_boundary;
    struct hf_boundary_prog* hf_boundary_prog;
    struct solid_fluid_connect* sf_connect;
    struct solid_fluid_connect_prog* sf_connect_prog;
    struct solid_solid_connect* ss_connect;
    struct solid_solid_connect_prog* ss_connect_prog;
    struct program* program;
  } d;
};

/******************************************************************************/
res_T
init_description
  (struct mem_allocator* alloc,
   struct description* desc);

void
release_description
  (struct description* desc,
   struct mem_allocator* allocator);

res_T
str_print_description
  (struct str* str,
   const unsigned rank,
   const struct description* desc);

const struct str*
get_description_name
  (const struct description* desc);

void
description_get_medium_id
  (const struct description* desc,
   unsigned* id);

#endif
