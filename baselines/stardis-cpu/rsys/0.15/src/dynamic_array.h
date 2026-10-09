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

#if !defined(DARRAY_NAME) && !defined(DARRAY_DATA)
#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H

#include "rsys_math.h"
#include "mem_allocator.h"
#include "rsys.h"
#include <string.h>

/* Helper macro to quickly access darray internal buffer */
#define DARRAY_BUF(Darray) (Darray)->data

#endif /* DYNAMIC_ARRAY_H */
#else
/*
 * Generate the dynamic array data type and functions with respect to the
 * following macros:
 *  - DARRAY_NAME: prefix of the dynamic array functions & types;
 *  - DARRAY_DATA: type of the data registered into the array;
 *  - DARRAY_ALIGNMENT: alignment of the array address. Must be a power of two.
 *      If not defined, used the default system alignment.
 *  - DARRAY_FUNCTOR_INIT: init functor on DARRAY_DATA. If not defined, no
 *      specific treatment is performed on the created data;
 *  - DARRAY_FUNCTOR_COPY: copy functor on DARRAY_DATA. If not defined a
 *      bitwise copy is used instead;
 *  - DARRAY_FUNCTOR_RELEASE: release functor on DARRAY_DATA. If not defined
 *      nothing is done on the release of an element;
 *  - DARRAY_FUNCTOR_COPY_AND_RELEASE: Copy and release of a DARRAY_DATA. If
 *      not defined the copy and the release functors is used.
 *
 *  The name of the generated type is: struct darray_<DARRAY_NAME>
 *
 *  while the generated functions are: darray_<DARRAY_NAME>_<FUNCTION_NAME>
 */
#ifndef DARRAY_NAME
  #error "Missing the DARRAY_NAME macro defining the structure name"
#endif
#ifndef DARRAY_DATA
  #error "Missing the DARRAY_DATA macro defining the array data type"
#endif

#define DARRAY_FUNC__(Func) CONCAT(CONCAT(CONCAT(darray_, DARRAY_NAME),_), Func)
#define DARRAY_TYPE__ CONCAT(darray_, DARRAY_NAME)

#ifndef DARRAY_ALIGNMENT
  #define DARRAY_ALIGNMENT__ MMAX(ALIGNOF(DARRAY_DATA), 16)
#else
  STATIC_ASSERT(IS_POW2(DARRAY_ALIGNMENT),
    DARRAY_ALIGNMENT_must_be_a_power_of_2);
  #define DARRAY_ALIGNMENT__\
    MMAX(ALIGNOF(DARRAY_DATA), MMAX(DARRAY_ALIGNMENT, 16))
#endif

struct DARRAY_TYPE__ {
  DARRAY_DATA* data;
  size_t size;
  size_t capacity;
  struct mem_allocator* allocator;
};

/*******************************************************************************
 * Internal default functors
 ******************************************************************************/
#ifndef DARRAY_FUNCTOR_INIT
static FINLINE void
DARRAY_FUNC__(functor_init__)(struct mem_allocator* alloc, DARRAY_DATA* data)
{ ASSERT(data); (void)alloc, (void)data; }
#define DARRAY_FUNCTOR_INIT DARRAY_FUNC__(functor_init__)
#endif

#ifndef DARRAY_FUNCTOR_RELEASE
static FINLINE void
DARRAY_FUNC__(functor_release__)(DARRAY_DATA* data)
{ ASSERT(data); (void)data; }
#define DARRAY_FUNCTOR_RELEASE DARRAY_FUNC__(functor_release__)
#endif

#ifndef DARRAY_FUNCTOR_COPY
static FINLINE res_T
DARRAY_FUNC__(functor_cp__)(DARRAY_DATA* dst, DARRAY_DATA const* src)
{ ASSERT(dst && src); *dst = *src; return RES_OK; }
#define DARRAY_FUNCTOR_COPY DARRAY_FUNC__(functor_cp__)
#endif

#ifndef DARRAY_FUNCTOR_COPY_AND_RELEASE
static FINLINE res_T
DARRAY_FUNC__(functor_cp_and_release__)(DARRAY_DATA* dst, DARRAY_DATA* src)
{
  const res_T res = DARRAY_FUNCTOR_COPY(dst, src);
  DARRAY_FUNCTOR_RELEASE(src);
  return res;
}
#define DARRAY_FUNCTOR_COPY_AND_RELEASE DARRAY_FUNC__(functor_cp_and_release__)
#endif

/*******************************************************************************
 * Dynamic array API
 ******************************************************************************/
static INLINE void
DARRAY_FUNC__(init)
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   struct DARRAY_TYPE__* darray)
{
  size_t i;
  ASSERT(darray);
  darray->data = NULL;
  darray->size = 0;
  darray->capacity = 0;
  FOR_EACH(i, 0, darray->capacity)
    DARRAY_FUNCTOR_INIT(allocator, darray->data + i);
  darray->allocator = allocator ? allocator : &mem_default_allocator;
}

static INLINE void
DARRAY_FUNC__(clear)(struct DARRAY_TYPE__* darray)
{
  size_t i;
  ASSERT(darray);
  FOR_EACH(i, 0, darray->size)
    DARRAY_FUNCTOR_RELEASE(darray->data + i);
  darray->size = 0;
}

static INLINE void
DARRAY_FUNC__(release)(struct DARRAY_TYPE__* darray)
{
  ASSERT(darray);
  DARRAY_FUNC__(clear)(darray);
  MEM_RM(darray->allocator, darray->data);
}

/* Clean up the array and, unlike the clear function, ensure that the memory
 * used to store the data is effectively released. */
static INLINE void
DARRAY_FUNC__(purge)(struct DARRAY_TYPE__* darray)
{
  struct mem_allocator* allocator;
  ASSERT(darray);
  allocator = darray->allocator;
  DARRAY_FUNC__(release)(darray);
  DARRAY_FUNC__(init)(allocator, darray);
}

static INLINE res_T
DARRAY_FUNC__(reserve)(struct DARRAY_TYPE__* darray, const size_t sz)
{
  DARRAY_DATA* data = NULL;
  ASSERT(darray);

  if(sz <= darray->capacity)
    return RES_OK;

  data = (DARRAY_DATA*)MEM_ALLOC_ALIGNED
    (darray->allocator, sz * sizeof(DARRAY_DATA), DARRAY_ALIGNMENT__);
  if(!data) return RES_MEM_ERR;

  if(darray->size) {
    size_t i = 0;
    FOR_EACH(i, 0, darray->size) {
      res_T res = 0;
      DARRAY_FUNCTOR_INIT(darray->allocator, data+i);
      res = DARRAY_FUNCTOR_COPY_AND_RELEASE(data+i, darray->data+i);
      if(res != RES_OK) {
        MEM_RM(darray->allocator, data);
        return res;
      }
    }
  }
  MEM_RM(darray->allocator, darray->data);

  darray->data = data;
  darray->capacity = sz;
  return RES_OK;
}

static INLINE res_T
DARRAY_FUNC__(resize)(struct DARRAY_TYPE__* darray, const size_t sz)
{
  size_t sz_adjusted;
  size_t i;
  res_T res;
  ASSERT(darray);

  if(sz < darray->capacity) {
    sz_adjusted = sz;
  } else if(sz < darray->size*2) {
    sz_adjusted = darray->size*2;
  } else {
    sz_adjusted = sz;
  }

  res = DARRAY_FUNC__(reserve)(darray, sz_adjusted);
  if(res != RES_OK) return res;

  if(sz < darray->size) {
    FOR_EACH(i, sz, darray->size)
      DARRAY_FUNCTOR_RELEASE(darray->data+i);
  } else if(darray->size < sz) {
    FOR_EACH(i, darray->size, sz)
      DARRAY_FUNCTOR_INIT(darray->allocator, darray->data+i);
  }
  darray->size = sz;
  return RES_OK;
}

static INLINE res_T
DARRAY_FUNC__(push_back)
  (struct DARRAY_TYPE__* darray,
   DARRAY_DATA const* data)
{
  DARRAY_DATA* dst;
  size_t sz_adjusted;
  res_T res;
  ASSERT(darray && data);

  sz_adjusted = darray->size + 1;
  if(sz_adjusted > darray->capacity) {
    sz_adjusted = MMAX(darray->capacity*2, 1);
  }
  res = DARRAY_FUNC__(reserve)(darray, sz_adjusted);
  if(res != RES_OK) return res;
  dst = darray->data + darray->size;
  DARRAY_FUNCTOR_INIT(darray->allocator, dst);
  DARRAY_FUNCTOR_COPY(dst, data);
  ++darray->size;
  return RES_OK;
}

static INLINE void
DARRAY_FUNC__(pop_back)(struct DARRAY_TYPE__* darray)
{
  ASSERT(darray);
  if(darray->size > 0) {
    const res_T res = DARRAY_FUNC__(resize)(darray, darray->size - 1);
    ASSERT(res == RES_OK); (void)res;
  }
}

static INLINE size_t
DARRAY_FUNC__(size_get)(const struct DARRAY_TYPE__* darray)
{
  ASSERT(darray);
  return darray->size;
}

static INLINE size_t
DARRAY_FUNC__(capacity)(const struct DARRAY_TYPE__* darray)
{
  ASSERT(darray);
  return darray->capacity;
}

static INLINE DARRAY_DATA*
DARRAY_FUNC__(data_get)(struct DARRAY_TYPE__* darray)
{
  ASSERT(darray);
  return darray->data;
}

static INLINE DARRAY_DATA const*
DARRAY_FUNC__(cdata_get)(const struct DARRAY_TYPE__* darray)
{
  ASSERT(darray);
  return darray->data;
}

static INLINE res_T
DARRAY_FUNC__(copy)(struct DARRAY_TYPE__* dst, const struct DARRAY_TYPE__* src)
{
  DARRAY_DATA const* src_data = NULL;
  size_t i, src_sz, dst_sz, sz_adjusted;
  res_T res;
  ASSERT(dst && src);

  if(dst == src)
    return RES_OK;

  DARRAY_FUNC__(clear)(dst);
  src_sz = DARRAY_FUNC__(size_get)(src);
  dst_sz = DARRAY_FUNC__(size_get)(dst);

  sz_adjusted = MMAX(src_sz, dst_sz);
  res = DARRAY_FUNC__(reserve)(dst, sz_adjusted);
  if(res != RES_OK) return res;

  src_data = DARRAY_FUNC__(cdata_get)(src);
  FOR_EACH(i, 0, src_sz)
    DARRAY_FUNC__(push_back)(dst, src_data+i);
  return RES_OK;
}

static INLINE res_T
DARRAY_FUNC__(copy_and_clear)
  (struct DARRAY_TYPE__* dst,
   struct DARRAY_TYPE__* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  if(dst == src) {
    DARRAY_FUNC__(clear)(dst);
    return RES_OK;
  }

  if(src->allocator == dst->allocator) {
    /* Give the ownership of src->data to dst */
    DARRAY_FUNC__(clear)(dst);
    MEM_RM(dst->allocator, dst->data);
    dst->data = src->data;
    dst->capacity = src->capacity;
    dst->size = src->size;
    DARRAY_FUNC__(init)(src->allocator, src);  /* Reset src */
  } else {
    DARRAY_DATA* src_data = NULL;
    DARRAY_DATA* dst_data = NULL;
    const size_t src_sz = DARRAY_FUNC__(size_get)(src);
    size_t i = 0;

    DARRAY_FUNC__(clear)(dst);
    res = DARRAY_FUNC__(resize)(dst, src_sz);
    if(res != RES_OK) return res;

    src_data = DARRAY_FUNC__(data_get)(src);
    dst_data = DARRAY_FUNC__(data_get)(dst);
    FOR_EACH(i, 0, src_sz) {
      res = DARRAY_FUNCTOR_COPY_AND_RELEASE(dst_data+i, src_data+i);
      if(res != RES_OK) return res;
    }
    src->size = 0;
  }
  return res;
}

static INLINE res_T
DARRAY_FUNC__(copy_and_release)
  (struct DARRAY_TYPE__* dst,
   struct DARRAY_TYPE__* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  if(dst == src) {
    DARRAY_FUNC__(release)(dst);
  } else {
    res = DARRAY_FUNC__(copy_and_clear)(dst, src);
    if(res == RES_OK)
      DARRAY_FUNC__(release)(src);
  }
  return res;
}

static INLINE res_T
DARRAY_FUNC__(swap)(struct DARRAY_TYPE__* a, struct DARRAY_TYPE__* b)
{
  res_T res = RES_OK;
  ASSERT(a && b);

  /* Ensure that `a' point toward the smallest array */
  if(DARRAY_FUNC__(size_get)(a) > DARRAY_FUNC__(size_get)(b)) {
    SWAP(struct DARRAY_TYPE__*, a, b);
  }

  if(a->allocator != b->allocator) {
    struct DARRAY_TYPE__ tmp;
    DARRAY_FUNC__(init)(a->allocator, &tmp);

    res = DARRAY_FUNC__(copy_and_clear)(&tmp, b);
    if(res != RES_OK) return res;
    res = DARRAY_FUNC__(copy_and_clear)(b, a);
    if(res != RES_OK) return res;
    res = DARRAY_FUNC__(copy_and_release)(a, &tmp);
    if(res != RES_OK) return res;

  } else {
    /* Back-up the data of `b' */
    DARRAY_DATA* b_data = b->data;
    const size_t b_capacity = b->capacity;
    const size_t b_size = b->size;

    /* Reset `b' and copy `a' into `b' */
    DARRAY_FUNC__(init)(b->allocator, b);
    res = DARRAY_FUNC__(copy_and_clear)(b, a);
    ASSERT(res == RES_OK);

    /* Give the ownership of `b' data to `a' */
    a->data = b_data;
    a->capacity = b_capacity;
    a->size = b_size;
  }
  return RES_OK;
}

#undef DARRAY_ALIGNMENT
#undef DARRAY_NAME
#undef DARRAY_DATA
#undef DARRAY_FUNCTOR_INIT
#undef DARRAY_FUNCTOR_RELEASE
#undef DARRAY_FUNCTOR_COPY
#undef DARRAY_FUNCTOR_COPY_AND_RELEASE
#undef DARRAY_ALIGNMENT__
#undef DARRAY_FUNC__
#undef DARRAY_TYPE__

#endif /* !DARRAY_NAME || !DARRAY_TYPE */
