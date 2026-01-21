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

#ifndef STRETCHY_ARRAY_H
#define STRETCHY_ARRAY_H

#include "rsys_math.h"
#include "mem_allocator.h"

/*
 * Port of the suckless Sean T. Barret's stretchy buffer. Stretchy arrays can
 * be use on data types that does not rely on an init or release process and
 * can be bitwise copied. Refer to dynamic_array for more advanced data types
 */

/*******************************************************************************
 * Internal macros
 ******************************************************************************/
#define sa_raw__(A) ((size_t*)(A)-2)
#define sa_capacity__(A) sa_raw__(A)[0]
#define sa_size__(A) sa_raw__(A)[1]
#define sa_need_grow__(A, N) ((A)==NULL || sa_size__(A) + (N) >= sa_capacity__(A))
#define sa_may_be_grow__(A, N) (sa_need_grow__(A, (N)) ? sa_grow__(A, N) : 0)
#define sa_grow__(A, N) \
  (*(void**)(&A) = sa_grow_func__((void*)(A), (N), sizeof(*(A))))

/*******************************************************************************
 * Stretchy buffer API
 ******************************************************************************/
/* Free `Array' memory */
#define sa_release(Array) {                                                    \
    if(Array)                                                                  \
      mem_rm(sa_raw__(Array));                                                 \
  } (void)0

/* Push back `Val' in `Array' */
#define sa_push(Array, Val) {                                                  \
    sa_may_be_grow__(Array, 1);                                                \
    (Array)[sa_size__(Array)++] = (Val);                                       \
  } (void)0

/* Clean up `Array' but does not release its memory */
#define sa_clear(Array) {                                                      \
    if(Array)                                                                  \
      sa_size__(Array) = 0;                                                    \
  } (void)0

/* Add `N' uninitialized items to `Array'. Return a pointer to the 1st added */
#define sa_add(Array, N)                                                       \
  (sa_may_be_grow__(Array, N),                                                 \
   sa_size__(Array) += (N),                                                    \
   &(Array)[sa_size__(Array) - (N)])

/* Return a lvalue of the last item in `Array' */
#define sa_last(Array) ((Array)[sa_size__(Array)-1])

/* Return the number of items in `Array' */
#define sa_size(Array) ((Array) ? sa_size__(Array) : 0)

/*******************************************************************************
 * Helper internal function
 ******************************************************************************/
static INLINE void*
sa_grow_func__(void* array, const size_t increment, const size_t itemsize)
{
  size_t dbl_capacity = array ? 2 * sa_capacity__(array) : 0;
  size_t min_needed_capacity = MMAX(sa_size(array) + increment, 32);
  size_t new_capacity = MMAX(dbl_capacity, min_needed_capacity);
  size_t sizeof_array = itemsize * new_capacity + sizeof(size_t)*2;
  size_t* new_array;
  ASSERT(itemsize);

  if(array) {
    new_array = (size_t*)mem_realloc(sa_raw__(array), sizeof_array);
  } else {
    new_array = (size_t*)mem_alloc_aligned(sizeof_array, 16);
  }

  if(!new_array)
    FATAL("Unsufficient memory\n");

  if(!array) new_array[1] = 0;
  new_array[0] = new_capacity;
  return new_array+2;
}

#endif /* STRETCHY_ARRAY_H */
