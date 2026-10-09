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

#include "swf.h"
#include "swf_tabulation.h"

#include <rsys/rsys.h>

#ifndef SWF_DIMENSION
  #error "The SWF_DIMENSION macro must be defined"
#endif

#if SWF_DIMENSION != 2 && SWF_DIMENSION != 3
  #error "Invalid SWF_DIMEMSION"
#endif

#define Xd(Name) CONCAT(CONCAT(Name, SWF_DIMENSION), d)

/*******************************************************************************
 * Helper generic dimension functions
 ******************************************************************************/
static INLINE double
Xd(H_inverse)
  (const double y,
   const double x_min,
   const double x_max,
   const double epsilon)
{
  double x0 = x_min;
  double x1 = x_max;
  ASSERT(y >= 0 && y < 1 && x_min < x_max && epsilon > 0);

  if(y == 0) return 0;

  while(x1 - x0 > epsilon) {
    const double x = (x0 + x1) * 0.5;
    const double f_x = Xd(H)(x, NULL, NULL);

    if(f_x < y) {
      x0 = x;
    } else {
      x1 = x;
    }
  }
  return (x0 + x1) * 0.5;
}

static INLINE res_T
Xd(H_tabulate)
  (const struct swf_H_tabulate_args* args,
   struct swf_tabulation** out_tab)
{
  struct swf_tabulation* tab = NULL;
  size_t i = 0;
  double x = 0;
  res_T res = RES_OK;

  if(!out_tab) { res = RES_BAD_ARG; goto error; }

  if((res = check_swf_H_tabulate_args(args)) != RES_OK) goto error;
  if((res = tabulation_create(args->allocator, &tab)) != RES_OK) goto error;

  /* Setup the tabulation */
  i = 0;
  for(x = args->x_min; x <= args->x_max; x += x*args->step) {
    const struct item* pitem = darray_item_cdata_get(&tab->items) + i - 1;
    struct item item = ITEM_NULL;

    /* Evaluate H(x) and its derivatives up to order 2 */
    item.x = x;
    item.fx = Xd(H)(item.x, &item.dfx, &item.d2fx);

    if(i > 0) {
      /* Detect oscillations due to numerical problems.
       * These oscillations occur close to 0 */
      if(item.fx < pitem->fx) {
        res = RES_BAD_OP;
        goto error;
      }
      /* Do not tabulate argument whose value is (numerically) equal to the
       * previous one: it would artificially add staircase steps */
      if(item.fx == pitem->fx) {
        continue;
      }
    }

    ++i;

    res = darray_item_push_back(&tab->items, &item);
    if(res != RES_OK) goto error;
  }
  ASSERT(darray_item_size_get(&tab->items) == i);

  if(args->normalize) {
    struct item* items = darray_item_data_get(&tab->items);
    const size_t nitems = darray_item_size_get(&tab->items);
    const double norm = items[nitems - 1].fx;

    /* Normalize the tabulation */
    FOR_EACH(i, 0, nitems) items[i].fx /= norm;
  }

exit:
  if(out_tab) *out_tab = tab;
  return res;
error:
  if(tab) {
    SWF(tabulation_ref_put(tab));
    tab = NULL;
  }
  goto exit;
}

#undef SWF_DIMENSION
#undef Xd
