/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#if !defined(BUFFER_NAME) && !defined(BUFFER_DARRAY)

#ifndef S2D_BUFFER_H
#define S2D_BUFFER_H

#include<rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#endif /* S2D_BUFFER_H */
#else
/*
 * Generate the buffer type with respect to the following macros:
 *  - BUFFER_NAME: name of the structure and prefix of the functions;
 *  - BUFFER_DARRAY: type of the dynamic array of the buffer;
 */
#if !defined(BUFFER_NAME) || !defined(BUFFER_DARRAY)
  #error "Missing macro definition"
#endif

#define BUFFER_FUNC__(Func) CONCAT(CONCAT(BUFFER_NAME, _), Func)
#define BUFFER_DARRAY_FUNC__(Func) CONCAT(CONCAT(BUFFER_DARRAY, _), Func)

struct BUFFER_NAME {
  struct BUFFER_DARRAY data;
  struct mem_allocator* allocator;
  ref_T ref;
};

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static INLINE void
BUFFER_FUNC__(release__)(ref_T* ref)
{
  struct BUFFER_NAME* buffer;
  ASSERT(ref);
  buffer = CONTAINER_OF(ref, struct BUFFER_NAME, ref);
  BUFFER_DARRAY_FUNC__(release)(&buffer->data);
  MEM_RM(buffer->allocator, buffer);
}

/*******************************************************************************
 * Buffer function
 ******************************************************************************/
static INLINE res_T
BUFFER_FUNC__(create)
  (struct mem_allocator* allocator,
   struct BUFFER_NAME** out_buffer)
{
  struct BUFFER_NAME* buffer;
  ASSERT(allocator && out_buffer);

  buffer = (struct BUFFER_NAME*)MEM_CALLOC
    (allocator, 1, sizeof(struct BUFFER_NAME));
  if(!buffer) return RES_MEM_ERR;
  BUFFER_DARRAY_FUNC__(init)(allocator, &buffer->data);
  buffer->allocator = allocator;
  ref_init(&buffer->ref);
  *out_buffer = buffer;
  return RES_OK;
}

static INLINE void
BUFFER_FUNC__(ref_get)(struct BUFFER_NAME* buffer)
{
  ASSERT(buffer);
  ref_get(&buffer->ref);
}

static INLINE void
BUFFER_FUNC__(ref_put)(struct BUFFER_NAME* buffer)
{
  ASSERT(buffer);
  ref_put(&buffer->ref, BUFFER_FUNC__(release__));
}

#undef BUFFER_NAME
#undef BUFFER_DARRAY

#endif /* !BUFFER_NAME || !BUFFER_DARRAY */

