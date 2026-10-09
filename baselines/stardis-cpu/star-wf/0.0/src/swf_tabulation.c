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

#include <rsys/algorithm.h>
#include <rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
release_tabulation(ref_T* ref)
{
  struct swf_tabulation* tab = CONTAINER_OF(ref, struct swf_tabulation, ref);
  ASSERT(ref);
  darray_item_release(&tab->items);
  MEM_RM(tab->allocator, tab);
}

static FINLINE int
cmp_fx(const void* a, const void* b)
{
  const double f_a = *((const double*)a);
  const double f_b = ((const struct item*)b)->fx;
  return f_a < f_b ? -1 : (f_a > f_b ? 1 : 0);
}

static INLINE double
linear_interpolation
  (const double y,
   const struct item* a,
   const struct item* b)
{
  double u, x;
  ASSERT(a && b);
  u = (y - a->fx) / (b->fx - a->fx);
  x = u*(b->x - a->x) + a->x;
  return x;
}

/* H(x) = H(x0) + H'(x0)*(x-x0) + 0.5*H"(x0)*(x-x0)^2
 * x = x0 + (-H'(x0) +/-) sqrt(H'(x0)^2 - 2*H"(x0)*(H(x0) - H(x)))) / H"(x0)) */
static INLINE double
quadratic_extrapolation
  (const double fx,
   const struct item* a,
   const struct item* b)
{
  double x;
  double d0, d1, x0, x1, y0, y1;
  ASSERT(a && b);

  d0 = a->dfx*a->dfx - 2*a->d2fx*(a->fx - fx);
  d1 = b->dfx*b->dfx - 2*b->d2fx*(b->fx - fx);
  ASSERT(d0 && d1);

  y0 = (-a->dfx + sqrt(d0)) / (a->d2fx);
  y1 = (-b->dfx + sqrt(d1)) / (b->d2fx);
  x0 = a->x + y0;
  x1 = b->x + y1;

  x = x0 - a->x > b->x - x1 ? x1 : x0;
  ASSERT(a->x <= x && x <= b->x);

  return x;
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
res_T
swf_tabulation_ref_get(struct swf_tabulation* tab)
{
  if(!tab) return RES_BAD_ARG;
  ref_get(&tab->ref);
  return RES_OK;
}

res_T
swf_tabulation_ref_put(struct swf_tabulation* tab)
{
  if(!tab) return RES_BAD_ARG;
  ref_put(&tab->ref, release_tabulation);
  return RES_OK;
}

double
swf_tabulation_inverse
  (const struct swf_tabulation* tab,
   const enum swf_prediction prediction,
   const double y)
{
  const struct item* items = NULL;
  const struct item* find = NULL;
  double x = 0; /* Argument corresponding to input value y */
  size_t nitems = 0;
  ASSERT(tab && y >= 0 && y < 1);
  ASSERT(prediction == SWF_LINEAR || prediction == SWF_QUADRATIC);

  items = darray_item_cdata_get(&tab->items);
  nitems = darray_item_size_get(&tab->items);
  ASSERT(nitems);

  find = search_lower_bound(&y, items, nitems, sizeof(*items), cmp_fx);

  if(y == 0) return 0;

  /* Input y is not in the tabulated range: returns the nearest x */
  if(!find) return items[nitems-1].x;
  if(find == items) return find->x;

  ASSERT(find->fx >= y);

  /* The input y correspond exactly to a tabulated argument */
  if(find->fx == y) return find->x;

  ASSERT(find > items && find[-1].fx < y);

  /* Predict x values from of tabulated arguments */
  switch(prediction) {
    case SWF_LINEAR:
      x = linear_interpolation(y, &find[-1], &find[0]);
      break;
    case SWF_QUADRATIC:
      x = quadratic_extrapolation(y, &find[-1], &find[0]);
      break;
    default: FATAL("Unreachable code\n"); break;
  }

  return x;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
tabulation_create
  (struct mem_allocator* mem_allocator,
   struct swf_tabulation** out_tab)
{
  struct swf_tabulation* tab = NULL;
  struct mem_allocator* allocator = NULL;
  res_T res = RES_OK;
  ASSERT(out_tab);

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  tab = MEM_CALLOC(allocator, 1, sizeof(*tab));
  if(!tab) { res = RES_MEM_ERR; goto error; }
  ref_init(&tab->ref);
  tab->allocator = allocator;
  darray_item_init(allocator, &tab->items);

exit:
  *out_tab = tab;
  return res;
error:
  if(tab) {
    SWF(tabulation_ref_put(tab));
    tab = NULL;
  }
  goto exit;
}
