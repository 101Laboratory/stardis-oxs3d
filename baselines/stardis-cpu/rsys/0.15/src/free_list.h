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

#ifndef FITEM_TYPE
/*
 * Declare regular free list types and macros. May be include before any free
 * list declaration
 */
# ifndef FREE_LIST_H
# define FREE_LIST_H

#include "rsys.h"

/* A free list item must be a structure with a FITEM field */
#define FITEM                                                                  \
  struct {                                                                     \
    struct fid id;                                                             \
    uint32_t next;                                                             \
    uint32_t prev;                                                             \
  } fitem__

/* Unique identifier of a free list item */
struct fid {
  uint32_t index; /* Index into the free list */
  uint32_t name; /* Unique id that identifies this item */
};

static const struct fid FID_NULL = { UINT32_MAX, UINT32_MAX };

/* Helper macros that defines if a free list identifier is NULL or not */
#define IS_FID_NULL(Fid) ((Fid).index == UINT32_MAX)

/* Compare 2 free list identifiers */
#define FID_EQ(Fid0, Fid1) \
  ((Fid0).index == (Fid1).index && (Fid0).name == (Fid1).name)

/* Helper macro that iterates over the items of the free list */
#define FLIST_FOR_EACH(Item, List)                                             \
  for((Item) = (List)->tail != UINT32_MAX                                      \
        ? (List)->items + (List)->tail : NULL;                                 \
      (Item) != NULL;                                                          \
      (Item) = (Item)->fitem__.prev != UINT32_MAX                              \
        ? (List)->items + (Item)->fitem__.prev : NULL)

# endif /* FREE_LIST_H */

#else
/*
 * Free list API generated with respect to the FITEM_TYPE macro.
 *
 * The name of the generated free list type is:
 *  struct flist_<FITEM_TYPE>
 *
 *  while each generated `function' respect the following naming convention:
 *    flist_<FITEM_TYPE>_<function>
 */
#define FLIST_FUNC__(Func) CONCAT(CONCAT(CONCAT(flist_, FITEM_TYPE), _), Func)
#define FLIST_TYPE__ CONCAT(flist_, FITEM_TYPE)

#include "mem_allocator.h"
#include <string.h>

struct FLIST_TYPE__ {
  uint32_t head; /* Index toward the first free item */
  uint32_t tail; /* Index toward the last created item */
  uint32_t name_next; /* Next unique free list item name */
  struct FITEM_TYPE* items;
  uint32_t nitems;
  struct mem_allocator* allocator;
};

static FINLINE void
FLIST_FUNC__(init)
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   struct FLIST_TYPE__* list)
{
  ASSERT(list);
  list->head = UINT32_MAX;
  list->tail = UINT32_MAX;
  list->name_next = 0;
  list->items = NULL;
  list->nitems = 0;
  list->allocator = allocator ? allocator : &mem_default_allocator;
}

static FINLINE void
FLIST_FUNC__(release)(struct FLIST_TYPE__* list)
{
  ASSERT(list);
  MEM_RM(list->allocator, list->items);
}

static FINLINE char
FLIST_FUNC__(hold)(struct FLIST_TYPE__* list, struct fid id)
{
  ASSERT(list);
  return id.index < list->nitems
      && list->items[id.index].fitem__.id.name == id.name;
}

static FINLINE struct FITEM_TYPE*
FLIST_FUNC__(get)(struct FLIST_TYPE__* list, struct fid id)
{
  ASSERT(list);
  if(FLIST_FUNC__(hold)(list, id)) {
    return list->items + id.index;
  } else {
    return NULL;
  }
}

static INLINE struct fid
FLIST_FUNC__(add)(struct FLIST_TYPE__* list)
{
  struct fid id;
  ASSERT(list);

  id.name = list->name_next++;
  if(list->head != UINT32_MAX) {
    id.index = list->head;
    list->head = list->items[list->head].fitem__.next;
  } else { /* Increase the free list size */
    const uint32_t nitems_new = list->nitems ? list->nitems * 2 : 16;
    uint32_t iitem = 0;
    struct FITEM_TYPE item;
    memset(&item, 0, sizeof(struct FITEM_TYPE));

    id.index = list->nitems;
    list->items = (struct FITEM_TYPE*)MEM_REALLOC
      (list->allocator,
       list->items,
       nitems_new * sizeof(struct FITEM_TYPE));
    if(!list->items)
      FATAL("Unsufficient memory\n");
    FOR_EACH(iitem, list->nitems, nitems_new) {
      list->items[iitem].fitem__.prev = iitem - 1;
      list->items[iitem].fitem__.next = iitem + 1;
      list->items[iitem].fitem__.id.name = UINT32_MAX;
    }
    list->items[nitems_new - 1].fitem__.next = UINT32_MAX;
    list->head = list->nitems + 1;
    list->nitems = nitems_new;
  }
  list->items[id.index].fitem__.id = id;

  /* Add the item in the linked list of valid element */
  if(list->tail != UINT32_MAX)
    list->items[list->tail].fitem__.next = id.index;
  list->items[id.index].fitem__.prev = list->tail;
  list->items[id.index].fitem__.next = UINT32_MAX;
  list->tail = id.index;
  return id;
}

static FINLINE void
FLIST_FUNC__(del)(struct FLIST_TYPE__* list, struct fid id)
{
  ASSERT(list);
  if(FLIST_FUNC__(hold)(list, id)) {
     struct FITEM_TYPE* item = FLIST_FUNC__(get)(list, id);

     /* Unlink the item from the double linked list of valid items */
     if(item->fitem__.prev != UINT32_MAX)
       list->items[item->fitem__.prev].fitem__.next = item->fitem__.next;
     if(item->fitem__.next != UINT32_MAX) {
       list->items[item->fitem__.next].fitem__.prev = item->fitem__.prev;
     } else {
       ASSERT(item->fitem__.id.index == list->tail);
       list->tail = item->fitem__.prev;
     }

     /* Add the item to the single linked list of free items */
     item->fitem__.next = list->head;
     list->head = item->fitem__.id.index;
     item->fitem__.id = FID_NULL;
  }
}

static FINLINE void
FLIST_FUNC__(clear)(struct FLIST_TYPE__* list)
{
  uint32_t iitem;
  ASSERT(list);
  FOR_EACH(iitem, 0, list->nitems) {
    list->items[iitem].fitem__.next = iitem + 1;
    list->items[iitem].fitem__.prev = iitem - 1;
    list->items[iitem].fitem__.id.name = UINT32_MAX;
  }
  if(!list->nitems) {
    list->head = UINT32_MAX;
  } else {
    list->head = 0;
    list->items[list->nitems - 1].fitem__.next = UINT32_MAX;
  }
  list->tail = UINT32_MAX;
}

static FINLINE struct fid
CONCAT(FITEM_TYPE, _id_get)(const struct FITEM_TYPE* item)
{
  ASSERT(item);
  return item->fitem__.id;
}

static FINLINE int
FLIST_FUNC__(is_empty)(struct FLIST_TYPE__* list)
{
  ASSERT(list);
  return list->tail == UINT32_MAX;
}

#undef FLIST_TYPE__
#undef FLIST_FUNC__
#undef FITEM_TYPE

#endif /* ifdef FITEM_TYPE */
