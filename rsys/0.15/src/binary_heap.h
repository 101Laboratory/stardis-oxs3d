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

#if !defined(BHEAP_DATA) && !defined(BHEAP_NAME)
#ifndef BINARY_HEAP_H
#define BINARY_HEAP_H

#include "rsys.h"
#include "dynamic_array.h"

#endif /* BINARY_HEAP_H */
#else
/*
 * Generate the binary heap data type and functions with respect to the
 * following macros:
 *  - BHEAP_NAME: prefix of the binary heap type and its functions;
 *  - BHEAP_DATA: type of the data registered into the binary heap;
 *  - BHEAP_FUNCTOR_COMPARE: comparison functor on binary heap data. Its
 *      profile is int (*)(const BHEAP_DATA* , const BHEAP_DATA* ). It returns
 *      an integer less than, equal to, or greater than zero if the first
 *      argument is considered to be sorted respectively before, at the same
 *      position, or after the second. If not defined the `<' operator is used
 *      to sort the BHEAP_DATA;
 *  - BHEAP_FUNCTOR_INIT: init functor on BHEAP_DATA. If not defined, no
 *      specific treatment is performed on the created data;
 *  - BHEAP_FUNCTOR_COPY: copy functor on DHEAP_DATA. If not defined a
 *      bitwise copy is used instead;
 *  - BHEAP_FUNCTOR_RELEASE: release functor on BHEAP_DATA. If not defined
 *      nothing is done on the release of an element;
 *  - BHEAP_FUNCTOR_COPY_AND_RELEASE: copy and release of a DHEAP_DATA. If
 *      not defined the copy and the release functors is used.
 *
 *  The name of the generated type is: struct bheap_<BHEAP_NAME>
 *  while the generated functions are: bheap_<BHEAP_NAME>_<FUNCTION_NAME>
 */
#ifndef BHEAP_NAME
  #error "Missing the BHEAP_NAME macro defining the structure name"
#endif
#ifndef BHEAP_DATA
  #error "Missing the BHEAP_DATA macro defining the heap data type"
#endif

#define DARRAY_NAME CONCAT(BHEAP_NAME, __)
#define DARRAY_DATA BHEAP_DATA
#ifdef BHEAP_FUNCTOR_INIT
  #define DARRAY_FUNCTOR_INIT BHEAP_FUNCTOR_INIT
#endif
#ifdef BHEAP_FUNCTOR_COPY
  #define DARRAY_FUNCTOR_COPY BHEAP_FUNCTOR_COPY
#endif
#ifdef BHEAP_FUNCTOR_RELEASE
  #define DARRAY_FUNCTOR_RELEASE BHEAP_FUNCTOR_RELEASE
#endif
#ifdef BHEAP_FUNCTOR_COPY_AND_RELEASE
  #define DARRAY_FUNCTOR_COPY_AND_RELEASE BHEAP_FUNCTOR_COPY_AND_RELEASE
#endif
#include "dynamic_array.h"

/* Helper macros */
#define BHEAP_FUNC__(Func) CONCAT(CONCAT(CONCAT(bheap_, BHEAP_NAME), _), Func)
#define BHEAP_TYPE__ CONCAT(bheap_, BHEAP_NAME)
#define BHEAP_NODES__ CONCAT(CONCAT(darray_, BHEAP_NAME), __)
#define BHEAP_NODES_FUNC__(Func) CONCAT(CONCAT(BHEAP_NODES__, _), Func)

struct BHEAP_TYPE__ {
  struct BHEAP_NODES__ nodes;
};

/*******************************************************************************
 * Internal default functors
 ******************************************************************************/
#ifndef BHEAP_FUNCTOR_COMPARE
static FINLINE int
BHEAP_FUNC__(functor_cmp__)(const BHEAP_DATA* a, const BHEAP_DATA* b)
{
  ASSERT(a && b);
  if(*a < *b) return -1;
  if(*a > *b) return 1;
  return 0;
}
#define BHEAP_FUNCTOR_COMPARE BHEAP_FUNC__(functor_cmp__)
#endif

#ifndef BHEAP_FUNCTOR_COPY
  #define BHEAP_FUNCTOR_COPY BHEAP_NODES_FUNC__(functor_cp__)
#endif
#ifndef BHEAP_FUNCTOR_INIT
  #define BHEAP_FUNCTOR_INIT BHEAP_NODES_FUNC__(functor_init__)
#endif
#ifndef BHEAP_FUNCTOR_RELEASE
  #define BHEAP_FUNCTOR_RELEASE BHEAP_NODES_FUNC__(functor_release__)
#endif

/*******************************************************************************
 * Binary heap API
 ******************************************************************************/
static INLINE void
BHEAP_FUNC__(init)
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   struct BHEAP_TYPE__* heap)
{
  ASSERT(heap);
  BHEAP_NODES_FUNC__(init)(allocator, &heap->nodes);
}

static INLINE  void
BHEAP_FUNC__(release)(struct BHEAP_TYPE__* heap)
{
  ASSERT(heap);
  BHEAP_NODES_FUNC__(release)(&heap->nodes);
}

static INLINE void
BHEAP_FUNC__(clear)(struct BHEAP_TYPE__* heap)
{
  ASSERT(heap);
  BHEAP_NODES_FUNC__(clear)(&heap->nodes);
}

static INLINE char
BHEAP_FUNC__(is_empty)(struct BHEAP_TYPE__* heap)
{
  ASSERT(heap);
  return BHEAP_NODES_FUNC__(size_get)(&heap->nodes) == 0;
}

static INLINE res_T
BHEAP_FUNC__(insert)(struct BHEAP_TYPE__* heap, const BHEAP_DATA* value)
{
  BHEAP_DATA* nodes = NULL;
  BHEAP_DATA tmp;
  size_t inode = 0;
  res_T res;

  ASSERT(heap);
  inode = BHEAP_NODES_FUNC__(size_get)(&heap->nodes);

  res = BHEAP_NODES_FUNC__(push_back)(&heap->nodes, value);
  if(res != RES_OK)
    return res;

  nodes = BHEAP_NODES_FUNC__(data_get)(&heap->nodes);
  BHEAP_FUNCTOR_INIT(heap->nodes.allocator, &tmp);
  while(inode) {
    const size_t iparent = (inode - 1) / 2;
    if(BHEAP_FUNCTOR_COMPARE(&nodes[iparent], &nodes[inode]) <= 0)
      break;

    BHEAP_FUNCTOR_COPY(&tmp, &nodes[iparent]);
    BHEAP_FUNCTOR_COPY(&nodes[iparent], &nodes[inode]);
    BHEAP_FUNCTOR_COPY(&nodes[inode], &tmp);
    inode = iparent;
  }
  BHEAP_FUNCTOR_RELEASE(&tmp);
  return RES_OK;
}

static INLINE char
BHEAP_FUNC__(top)(struct BHEAP_TYPE__* heap, BHEAP_DATA* top)
{
  ASSERT(heap);

  if(BHEAP_FUNC__(is_empty)(heap))
    return 0;

  BHEAP_FUNCTOR_COPY(top, &BHEAP_NODES_FUNC__(data_get)(&heap->nodes)[0]);
  return 1;
}

static INLINE char
BHEAP_FUNC__(pop)(struct BHEAP_TYPE__* heap, BHEAP_DATA* top)
{
  BHEAP_DATA* nodes = NULL;
  BHEAP_DATA tmp;
  size_t size = 0;
  size_t inode = 0;
  size_t ichild = 0;
  char res = 1;
  ASSERT(heap);

  BHEAP_FUNCTOR_INIT(heap->nodes.allocator, &tmp);

  if(BHEAP_FUNC__(is_empty)(heap)) {
    res = 0;
    goto exit;
  }

  nodes = BHEAP_NODES_FUNC__(data_get)(&heap->nodes);
  BHEAP_FUNCTOR_COPY(top, &nodes[0]);
  inode = BHEAP_NODES_FUNC__(size_get)(&heap->nodes) - 1;
  BHEAP_FUNCTOR_COPY(&tmp, &nodes[inode]);
  BHEAP_NODES_FUNC__(pop_back)(&heap->nodes);
  if(!inode) {
    res = 1;
    goto exit;
  }

  nodes = BHEAP_NODES_FUNC__(data_get)(&heap->nodes);
  BHEAP_FUNCTOR_COPY(&nodes[0], &tmp);
  size = inode;
  for(inode=0, ichild=1; ichild < size; inode = ichild, ichild = 2*inode + 1) {

    if(ichild + 1 < size
    && BHEAP_FUNCTOR_COMPARE(&nodes[ichild], &nodes[ichild+1]) > 0)
      ++ichild;

    if(BHEAP_FUNCTOR_COMPARE(&nodes[inode], &nodes[ichild]) <= 0)
      break;

    BHEAP_FUNCTOR_COPY(&tmp, &nodes[inode]);
    BHEAP_FUNCTOR_COPY(&nodes[inode], &nodes[ichild]);
    BHEAP_FUNCTOR_COPY(&nodes[ichild], &tmp);
    inode = ichild;
  }
exit:
  BHEAP_FUNCTOR_RELEASE(&tmp);
  return res;
}

#undef BHEAP_NAME
#undef BHEAP_DATA
#undef BHEAP_FUNCTOR_COMPARE
#undef BHEAP_FUNCTOR_INIT
#undef BHEAP_FUNCTOR_COPY
#undef BHEAP_FUNCTOR_RELEASE
#undef BHEAP_FUNCTOR_COPY_AND_RELEASE
#undef BHEAP_FUNC__
#undef BHEAP_TYPE__
#undef BHEAP_NODES__
#undef BHEAP_NODES_FUNC__

#endif /* !BHEAP_NAME || !BHEAP_DATA */
