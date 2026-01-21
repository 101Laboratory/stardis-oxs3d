/* Copyright (C) 2013-2023, 2025 Vincent Forest (vaplv@free.fr)
 *
 * The RSys library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The RSys library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the RSys library. If not, see <http://www.gnu.org/licenses/>. */

#ifndef ALGORITHM_H
#define ALGORITHM_H

#include "rsys.h"

/* Search the first element in an array of `nmemb' objects pointed by `base',
 * the first member that is *not* less than `key'. The size of each member of
 * the array is specified by `size'. The array should be in ascending order
 * according to the `compar' functor. This function is expected to have two
 * arguments which point to the `key' object and an array member, in that
 * order, and should return an integer less than, equal to, or greater than
 * zero if the key object is found, respectively, to be less than, to match, or
 * be greater than the array member. Return NULL if all array members compare
 * less than `key'. */
static INLINE void*
search_lower_bound
  (const void* key,
   const void* base,
   size_t nmemb,
   size_t size,
   int (*compar)(const void*, const void*))
{
#define AT(Base, Id) (void*)((const char*)(Base) + (Id)*size)
  size_t lo = 0, hi = nmemb - 1;
  while(lo < hi) {
    size_t mid = (lo + hi) / 2;
    if(compar(key, AT(base, mid)) > 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return compar(key, AT(base, lo)) > 0 ? NULL : AT(base, lo);
#undef AT
}

#endif /* ALGORITHM_H */
