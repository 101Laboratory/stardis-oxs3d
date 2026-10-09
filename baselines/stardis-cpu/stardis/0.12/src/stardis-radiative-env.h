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

#ifndef STARDIS_RADIATIVE_ENV_H
#define STARDIS_RADIATIVE_ENV_H

#include "stardis-default.h"
#include <rsys/rsys.h>

/* Forward declaration */
struct stardis;

enum radiative_env_type {
  RADIATIVE_ENV_CONST,
  RADIATIVE_ENV_PROG
};

struct radiative_env_const {
  double temperature;
  double reference_temperature;
};
#define RADIATIVE_ENV_CONST_DEFAULT__ {                                        \
  STARDIS_DEFAULT_TRAD,                                                        \
  STARDIS_DEFAULT_TRAD_REFERENCE                                               \
}
static const struct radiative_env_const RADIATIVE_ENV_CONST_DEFAULT =
  RADIATIVE_ENV_CONST_DEFAULT__;

struct radiative_env_prog {
  struct str prog_name;
  struct program* program;
  void* data; /* Pointer toward the program data */
  struct mem_allocator* allocator;

  /* Input arguments */
  size_t argc;
  char** argv;

  void*
  (*create)
    (const struct stardis_description_create_context* ctx,
     void* lib_data,
     size_t argc,
     char* argv[]);

  void
  (*release)
    (void* data);

  double
  (*temperature)
    (const double time, /* [s] */
     const double dir[3],
     void* data);

  double
  (*reference_temperature)
    (const double time, /* [s] */
     const double dir[3],
     void* data);

  double*
  (*t_range)
    (void* data,
     double t_range[2]);
};
#define RADIATIVE_ENV_PROG_NULL__ \
  {{0}, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL}
static const struct radiative_env_prog RADIATIVE_ENV_PROG_NULL =
  RADIATIVE_ENV_PROG_NULL__;

struct radiative_env {
  enum radiative_env_type type;
  union {
    struct radiative_env_const cst;
    struct radiative_env_prog prg;
  } data;
  struct sdis_radiative_env* sdis_radenv;
};
#define RADIATIVE_ENV_DEFAULT__ {                                              \
  RADIATIVE_ENV_CONST,                                                         \
  {RADIATIVE_ENV_CONST_DEFAULT__},                                             \
  NULL                                                                         \
}
static const struct radiative_env RADIATIVE_ENV_DEFAULT = RADIATIVE_ENV_DEFAULT__;

extern LOCAL_SYM res_T
radiative_env_init_const
  (struct mem_allocator* allocator,
   struct radiative_env* radenv);

extern LOCAL_SYM res_T
radiative_env_init_prog
  (struct mem_allocator* allocator,
   struct radiative_env* radenv);

extern LOCAL_SYM void
radiative_env_release
  (struct radiative_env* radenv);

extern LOCAL_SYM res_T
radiative_env_create_solver_radiative_env
  (struct radiative_env* radenv,
   struct stardis* stardis);

#endif /* STARDIS_RADIATIVE_ENV_H */
