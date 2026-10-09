/* Copyright (C) 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SWF_TABULATION_H
#define SWF_TABULATION_H

#include <rsys/dynamic_array.h>
#include <rsys/ref_count.h>

struct item {
  double x; /* Function argument */
  double fx; /* Value of the function */
  double dfx; /* Value of the derivative of the function */
  double d2fx; /* Value of the second derivative of the function */
};
#define ITEM_NULL__ {0, 0, 0, 0}
static const struct item ITEM_NULL = ITEM_NULL__;

/* Declare the dynamic array of items */
#define DARRAY_NAME item
#define DARRAY_DATA struct item
#include <rsys/dynamic_array.h>

struct swf_tabulation {
  struct darray_item items;
  struct mem_allocator* allocator;
  ref_T ref;
};

extern LOCAL_SYM res_T
tabulation_create
  (struct mem_allocator* allocator,
   struct swf_tabulation** out_tab);

#endif /* SWF_TABULATION_H */
