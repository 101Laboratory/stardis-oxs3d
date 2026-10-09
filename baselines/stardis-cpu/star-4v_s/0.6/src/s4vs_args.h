/* Copyright (C) 2015-2018, 2021, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef S4VS_ARGS_H
#define S4VS_ARGS_H

#include <rsys/rsys.h>

struct s4vs_args {
  const char* filename; /* May be NULL <=> stdin */
  unsigned long nrealisations;
  double ks;
};
static const struct s4vs_args S4VS_ARGS_DEFAULT = {NULL, 10000, 0.0};

extern LOCAL_SYM res_T
s4vs_args_init
  (struct s4vs_args* args,
   int argc,
   char** argv);

extern LOCAL_SYM void
s4vs_args_release
  (struct s4vs_args* args);

#endif /* S4VS_ARGS_H */
