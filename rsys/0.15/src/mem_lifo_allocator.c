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

#define _POSIX_C_SOURCE 200112L /* snprintf support */

#include "rsys_math.h"
#include "mem_allocator.h"
#include "mutex.h"
#include <string.h>

struct lifo_data {
  char* top;
  char* stack;
  size_t remain;
  size_t capacity;
  struct mem_allocator* allocator;
  struct mutex* mutex;
};

/*
 * Stack entry memory layout:
 * +----+-----------+-----------+------+----------+--------+-----------+
 * |... | A: 16bits | B: 48bits | DATA | C: 15bit | D: 1bit| E: 48bits |
 * +----+-----------+-----------+------+----------+--------+-----------+
 *  \___________Header_________/        \____________Footer___________/
 *
 *    A: size of the header in bytes
 *    B: size of DATA in bytes
 *    C: dummy data
 *    D: define whether the entry is in used or not
 *    E: size of DATA in bytes
 */

#define LIFO_DEFAULT_ALIGNMENT 8

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void*
lifo_alloc_aligned
  (void* data,
   const size_t size,
   const size_t align,
   const char* filename,
   const unsigned int fileline)
{
  struct lifo_data* lifo = data;
  size_t align_adjusted;
  size_t header_size;
  size_t footer_size;
  size_t data_size;
  size_t entry_size;
  char* mem;
  int64_t header;
  int64_t footer;
  ASSERT(data);
  (void)filename, (void)fileline;

  if(!IS_POW2(align) || align > 32768)
    return NULL;

  align_adjusted = align < LIFO_DEFAULT_ALIGNMENT
    ? LIFO_DEFAULT_ALIGNMENT : align;

  mutex_lock(lifo->mutex);
  { /* Critical section */
    intptr_t data_addr = (intptr_t)(lifo->top + sizeof(int64_t));
    data_addr = ALIGN_SIZE(data_addr, (intptr_t)align_adjusted);

    header_size = (size_t)(data_addr - (intptr_t)lifo->top);
    footer_size = sizeof(int64_t);
    data_size = ALIGN_SIZE(size, sizeof(int64_t));
    entry_size = header_size + data_size + footer_size;

    ASSERT(data_size < (size_t)(((int64_t)1<<48)-1));
    ASSERT(header_size < (1<<16)-1);
    header = (int64_t)data_size | ((int64_t)header_size<<48);
    footer = (int64_t)data_size | ((int64_t)1<<48);

    if(lifo->remain < entry_size) {
      mem = NULL;
    } else {
      lifo->remain -= entry_size;
      mem = lifo->top + header_size;
      ASSERT(IS_ALIGNED(lifo->top, LIFO_DEFAULT_ALIGNMENT));
      lifo->top += entry_size;
      *(int64_t*)(mem - sizeof(int64_t)) = header;
      *(int64_t*)(mem + data_size) = footer;
    }
  }
  CHK(IS_ALIGNED(mem, align));

  mutex_unlock(lifo->mutex);
  return mem;
}

static void*
lifo_alloc
  (void* data,
   const size_t size,
   const char* filename,
   const unsigned int fileline)
{
  return lifo_alloc_aligned
    (data, size, LIFO_DEFAULT_ALIGNMENT, filename, fileline);
}

static void*
lifo_calloc
  (void* data,
   const size_t nelmts,
   const size_t size,
   const char* filename,
   const unsigned int fileline)
{
  size_t allocation_size = nelmts * size;
  void* mem = lifo_alloc(data, allocation_size, filename, fileline);
  if(mem) mem = memset(mem, 0, allocation_size);
  return mem;
}

static void
lifo_free(void* data, void* mem)
{
  struct lifo_data* lifo = data;
  int64_t header;
  int64_t footer;
  size_t data_size;
  int64_t* pfooter;
  char* ptr;
  char* end;
  ASSERT(data);

  if(!mem) return;

  ptr = mem;
  header = *(int64_t*)(ptr-sizeof(int64_t));
  data_size = (size_t)(header & (((int64_t)1<<48)-1));
  pfooter = (int64_t*)(ptr + data_size);
  end = ptr + data_size + sizeof(int64_t);

  mutex_lock(lifo->mutex);
  *pfooter &= ~((int64_t)1<<48); /* No more in use */
  if(end == lifo->top) { /* Pop */
    size_t header_size = (size_t)(header >> 48);
    char* top = ptr - header_size;

    lifo->remain += data_size + header_size + sizeof(int64_t)/*footer size*/;

    while(top != lifo->stack) { /* Pop all free entries */
      ptr = top - sizeof(int64_t);
      footer = *(int64_t*)ptr;
      if(footer & ((int64_t)1<<48)) break; /* In use */

      data_size = (size_t)(footer & (((int64_t)1<<48)-1));
      ptr -= data_size;

      header = *(int64_t*)(ptr-sizeof(int64_t));
      ASSERT(data_size == (size_t)(header & (((int64_t)1<<48)-1)));
      header_size = (size_t)(header>>48);
      top = ptr - header_size;

      lifo->remain += data_size + header_size + sizeof(int64_t)/*footer_size*/;
      ASSERT(lifo->remain <= lifo->capacity);
    }
    lifo->top = top;
  }
  mutex_unlock(lifo->mutex);
}

static void*
lifo_realloc
  (void* data,
   void* mem,
   const size_t size,
   const char* filename,
   const unsigned int fileline)
{
  if(size==0) {
    lifo_free(data, mem);
    return NULL;
  } else {
    size_t mem_size;
    int64_t mem_header;
    char* new_mem = lifo_alloc(data, size, filename, fileline);
    if(!new_mem || !mem) return new_mem;
    mem_header = *(int64_t*)((char*)mem - sizeof(int64_t));
    mem_size = (size_t)(mem_header & (((int64_t)1<<48)-1));
    memcpy(new_mem, mem, mem_size);
    lifo_free(data, mem);
    return new_mem;
  }
}

static size_t
lifo_mem_size(void* data, void* mem)
{
  int64_t header;
  (void)data;
  header = *(int64_t*)((char*)mem-sizeof(int64_t));
  return (size_t)(header & (((int64_t)1<<48)-1));
}

static size_t
lifo_allocated_size(const void* data)
{
  const struct lifo_data* lifo = data;
  size_t size;
  ASSERT(data);
  mutex_lock(lifo->mutex);
  size = lifo->capacity - lifo->remain;
  mutex_unlock(lifo->mutex);
  return size;
}

static size_t
lifo_dump(const void* data, char* dump, const size_t max_dump_len)
{
  size_t len;

  len = (size_t)snprintf(dump, max_dump_len, "%lu bytes allocated.",
    (unsigned long)lifo_allocated_size(data));
  if(len >= (max_dump_len-1)) /* -1 <=> null char */
    dump[max_dump_len-1] = '\0';
  return len;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
mem_init_lifo_allocator
  (struct mem_allocator* lifo_allocator,
   struct mem_allocator* allocator,
   const size_t size)
{
  struct lifo_data* lifo = NULL;
  res_T res = RES_OK;

  if(!allocator || !lifo_allocator) {
    res = RES_BAD_ARG;
    goto error;
  }
  memset(lifo_allocator, 0, sizeof(struct mem_allocator));

  lifo = MEM_CALLOC(allocator, 1, sizeof(struct lifo_data));
  if(!lifo) {
    res = RES_MEM_ERR;
    goto error;
  }
  lifo_allocator->data = (void*)lifo;
  lifo->allocator = allocator;
  lifo->stack = MEM_ALLOC_ALIGNED(allocator, size, LIFO_DEFAULT_ALIGNMENT);
  if(!lifo->stack) {
    res = RES_MEM_ERR;
    goto error;
  }
  lifo->mutex = mutex_create();
  if(!lifo->mutex) {
    res = RES_MEM_ERR;
    goto error;
  }
  lifo->top = lifo->stack;
  lifo->capacity = size;
  lifo->remain = size;

  lifo_allocator->alloc = lifo_alloc;
  lifo_allocator->calloc = lifo_calloc;
  lifo_allocator->realloc = lifo_realloc;
  lifo_allocator->mem_size = lifo_mem_size;
  lifo_allocator->alloc_aligned = lifo_alloc_aligned;
  lifo_allocator->rm = lifo_free;
  lifo_allocator->allocated_size = lifo_allocated_size;
  lifo_allocator->dump  = lifo_dump;

exit:
  return res;
error:
  if(lifo_allocator)
    mem_shutdown_lifo_allocator(lifo_allocator);
  goto exit;
}

void
mem_shutdown_lifo_allocator(struct mem_allocator* allocator)
{
  struct lifo_data* lifo;
  ASSERT(allocator);
  lifo = allocator->data;
  if(lifo) {
    if(lifo->mutex) mutex_destroy(lifo->mutex);
    if(lifo->stack) MEM_RM(lifo->allocator, lifo->stack);
    MEM_RM(lifo->allocator, lifo);
  }
  memset(allocator, 0, sizeof(struct mem_allocator));
}
