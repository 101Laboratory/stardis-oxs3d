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

#ifndef MEM_ALLOCATOR_H
#define MEM_ALLOCATOR_H

#include "rsys.h"
#include <stddef.h>

/*******************************************************************************
 * Memory allocator interface
 ******************************************************************************/
struct mem_allocator {
  void* (*alloc)
    (void* data,
     const size_t size,
     const char* filename,
     const unsigned int fileline);

  void* (*calloc)
    (void* data,
     const size_t nbelmts,
     const size_t size,
     const char* filename,
     const unsigned int fileline);

  void* (*realloc)
    (void* data,
     void* mem,
     const size_t size,
     const char* filename,
     const unsigned int fileline);

  void* (*alloc_aligned)
    (void* data,
     const size_t size,
     const size_t alignment,
     const char* filename,
     const unsigned int fileline);

  void (*rm)
    (void* data,
     void* mem);

  size_t (*mem_size)
    (void* data,
     void* mem);

  size_t (*allocated_size)
    (const void* data);

  size_t (*dump) /* Return the real dump len (without the null char) */
    (const void* data,
     char* dump,
     const size_t max_dump_len); /* Include the null char */

  void* data;
};

/*******************************************************************************
 * Helper macros
 ******************************************************************************/
#define MEM_ALLOC(Allocator, Size)                                             \
  ((Allocator)->alloc((Allocator)->data, (Size), __FILE__, __LINE__))

#define MEM_CALLOC(Allocator, Nb, Size)                                        \
  ((Allocator)->calloc((Allocator)->data, (Nb), (Size), __FILE__, __LINE__))

#define MEM_REALLOC(Allocator, Mem, Size)                                      \
  ((Allocator)->realloc((Allocator)->data, (Mem), (Size), __FILE__, __LINE__))

#define MEM_ALLOC_ALIGNED(Allocator, Size, Alignment)                          \
  ((Allocator)->alloc_aligned                                                  \
   ((Allocator)->data, (Size), (Alignment), __FILE__, __LINE__))

#define MEM_RM(Allocator, Mem)                                                 \
  ((Allocator)->rm((Allocator)->data, (void*)(Mem)))

#define MEM_SIZE(Allocator, Mem)                                               \
  ((Allocator)->mem_size((Allocator)->data, (Mem)))

#define MEM_ALLOCATED_SIZE(Allocator)                                          \
  ((Allocator)->allocated_size((Allocator)->data))

#define MEM_DUMP(Allocator, Msg, MaxLen)                                       \
  ((Allocator)->dump((Allocator)->data, (Msg), (MaxLen)))


BEGIN_DECLS

/* Default allocator. */
RSYS_API struct mem_allocator mem_default_allocator;

/*******************************************************************************
 * Regular allocation functions.
 ******************************************************************************/
RSYS_API void* mem_alloc(const size_t size);
RSYS_API void* mem_calloc(const size_t nelmts, const size_t size);
RSYS_API void* mem_realloc(void* ptr, const size_t size);
RSYS_API void* mem_alloc_aligned(const size_t size, const size_t alignment);
RSYS_API void mem_rm(void* ptr);
RSYS_API size_t mem_size(void* ptr);
RSYS_API size_t mem_allocated_size(void);

/*******************************************************************************
 * Proxy allocator - Register the filename and the fileline of the allocation.
 ******************************************************************************/
RSYS_API res_T
mem_init_proxy_allocator
  (struct mem_allocator* proxy,
   struct mem_allocator* allocator);

RSYS_API void
mem_shutdown_proxy_allocator
  (struct mem_allocator* proxy_allocator);

/*******************************************************************************
 * Regular allocator - Wrap the regular allocation functions.
 ******************************************************************************/
RSYS_API res_T
mem_init_regular_allocator
  (struct mem_allocator* allocator);

RSYS_API void
mem_shutdown_regular_allocator
  (struct mem_allocator* allocator);

/*******************************************************************************
 * LIFO allocator - Allocate the memory in a preallocated memory chunk wrt to a
 * LIFO pattern; the last allocated entry is the first that can be deallocated.
 * If the entry to delete is not on top of the LIFO stack, it is marked as
 * freed and will be effectively removed when it will be on top of the LIFO
 * stack.
 ******************************************************************************/
RSYS_API res_T
mem_init_lifo_allocator
  (struct mem_allocator* lifo_allocator,
   struct mem_allocator* allocator,
   const size_t size); /* Overall size that can be allocated by the allocator */

RSYS_API void
mem_shutdown_lifo_allocator
  (struct mem_allocator* lifo_allocator);

END_DECLS

#endif /* MEM_ALLOCATOR_H */
