/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_GREEN_H
#define SDIS_GREEN_H

#include <rsys/rsys.h>

/* Forward declaration */
struct sdis_green_function;
struct green_path;

struct green_path_handle {
  struct sdis_green_function* green;
  struct green_path* path;
};
#define GREEN_PATH_HANDLE_NULL__ {NULL, NULL}
static const struct green_path_handle GREEN_PATH_HANDLE_NULL =
  GREEN_PATH_HANDLE_NULL__;

extern LOCAL_SYM res_T
green_function_create
  (struct sdis_device* dev,
   struct sdis_green_function** green);

/* Merge `src' into `dst' an clear `src' */
extern LOCAL_SYM res_T
green_function_merge_and_clear
  (struct sdis_green_function* dst,
   struct sdis_green_function* src);

/* Finalize the green function state (e.g.: computes the #paths & #failures) */
extern LOCAL_SYM res_T
green_function_finalize
  (struct sdis_green_function* green);

extern LOCAL_SYM res_T
green_function_create_path
  (struct sdis_green_function* green,
   struct green_path_handle* handle);

extern LOCAL_SYM res_T
green_path_set_limit_interface_fragment
  (struct green_path_handle* path,
   struct sdis_interface* interf,
   const struct sdis_interface_fragment* fragment);

extern LOCAL_SYM res_T
green_path_set_limit_vertex
  (struct green_path_handle* path,
   struct sdis_medium* mdm,
   const struct sdis_rwalk_vertex* vertex);

extern LOCAL_SYM res_T
green_path_add_power_term
  (struct green_path_handle* path,
   struct sdis_medium* mdm,
   const struct sdis_rwalk_vertex* vertex,
   const double term);

extern LOCAL_SYM res_T
green_path_add_flux_term
  (struct green_path_handle* path,
   struct sdis_interface* interf,
   const struct sdis_interface_fragment* fragment,
   const double term);

#endif /* SDIS_GREEN_H */

