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

#if !defined(HTABLE_NAME) && !defined(HTABLE_KEY) && !defined(HTABLE_DATA)
#ifndef HASH_TABLE_H
#define HASH_TABLE_H

/* Pre include required headers */
#include "dynamic_array_char.h"
#include "hash.h"
#include "mem_allocator.h"
#include "rsys_math.h"
#include "rsys.h"
#include <string.h>

#endif /* HASH_TABLE_H */
#else
/*
 * Generate the hash table data types and functions with respect to the
 * following macros:
 *  - HTABLE_NAME: prefix of the hash table functions & types;
 *  - HTABLE_DATA: type of the data registered into the hash table;
 *  - HTABLE_KEY: type of the key indexing the hash table data;
 *  - HTABLE_KEY_FUNCTOR_HASH: hash functor on HTABLE_KEY. If not defined, use
 *      the default function that hashes the bytes of HTABLE_KEY;
 *  - HTABLE_KEY_FUNCTOR_EQ: equality functor on 2 HTABLE_KEY;
 *  - HTABLE_<DATA|KEY>_FUNCTOR_INIT: init functor on HTABLE_<DATA|KEY>. If not
 *      defined, no specific treatment is performed on the created data;
 *  - HTABLE_<DATA|KEY>_FUNCTOR_COPY: copy functor on HTABLE_<DATA|KEY>. If not
 *      defined a bitwise copy is used instead;
 *  - HTABLE_<DATA|KEY>_FUNCTOR_RELEASE: release functor on HTABLE_<DATA|KEY>.
 *      If not defined nothing is done on the release of an element;
 *  - HTABLE_<DATA|KEY>_FUNCTOR_COPY_AND_RELEASE: Copy and release of a
 *      HTABLE_<DATA|KEY>. If not defined the copy and the release functors is
 *      used.
 *
 *  The name of the generated types are:
 *    struct htable_<HTABLE_NAME>[_pair|_iterator]
 *
 *  while the generated functions are:
 *    htable_<HTABLE_NAME>_<FUNCTION_NAME>
 *
 *  The hash table collisions are resolved by the "open addressing" strategy
 *  with a fixed linear probing of one.
 */
#if !defined( HTABLE_NAME )
# error "Undefined HTABLE_NAME macro defining the hash table suffix"
#endif
#if !defined( HTABLE_KEY)
# error "Undefined HTABLE_KEY macro defining the hash key type"
#endif
#if !defined( HTABLE_DATA )
# error "Undefined HTABLE_DATA macro defining the hash data type"
#endif

#define HTABLE__ CONCAT(htable_, HTABLE_NAME)
#define HTABLE_PAIR__ CONCAT(CONCAT(HTABLE__, _), pair)
#define HTABLE_ITERATOR__ CONCAT(CONCAT(HTABLE__, _), iterator)
#define HTABLE_FUNC__(Func) \
  CONCAT(CONCAT(CONCAT(CONCAT(htable, _), HTABLE_NAME), _), Func)

/* Internal data structure */
struct HTABLE_PAIR__ {
  HTABLE_DATA data;
  HTABLE_KEY key;
};

/*******************************************************************************
 * Internal default functions
 ******************************************************************************/
#ifndef HTABLE_DATA_FUNCTOR_INIT
static FINLINE void
HTABLE_FUNC__(data_functor_init__)
  (struct mem_allocator* alloc, HTABLE_DATA* data)
{ ASSERT(data); (void)alloc, (void)data; }
#define HTABLE_DATA_FUNCTOR_INIT HTABLE_FUNC__(data_functor_init__)
#endif

#ifndef HTABLE_KEY_FUNCTOR_INIT
static FINLINE void
HTABLE_FUNC__(key_functor_init__)(struct mem_allocator* alloc, HTABLE_KEY* key)
{ ASSERT(key); (void)alloc, (void)key; }
#define HTABLE_KEY_FUNCTOR_INIT HTABLE_FUNC__(key_functor_init__)
#endif

#ifndef HTABLE_DATA_FUNCTOR_RELEASE
static FINLINE void
HTABLE_FUNC__(data_functor_release__)(HTABLE_DATA* data)
{ ASSERT(data); (void)data; }
#define HTABLE_DATA_FUNCTOR_RELEASE HTABLE_FUNC__(data_functor_release__)
#endif

#ifndef HTABLE_KEY_FUNCTOR_RELEASE
static FINLINE void
HTABLE_FUNC__(key_functor_release__)(HTABLE_KEY* key)
{ ASSERT(key); (void)key; }
#define HTABLE_KEY_FUNCTOR_RELEASE HTABLE_FUNC__(key_functor_release__)
#endif

#ifndef HTABLE_DATA_FUNCTOR_COPY
static FINLINE res_T
HTABLE_FUNC__(data_functor_cp__)(HTABLE_DATA* dst, HTABLE_DATA const* src)
{ ASSERT(dst && src); *dst = *src; return RES_OK; }
#define HTABLE_DATA_FUNCTOR_COPY HTABLE_FUNC__(data_functor_cp__)
#endif

#ifndef HTABLE_KEY_FUNCTOR_COPY
static FINLINE res_T
HTABLE_FUNC__(key_functor_cp__)(HTABLE_KEY* dst, HTABLE_KEY const* src)
{ ASSERT(dst && src); *dst = *src; return RES_OK; }
#define HTABLE_KEY_FUNCTOR_COPY HTABLE_FUNC__(key_functor_cp__)
#endif

#ifndef HTABLE_DATA_FUNCTOR_COPY_AND_RELEASE
static FINLINE res_T
HTABLE_FUNC__(data_functor_cp_and_release__)(HTABLE_DATA* dst, HTABLE_DATA* src)
{
  const res_T res = HTABLE_DATA_FUNCTOR_COPY(dst, src);
  HTABLE_DATA_FUNCTOR_RELEASE(src);
  return res;
}
#define HTABLE_DATA_FUNCTOR_COPY_AND_RELEASE \
  HTABLE_FUNC__(data_functor_cp_and_release__)
#endif

#ifndef HTABLE_KEY_FUNCTOR_COPY_AND_RELEASE
static FINLINE res_T
HTABLE_FUNC__(key_functor_cp_and_release__)(HTABLE_KEY* dst, HTABLE_KEY* src)
{
  const res_T res = HTABLE_KEY_FUNCTOR_COPY(dst, src);
  HTABLE_KEY_FUNCTOR_RELEASE(src);
  return res;
}
#define HTABLE_KEY_FUNCTOR_COPY_AND_RELEASE \
  HTABLE_FUNC__(key_functor_cp_and_release__)
#endif

#ifndef HTABLE_KEY_FUNCTOR_HASH
static INLINE size_t
HTABLE_FUNC__(key_functor_hash__)(HTABLE_KEY const* key)
{
#ifdef ARCH_32BITS
  return (size_t)hash_fnv32(key, sizeof(HTABLE_KEY));
#elif defined(ARCH_64BITS)
  return (size_t)hash_fnv64(key, sizeof(HTABLE_KEY));
#else
  #error "Unexpected architecture"
#endif
}
#define HTABLE_KEY_FUNCTOR_HASH HTABLE_FUNC__(key_functor_hash__)
#endif

#ifndef HTABLE_KEY_FUNCTOR_EQ
static INLINE char
HTABLE_FUNC__(key_functor_eq__)(HTABLE_KEY const* a, HTABLE_KEY const* b)
{
  ASSERT(a && b);
  return *a == *b;
}
#define HTABLE_KEY_FUNCTOR_EQ HTABLE_FUNC__(key_functor_eq__)
#endif

/*******************************************************************************
 * Pair functor
 ******************************************************************************/
static INLINE void
HTABLE_FUNC__(pair_init__)
  (struct mem_allocator* allocator, struct HTABLE_PAIR__* pair)
{
  ASSERT(pair);
  HTABLE_KEY_FUNCTOR_INIT(allocator, &pair->key);
  HTABLE_DATA_FUNCTOR_INIT(allocator, &pair->data);
}

static INLINE void
HTABLE_FUNC__(pair_release__)(struct HTABLE_PAIR__* pair)
{
  ASSERT(pair);
  HTABLE_KEY_FUNCTOR_RELEASE(&pair->key);
  HTABLE_DATA_FUNCTOR_RELEASE(&pair->data);
}

static INLINE res_T
HTABLE_FUNC__(pair_copy__)
  (struct HTABLE_PAIR__* dst, struct HTABLE_PAIR__ const* src)
{
  res_T res;
  ASSERT(dst && src);
  if(RES_OK != (res = HTABLE_KEY_FUNCTOR_COPY(&dst->key, &src->key)))
    return res;
  res = HTABLE_DATA_FUNCTOR_COPY(&dst->data, &src->data);
  return res;
}

static INLINE res_T
HTABLE_FUNC__(pair_copy_and_release__)
  (struct HTABLE_PAIR__* dst, struct HTABLE_PAIR__* src)
{
  res_T res;
  ASSERT(dst && src);
  if(RES_OK != (res = HTABLE_KEY_FUNCTOR_COPY_AND_RELEASE(&dst->key, &src->key)))
    return res;
  res = HTABLE_DATA_FUNCTOR_COPY_AND_RELEASE(&dst->data, &src->data);
  return res;
}

/*******************************************************************************
 * Data types
 ******************************************************************************/
#define DARRAY_NAME CONCAT(HTABLE_NAME, __)
#define DARRAY_DATA struct HTABLE_PAIR__
#define DARRAY_FUNCTOR_INIT HTABLE_FUNC__(pair_init__)
#define DARRAY_FUNCTOR_RELEASE HTABLE_FUNC__(pair_release__)
#define DARRAY_FUNCTOR_COPY HTABLE_FUNC__(pair_copy__)
#define DARRAY_FUNCTOR_COPY_AND_RELEASE HTABLE_FUNC__(pair_copy_and_release__)
#include "dynamic_array.h"

#define HTABLE_DATA__ CONCAT(CONCAT(darray_, HTABLE_NAME), __)
#define HTABLE_DATA_FUNC__(Func) CONCAT(CONCAT(HTABLE_DATA__, _), Func)

struct HTABLE__ {
  struct HTABLE_DATA__ table;
  struct darray_char table_slot_is_used;
  size_t table_size_in_use; /* #slots in used */
  struct mem_allocator* allocator;
};

struct HTABLE_ITERATOR__ {
  struct HTABLE__* hash_table;
  size_t slot;
  size_t entries_scanned; /* Used to early stop the next procedure */
};

/*******************************************************************************
 * Internal helper functions
 ******************************************************************************/
static INLINE size_t
HTABLE_FUNC__(find_slot__)(const struct HTABLE__* htbl, HTABLE_KEY const* key)
{
  size_t slot = 0;
  const struct HTABLE_PAIR__* pairs = NULL;
  const char* used_pairs = NULL;
  ASSERT(htbl && key);
  /* The tbl size must be a pow of 2 */
  ASSERT(IS_POW2(HTABLE_DATA_FUNC__(size_get)(&htbl->table)));
  /* The tbl is not full */
  ASSERT(htbl->table_size_in_use < HTABLE_DATA_FUNC__(size_get)(&htbl->table));

  pairs = HTABLE_DATA_FUNC__(cdata_get)(&htbl->table);
  used_pairs = darray_char_cdata_get(&htbl->table_slot_is_used);
  slot =  /* slot = hash % size */
    HTABLE_KEY_FUNCTOR_HASH(key)
  & (HTABLE_DATA_FUNC__(size_get)(&htbl->table) - 1);

  while(used_pairs[slot] && !HTABLE_KEY_FUNCTOR_EQ(key, &pairs[slot].key)) {
    /* slot = (slot + 1) % size */
    slot = (slot + 1) & (HTABLE_DATA_FUNC__(size_get)(&htbl->table) - 1);
  }
  return slot;
}

/*******************************************************************************
 * Hash table API
 ******************************************************************************/
static INLINE void
HTABLE_FUNC__(init)
  ( /* May be NULL <=> use default allocator */
   struct mem_allocator* allocator,
   struct HTABLE__* htbl)
{
  ASSERT(htbl);
  HTABLE_DATA_FUNC__(init)(allocator, &htbl->table);
  darray_char_init(allocator, &htbl->table_slot_is_used);
  htbl->table_size_in_use = 0;
  htbl->allocator = allocator ? allocator : &mem_default_allocator;
}

static INLINE void
HTABLE_FUNC__(clear)(struct HTABLE__* htbl)
{
  size_t islot = 0, in_use = 0;
  ASSERT(htbl);

  FOR_EACH(islot, 0, HTABLE_DATA_FUNC__(size_get)(&htbl->table)) {
    struct HTABLE_PAIR__* pair = NULL;
    if(in_use == htbl->table_size_in_use)
      break; /* Early stop the clear since there is no more slot in htbl */

    if(!darray_char_cdata_get(&htbl->table_slot_is_used)[islot])
      continue;

    darray_char_data_get(&htbl->table_slot_is_used)[islot] = 0;
    pair = HTABLE_DATA_FUNC__(data_get)(&htbl->table) + islot;
    /* Release the pair data and re-init it since it must stay valid for
     * subsequent uses */
    HTABLE_FUNC__(pair_release__)(pair);
    HTABLE_FUNC__(pair_init__)(htbl->allocator, pair);
    ++in_use;
  }
  htbl->table_size_in_use = 0;
}

static INLINE void
HTABLE_FUNC__(release)(struct HTABLE__* htbl)
{
  ASSERT(htbl);
  HTABLE_FUNC__(clear)(htbl);
  HTABLE_DATA_FUNC__(release)(&htbl->table);
  darray_char_release(&htbl->table_slot_is_used);
}

/* Clean up the hash table and, unlike the clear function, ensure that the
 * memory used to store the data is effectively released */
static INLINE void
HTABLE_FUNC__(purge)(struct HTABLE__* htbl)
{
  struct mem_allocator* allocator;
  ASSERT(htbl);
  allocator = htbl->allocator;
  HTABLE_FUNC__(release)(htbl);
  HTABLE_FUNC__(init)(allocator, htbl);
}

static res_T
HTABLE_FUNC__(reserve)(struct HTABLE__* htbl, const size_t size_submitted)
{
  struct HTABLE_DATA__ tbl;
  struct darray_char tbl_slot_is_used;
  struct HTABLE_PAIR__* old_pairs = NULL;
  struct HTABLE_PAIR__* new_pairs = NULL;
  const char* old_used_pairs = NULL;
  char* new_used_pairs = NULL;
  size_t islot = 0;
  size_t in_use = 0;
  size_t size = 0;
  res_T res = RES_OK;
  ASSERT(htbl);

  size = round_up_pow2(size_submitted);
  if(size <= HTABLE_DATA_FUNC__(size_get)(&htbl->table))
    goto exit;

  darray_char_init(htbl->allocator, &tbl_slot_is_used);

  HTABLE_DATA_FUNC__(init)(htbl->allocator, &tbl);
  if(RES_OK != (res = HTABLE_DATA_FUNC__(resize)(&tbl, size)))
    goto error;

  if(RES_OK != (res = darray_char_resize(&tbl_slot_is_used, size)))
    goto error;
  memset(darray_char_data_get(&tbl_slot_is_used), 0, size*sizeof(char));

  /* Rehash the hash table */
  old_pairs = HTABLE_DATA_FUNC__(data_get)(&htbl->table);
  new_pairs = HTABLE_DATA_FUNC__(data_get)(&tbl);
  old_used_pairs = darray_char_cdata_get(&htbl->table_slot_is_used);
  new_used_pairs = darray_char_data_get(&tbl_slot_is_used);
  FOR_EACH(islot, 0, HTABLE_DATA_FUNC__(size_get)(&htbl->table)) {
    size_t islot_new = 0;

    if(in_use == htbl->table_size_in_use)
      break; /* Early stop the rehasing since there is no more slot in htbl */

    if(!old_used_pairs[islot])
      continue;

    islot_new = HTABLE_KEY_FUNCTOR_HASH(&old_pairs[islot].key) & (size - 1);
    while(new_used_pairs[islot_new]) {
      /* There is at most 1 entry for a given key */
      ASSERT(!HTABLE_KEY_FUNCTOR_EQ
        (&new_pairs[islot_new].key, &old_pairs[islot].key ));
      islot_new = (islot_new + 1) & (size - 1); /* (islot_new + 1) % size */
    }

    /* Transfer ownership of old_pairs[islot] data to new_pairs[islot_new].
     * Re-init the old_pairs[islot] structure since it must stay valid */
    HTABLE_FUNC__(pair_copy_and_release__)
      (&new_pairs[islot_new], &old_pairs[islot]);
    HTABLE_FUNC__(pair_init__)(htbl->allocator, &old_pairs[islot]);

    new_used_pairs[islot_new] = 1;
    ++in_use;
  }

  HTABLE_DATA_FUNC__(copy_and_release)(&htbl->table, &tbl);
  darray_char_copy_and_release(&htbl->table_slot_is_used, &tbl_slot_is_used);

exit:
  return res;
error:
  HTABLE_DATA_FUNC__(release)(&tbl);
  darray_char_release(&tbl_slot_is_used);
  goto exit;
}

static INLINE res_T
HTABLE_FUNC__(set)
  ( struct HTABLE__* htbl,
    HTABLE_KEY const* key,
    HTABLE_DATA const* data )
{
  struct HTABLE_PAIR__* pair = NULL;
  size_t tbl_size = 0;
  size_t i = 0;
  res_T res = RES_OK;
  ASSERT(htbl && key && data);

  /* Increase hash table size when the load factor is too high */
  tbl_size = HTABLE_DATA_FUNC__(size_get)(&htbl->table);
  if(htbl->table_size_in_use >= tbl_size * 3/4) {
    if(!tbl_size) {
      res = HTABLE_FUNC__(reserve)(htbl, 32);
    } else {
      res = HTABLE_FUNC__(reserve)(htbl, tbl_size * 2 );
    }
    if(res != RES_OK)
      return res;
  }

  i = HTABLE_FUNC__(find_slot__)(htbl, key);
  pair = HTABLE_DATA_FUNC__(data_get)(&htbl->table) + i;
  if(darray_char_cdata_get(&htbl->table_slot_is_used)[i]) {
    res = HTABLE_DATA_FUNCTOR_COPY(&pair->data, data);
    if(res != RES_OK) {
      darray_char_data_get(&htbl->table_slot_is_used)[i] = 0;
      --htbl->table_size_in_use;
    }
  } else {
    res = HTABLE_KEY_FUNCTOR_COPY(&pair->key, key);
    if(res == RES_OK) {
      res = HTABLE_DATA_FUNCTOR_COPY(&pair->data, data);
      if(res == RES_OK) {
        darray_char_data_get(&htbl->table_slot_is_used)[i] = 1;
        ++htbl->table_size_in_use;
      }
    }
  }
  return res;
}

/* Return the number of erased elements, i.e. 0 or 1 */
static INLINE size_t
HTABLE_FUNC__(erase)(struct HTABLE__* htbl, HTABLE_KEY const* key)
{
  size_t i = 0, j = 0, tbl_size = 0;
  struct HTABLE_PAIR__* pairs = NULL;
  char* used_pairs = NULL;
  ASSERT(htbl && key);

  pairs = HTABLE_DATA_FUNC__(data_get)(&htbl->table);
  tbl_size = HTABLE_DATA_FUNC__(size_get)(&htbl->table);
  if(!tbl_size)
    return 0;
  ASSERT(IS_POW2(tbl_size));

  used_pairs = darray_char_data_get(&htbl->table_slot_is_used);
  i = HTABLE_FUNC__(find_slot__)(htbl, key);
  if(!used_pairs[i])
    return 0; /* No entry */

  /* Remove the entry but does not release it */
  used_pairs[i] = 0;
  --htbl->table_size_in_use;

  for(j = (i + 1) & (tbl_size - 1);  /* <=> j = (i+1) % size */
      used_pairs[j];
      j = (j + 1) & (tbl_size - 1)) { /* <=> j = (j+1) % size */
    const size_t k = HTABLE_KEY_FUNCTOR_HASH(&pairs[j].key) & (tbl_size- 1);

    if(i <= j ? (i < k && k <= j) : (i < k || k <= j))
      continue;

    /* Transfer ownership of pairs[j] data to pairs[i]. Re-init pairs[j] since
     * it must stay valid for subsequent uses */
    HTABLE_FUNC__(pair_copy_and_release__)(&pairs[i], &pairs[j]);
    HTABLE_FUNC__(pair_init__)(htbl->allocator, &pairs[j]);
    used_pairs[i] = 1;
    used_pairs[j] = 0;
    i = j;
  }
  return 1;
}

static INLINE char
HTABLE_FUNC__(is_empty)(const struct HTABLE__* htbl)
{
  ASSERT(htbl);
  return htbl->table_size_in_use == 0;
}

static INLINE HTABLE_DATA*
HTABLE_FUNC__(find)(struct HTABLE__* htbl, HTABLE_KEY const* key)
{
  size_t i = 0;
  ASSERT(htbl && key);
  if(HTABLE_FUNC__(is_empty)(htbl))
    return NULL;

  i = HTABLE_FUNC__(find_slot__)(htbl, key);
  if(darray_char_cdata_get(&htbl->table_slot_is_used)[i]) {
    return &HTABLE_DATA_FUNC__(data_get)(&htbl->table)[i].data;
  } else {
    return NULL;
  }
}

static INLINE size_t
HTABLE_FUNC__(size_get)(const struct HTABLE__* htbl)
{
  ASSERT(htbl);
  return htbl->table_size_in_use;
}

static INLINE res_T
HTABLE_FUNC__(copy)(struct HTABLE__* dst, const struct HTABLE__* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);

  res = HTABLE_DATA_FUNC__(copy)(&dst->table, &src->table);
  if(res != RES_OK) goto error;

  res = darray_char_copy(&dst->table_slot_is_used, &src->table_slot_is_used);
  if(res != RES_OK) goto error;

  dst->table_size_in_use = src->table_size_in_use;

exit:
  return res;
error:
  HTABLE_FUNC__(clear)(dst);
  goto exit;
}

static INLINE res_T
HTABLE_FUNC__(copy_and_clear)(struct HTABLE__* dst, struct HTABLE__* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  if(dst == src) {
    HTABLE_FUNC__(clear)(dst);
    return RES_OK;
  }

  res = HTABLE_DATA_FUNC__(copy_and_clear)(&dst->table, &src->table);
  if(res != RES_OK) goto error;

  res = darray_char_copy_and_clear
    (&dst->table_slot_is_used, &src->table_slot_is_used);
  if(res != RES_OK) goto error;

  dst->table_size_in_use = src->table_size_in_use;
  src->table_size_in_use = 0;

exit:
  return res;
error:
  HTABLE_FUNC__(clear)(dst);
  goto exit;
}

static INLINE res_T
HTABLE_FUNC__(copy_and_release)(struct HTABLE__*dst, struct HTABLE__* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  if(dst == src) {
    HTABLE_FUNC__(release)(dst);
  } else {
    res = HTABLE_FUNC__(copy_and_clear)(dst, src);
    if(res == RES_OK)
      HTABLE_FUNC__(release)(src);
  }
  return res;
}

static INLINE void
HTABLE_FUNC__(begin)
  (struct HTABLE__* htbl,
   struct HTABLE_ITERATOR__* it)
{
  const char* used_pairs = NULL;
  size_t i = 0;
  size_t tbl_size = 0;
  ASSERT(htbl && it);

  tbl_size = HTABLE_DATA_FUNC__(size_get)(&htbl->table);
  used_pairs = darray_char_cdata_get(&htbl->table_slot_is_used);

  it->slot = it->entries_scanned = 0;
  for(i = 0; i < tbl_size && !used_pairs[i]; ++i) {}
  it->slot = i;
  it->entries_scanned = i < tbl_size;
  it->hash_table = htbl;
}

static INLINE void
HTABLE_FUNC__(end)
  (struct HTABLE__* htbl,
   struct HTABLE_ITERATOR__* it)
{
  ASSERT(htbl && it);
  it->slot = HTABLE_DATA_FUNC__(size_get)(&htbl->table);
  it->entries_scanned = htbl->table_size_in_use;
  it->hash_table = htbl;
}

static INLINE void
HTABLE_FUNC__(find_iterator)
  (struct HTABLE__* htbl,
   HTABLE_KEY const* key,
   struct HTABLE_ITERATOR__* it)
{
  size_t i = 0;
  ASSERT(htbl && key && it);
  if(HTABLE_FUNC__(is_empty)(htbl)) {
    HTABLE_FUNC__(end)(htbl, it);
    return;
  }

  i = HTABLE_FUNC__(find_slot__)(htbl, key);
  if(!darray_char_cdata_get(&htbl->table_slot_is_used)[i]) {
    HTABLE_FUNC__(end)(htbl, it);
  } else {
    it->slot = i;
    it->hash_table = htbl;
    it->entries_scanned = 0;
  }
}

static INLINE void
HTABLE_FUNC__(iterator_next)(struct HTABLE_ITERATOR__* it)
{
  size_t tbl_size = 0;
  ASSERT(it);

  tbl_size = HTABLE_DATA_FUNC__(size_get)(&it->hash_table->table);
  if(it->entries_scanned >= it->hash_table->table_size_in_use) {/* Early stop */
    it->slot = tbl_size;
  } else {
    size_t i = 0;
    FOR_EACH(i, it->slot + 1, tbl_size)
      if(darray_char_cdata_get(&it->hash_table->table_slot_is_used)[i]) break;
    it->slot = i;
    it->entries_scanned += i < tbl_size;
  }
}

static INLINE char
HTABLE_FUNC__(iterator_eq)
  (const struct HTABLE_ITERATOR__* it0,
   const struct HTABLE_ITERATOR__* it1)
{
  ASSERT(it0 && it1);
  /* Do not compare the 'entries_scanned' field used only to early stop the
   * iterator_next function in some situations */
  return
     it0->slot == it1->slot
  && it0->hash_table == it1->hash_table;
}

static INLINE HTABLE_KEY*
HTABLE_FUNC__(iterator_key_get)(const struct HTABLE_ITERATOR__* it)
{
  ASSERT(it && it->slot < HTABLE_DATA_FUNC__(size_get)(&it->hash_table->table));
  return &HTABLE_DATA_FUNC__(data_get)(&it->hash_table->table)[it->slot].key;
}

static INLINE HTABLE_DATA*
HTABLE_FUNC__(iterator_data_get)(const struct HTABLE_ITERATOR__* it)
{
  ASSERT(it && it->slot < HTABLE_DATA_FUNC__(size_get)(&it->hash_table->table));
  return &HTABLE_DATA_FUNC__(data_get)(&it->hash_table->table)[it->slot].data;
}

#undef HTABLE_KEY
#undef HTABLE_KEY_FUNCTOR_COPY
#undef HTABLE_KEY_FUNCTOR_COPY_AND_RELEASE
#undef HTABLE_KEY_FUNCTOR_EQ
#undef HTABLE_KEY_FUNCTOR_HASH
#undef HTABLE_KEY_FUNCTOR_INIT
#undef HTABLE_KEY_FUNCTOR_RELEASE
#undef HTABLE_DATA
#undef HTABLE_DATA_FUNCTOR_COPY
#undef HTABLE_DATA_FUNCTOR_COPY_AND_RELEASE
#undef HTABLE_DATA_FUNCTOR_INIT
#undef HTABLE_DATA_FUNCTOR_RELEASE
#undef HTABLE_NAME
#undef HTABLE__
#undef HTABLE_PAIR__
#undef HTABLE_ITERATOR__
#undef HTABLE_FUNC__

#undef HTABLE_DATA__
#undef HTABLE_DATA_FUNC__

#endif /* !HTABLE_NAME || !HTABLE_KEY || !HTABLE_DATA  */
