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
#ifndef __STDC_FORMAT_MACROS
  #define __STDC_FORMAT_MACROS
#endif
#include "rsys_math.h"
#include "mem_allocator.h"
#include "mutex.h"

#include <errno.h>
#include <inttypes.h>
#if defined(OS_MACH)
  #include <malloc.h>
#else
#include <malloc.h>
#endif
#include <string.h>

#ifdef OS_WINDOWS
  /* On Windows the _aligned_msize function is not defined. The size is thus
   * stored into the memory block header */
  #define MEM_HEADER_SIZE (2 * sizeof(size_t))
  #include "io_c99.h"
#endif

struct alloc_counter {
  ATOMIC nb_allocs;
  ATOMIC allocated_size;
};

/*******************************************************************************
 * Common allocation functions
 ******************************************************************************/
static struct alloc_counter g_alloc_counter = { 0, 0 };

void*
mem_alloc(const size_t size)
{
  void* mem = NULL;
  if(size) {
#if defined(OS_UNIX) || defined (OS_MACH)
    mem = malloc(size);
    if(mem) {
      ATOMIC_ADD(&g_alloc_counter.allocated_size, mem_size(mem));
      ATOMIC_INCR(&g_alloc_counter.nb_allocs);
    }
#elif defined(OS_WINDOWS)
    const size_t DEFAULT_ALIGNMENT = 16;
    mem = _aligned_offset_malloc
      (size + MEM_HEADER_SIZE, DEFAULT_ALIGNMENT, MEM_HEADER_SIZE);
    if(mem) {
      ((size_t*)mem)[0] = DEFAULT_ALIGNMENT;
      ((size_t*)mem)[1] = size + MEM_HEADER_SIZE;
      mem = ((char*)mem) + MEM_HEADER_SIZE;
      ATOMIC_ADD(&g_alloc_counter.allocated_size, mem_size(mem));
      ATOMIC_INCR(&g_alloc_counter.nb_allocs);
    }
#else
  #error "Unsupported OS"
#endif
  }
  return mem;
}

void*
mem_calloc(const size_t nelmts, const size_t size)
{
  void* mem = NULL;
  const size_t alloc_size = nelmts * size;
  mem = mem_alloc(alloc_size);
  if(mem) {
    memset(mem, 0, alloc_size);
  }
  return mem;
}

void*
mem_realloc(void* mem, const size_t size)
{
  void* new_mem = NULL;

  if(mem == NULL) {
    new_mem = mem_alloc(size);
  }  else if(size == 0) {
    mem_rm(mem);
  } else {
    const size_t old_size = mem_size(mem);

    ASSERT
      (  old_size < SIZE_MAX
      && g_alloc_counter.allocated_size >= (int64_t)old_size);
    ATOMIC_SUB( &g_alloc_counter.allocated_size, old_size);

#if defined(OS_WINDOWS)
    mem = ((char*)mem) - MEM_HEADER_SIZE;
    new_mem = _aligned_offset_realloc
      (mem, size + MEM_HEADER_SIZE, ((size_t*)mem)[0], MEM_HEADER_SIZE);
    if(new_mem) {
      ((size_t*)new_mem)[1] = size + MEM_HEADER_SIZE;
      new_mem = ((char*)new_mem) + MEM_HEADER_SIZE;
    }
#elif defined(OS_UNIX) || defined(OS_MACH)
    new_mem = realloc( mem, size );
#else
  #error "Unsupported OS"
#endif
    ATOMIC_ADD(&g_alloc_counter.allocated_size, mem_size(new_mem));
  }
  return new_mem;

}
void*
mem_alloc_aligned(const size_t size, const size_t alignment)
{
  void* mem = NULL;

  if(size
  && IS_POW2( alignment )
  && alignment <= 32768 /* 32 KB */) {
#if defined(OS_WINDOWS)
    mem = _aligned_offset_malloc
      (size + MEM_HEADER_SIZE, alignment, MEM_HEADER_SIZE);
    if(mem) {
      ((size_t*)mem)[0] = alignment;
      ((size_t*)mem)[1] = size + MEM_HEADER_SIZE;
      mem = ((char*)mem) + MEM_HEADER_SIZE;
      ATOMIC_ADD(&g_alloc_counter.allocated_size, mem_size(mem));
      ATOMIC_INCR(&g_alloc_counter.nb_allocs);
    }
#elif defined(OS_UNIX) || defined(OS_MACH)
    const int result = posix_memalign
      (&mem, (alignment < sizeof(void*)) ? sizeof(void*) : alignment, size);
    (void)result; /* avoid warning in Release */
    /* The following assert may not occur due to previous conditions */
    ASSERT(result != EINVAL);
    ASSERT((result != ENOMEM) || (mem == NULL));
    if(mem) {
      ATOMIC_ADD(&g_alloc_counter.allocated_size, mem_size(mem));
      ATOMIC_INCR(&g_alloc_counter.nb_allocs);
    }
#else
  #error "Unsupported OS"
#endif
  }
  return mem;
}

void
mem_rm(void* mem)
{
  if(mem) {
    ASSERT
      (  g_alloc_counter.nb_allocs != 0
      && mem_size(mem) < SIZE_MAX
      && g_alloc_counter.allocated_size >= (int64_t)mem_size(mem));
    ATOMIC_SUB(&g_alloc_counter.allocated_size, mem_size(mem));
    ATOMIC_DECR(&g_alloc_counter.nb_allocs);
#if defined(OS_WINDOWS)
    mem = ((char*)mem) - MEM_HEADER_SIZE;
    _aligned_free( mem );
#elif defined(OS_UNIX) || defined(OS_MACH)
    free( mem );
#else
  #error "Unsupported OS"
#endif
  }
}

size_t
mem_size(void* mem)
{
  size_t mem_size = 0;
  if(mem) {
#if defined(OS_WINDOWS)
    void* raw_mem = ((char*)mem) - MEM_HEADER_SIZE;
    mem_size = ((size_t*)raw_mem)[1];
#elif defined(OS_UNIX)
    mem_size = malloc_usable_size(mem);
#elif defined(OS_MACH)
    mem_size = malloc_size(mem);
#else
  #error "Unsupported OS"
#endif
  }
  return mem_size;
}

size_t
mem_allocated_size(void)
{
  return (size_t)g_alloc_counter.allocated_size;
}

/*******************************************************************************
 * Default allocator functions
 ******************************************************************************/
#define TRACK_DEFAULT_ALLOC /* Enable the tracking of default allocations */

static void*
default_alloc
  (void* data,
   const size_t size,
   const char* filename,
   const unsigned int fileline)
{
  void* mem = NULL;

  (void)filename;
  (void)fileline;

  if(size) {
    mem = mem_alloc(size);
#ifndef TRACK_DEFAULT_ALLOC
    (void)data;
#else
    ASSERT(data);
    if(mem) {
      struct alloc_counter* counter = data;
      const size_t size_mem = mem_size(mem);
      ATOMIC_ADD(&counter->allocated_size, size_mem);
      ATOMIC_INCR(&counter->nb_allocs);
    }
#endif /* TRACK_DEFAULT_ALLOC */
  }
  return mem;
}

static void
default_free(void* data, void* mem)
{
  if(mem) {
#ifndef TRACK_DEFAULT_ALLOC
    (void)data;
#else
    struct alloc_counter* counter = data;
    size_t size_mem = mem_size(mem);
    ASSERT
      ( (data != NULL)
      & (counter->nb_allocs != 0)
      & (counter->allocated_size >= (int64_t)size_mem));

    ATOMIC_SUB(&counter->allocated_size, size_mem);
    ATOMIC_DECR(&counter->nb_allocs);
#endif /* TRACK_DEFAULT_ALLOC */
    mem_rm(mem);
  }
}

static void*
default_calloc
  (void* data,
   const size_t nbelmts,
   const size_t size,
   const char* filename,
   const unsigned int fileline)
{
  void* mem = NULL;
  const size_t alloc_size = nbelmts * size;

  mem = default_alloc(data, alloc_size, filename, fileline);
  if(mem) {
    memset(mem, 0, alloc_size);
  }
  return mem;
}

static void*
default_realloc
  (void* data,
   void* mem,
   const size_t size,
   const char* filename,
   const unsigned int fileline)
{
  void* new_mem = NULL;

#ifndef TRACK_DEFAULT_ALLOC
  (void)data;
  (void)filename;
  (void)fileline;
  new_mem = mem_realloc(mem, size);
#else
  ASSERT(data);
  if(!mem) {
    new_mem = default_alloc(data, size, filename, fileline);
  } else {
    if(size == 0) {
      default_free(data, mem);
    } else {
      struct alloc_counter* counter = data;
      const size_t size_old = mem_size(mem);
      size_t size_new = 0;

      ASSERT(counter->allocated_size >= (int64_t)size_old);
      ATOMIC_SUB(&counter->allocated_size, size_old);

      new_mem = mem_realloc(mem, size);
      size_new = mem_size(new_mem);
      ATOMIC_ADD(&counter->allocated_size, size_new);
    }
  }
#endif /* TRACK_DEFAULT_ALLOC */
  return new_mem;
}

static void*
default_alloc_aligned
  (void* data,
   const size_t size,
   const size_t alignment,
   const char* filename,
   const unsigned int fileline)
{
  void* mem = NULL;

  (void)filename;
  (void)fileline;

  if(size && IS_POW2(alignment)) {
    mem = mem_alloc_aligned(size, alignment);
#ifndef TRACK_DEFAULT_ALLOC
    (void)data;
    #else
    ASSERT(data);
    if(mem) {
      struct alloc_counter* counter = data;
      const size_t size_mem = mem_size(mem);
      ATOMIC_ADD(&counter->allocated_size, size_mem);
      ATOMIC_INCR(&counter->nb_allocs);
    }
#endif /* TRACK_DEFAULT_ALLOC */
  }
  return mem;
}

static size_t
default_mem_size(void* data, void* mem)
{
  (void)data;
  return mem_size(mem);
}

static size_t
default_allocated_size(const void* data)
{
#ifndef TRACK_DEFAULT_ALLOC
  (void)data;
  return 0;
#else
  const struct alloc_counter* counter = data;
  ASSERT(counter != NULL);
  return (size_t)counter->allocated_size;
#endif /* TRACK_DEFAULT_ALLOC */
}

static size_t
default_dump
  (const void* data,
   char* dump,
   const size_t max_dump_len)
{
#ifndef TRACK_DEFAULT_ALLOC
  (void)data;
  if(dump && max_dump_len)
    dump[0] = '\0';
  return 0;

#else
  const struct alloc_counter* counter = data;
  size_t dump_len = 0;
  int len = 0;

  ASSERT(counter && (!max_dump_len || dump));

  len = snprintf
    (dump,
     max_dump_len,
#if defined(OS_WINDOWS)
     "%I64d bytes allocated in %I64d allocations.",
#else
     "%" PRId64 " bytes allocated in %" PRId64 " allocations.",
#endif
     counter->allocated_size,
     counter->nb_allocs);
#if defined(OS_WINDOWS)
  if(len < 0) {
    len = _scprintf("%" PRId64 " bytes allocated in %" PRId64 " allocations.",
                    counter->allocated_size,
                    counter->nb_allocs);
  }
#endif
  ASSERT(len >= 0);
  dump_len = (size_t)len;

  if((size_t)len >= (max_dump_len - 1)) /* -1 <=> null char. */
    dump[max_dump_len-1] = '\0';

  return dump_len;
#endif
}

/*******************************************************************************
 * Default allocator
 ******************************************************************************/
static struct alloc_counter default_alloc_counter = {0, 0};

struct mem_allocator mem_default_allocator = {
  default_alloc,
  default_calloc,
  default_realloc,
  default_alloc_aligned,
  default_free,
  default_mem_size,
  default_allocated_size,
  default_dump,
  (void*)&default_alloc_counter
};

/*******************************************************************************
 * Regular allocator
 ******************************************************************************/
res_T
mem_init_regular_allocator(struct mem_allocator* allocator)
{
  struct alloc_counter* counter = NULL;
  res_T res = RES_OK;

  if(!allocator) {
    res = RES_BAD_ARG;
    goto error;
  }
  memset(allocator, 0, sizeof(struct mem_allocator));

  counter = mem_calloc(1, sizeof(struct alloc_counter));
  if(!counter) {
    res = RES_MEM_ERR;
    goto error;
  }

  allocator->alloc = default_alloc;
  allocator->calloc = default_calloc;
  allocator->realloc = default_realloc;
  allocator->mem_size = default_mem_size;
  allocator->alloc_aligned = default_alloc_aligned;
  allocator->rm = default_free;
  allocator->allocated_size = default_allocated_size;
  allocator->dump = default_dump;
  allocator->data = (void*)counter;

exit:
  return res;
error:
  if(allocator) mem_shutdown_regular_allocator(allocator);
  goto exit;
}

void
mem_shutdown_regular_allocator(struct mem_allocator* allocator)
{
  struct alloc_counter* counter;
  ASSERT(allocator);

  counter = allocator->data;
  if(counter) {
    ASSERT(!counter->allocated_size && !counter->nb_allocs);
    mem_rm(counter);
  }
  memset(allocator, 0, sizeof(struct mem_allocator));
}
