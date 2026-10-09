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

struct proxy_data {
  struct mem_allocator* allocator;
  struct mutex* mutex;
  struct mem_node* node_list;
};

struct mem_node {
  struct mem_node* next;
  struct mem_node* prev;
  size_t size;
  const char* filename;
  unsigned int fileline;
  char reserved[2];
};

#define PROXY_DEFAULT_ALIGNMENT 8

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void*
proxy_alloc_aligned
  (void* data,
   const size_t size,
   const size_t align,
   const char* filename,
   const unsigned int fileline)
{
  struct proxy_data* proxy_data = NULL;
  char* mem = NULL;
  size_t node_header_size = 0;
  size_t node_size = 0;
  size_t align_adjusted = 0;
  struct mem_node* node = NULL;

  ASSERT(data);
  proxy_data = data;

  if((IS_POW2(align) == 0) || align > 32768)
    return NULL;
  align_adjusted = align < PROXY_DEFAULT_ALIGNMENT
    ? PROXY_DEFAULT_ALIGNMENT : align;

  node_header_size = ALIGN_SIZE(sizeof(struct mem_node), align_adjusted);
  node_size = node_header_size + size;
  node = MEM_ALLOC_ALIGNED(proxy_data->allocator, node_size, align_adjusted);
  if(!node)
    return NULL;

  mem = (char*)((uintptr_t)node + (uintptr_t)node_header_size);
  mem[-1] = (char)(align_adjusted & 0xFF);
  mem[-2] = (char)((align_adjusted >> 8) & 0xFF);
  node->prev = NULL;
  node->filename = filename;
  node->fileline = fileline;
  node->size = size;

  mutex_lock(proxy_data->mutex);
  node->next = proxy_data->node_list;
  if(proxy_data->node_list)
    proxy_data->node_list->prev = node;
  proxy_data->node_list = node;
  mutex_unlock(proxy_data->mutex);
  return mem;
}

static void*
proxy_alloc
  (void* data,
   const size_t size,
   const char* filename,
   const unsigned int fileline)
{
  return proxy_alloc_aligned
    (data, size, PROXY_DEFAULT_ALIGNMENT, filename, fileline);
}

static void*
proxy_calloc
  (void* data,
   const size_t nbelmts,
   const size_t size,
   const char* filename,
   const unsigned int fileline)
{
  size_t allocation_size = nbelmts * size;
  void* mem = proxy_alloc_aligned
    (data, allocation_size, PROXY_DEFAULT_ALIGNMENT, filename, fileline);
  if(mem)
    mem = memset(mem, 0, allocation_size);
  return mem;
}

static void
proxy_free(void* data, void* mem)
{
  if(mem) {
    struct proxy_data* proxy_data = NULL;
    struct mem_node* node = NULL;
    uintptr_t alignment = 0;

    ASSERT(data);
    proxy_data = data;

    alignment = (uintptr_t)(((char*)mem)[-1] | (((char*)mem)[-2] << 8));
    node =
      (void*)((uintptr_t)mem - ALIGN_SIZE(sizeof(struct mem_node), alignment));

    mutex_lock(proxy_data->mutex);
    if(node->prev) {
      node->prev->next = node->next;
    }
    if(node->next) {
      node->next->prev = node->prev;
    }
    if(node->prev == NULL) {
      proxy_data->node_list = node->next;
    }
    mutex_unlock(proxy_data->mutex);
    MEM_RM(proxy_data->allocator, node);
  }
}

static void*
proxy_realloc
  (void* data,
   void* mem,
   const size_t size,
   const char* filename,
   const unsigned int fileline)
{
  if(size == 0) {
    proxy_free(data, mem);
    return NULL;
  } else if(mem == NULL) {
    return proxy_alloc_aligned
      (data, size, PROXY_DEFAULT_ALIGNMENT, filename, fileline);
  } else {
    struct mem_node* node = NULL;
    uintptr_t node_header_size = 0;
    uintptr_t alignment = 0;

    alignment = (uintptr_t)(((char*)mem)[-1] | (((char*)mem)[-2] << 8));
    node_header_size = ALIGN_SIZE(sizeof(struct mem_node), alignment);
    node = (void*)((uintptr_t)mem - node_header_size);

    if(node->size == size) {
      return mem;
    } else {
      void* dst = proxy_alloc_aligned
        (data, size, alignment, filename, fileline);
      if(!dst) {
        proxy_free(data, mem);
        return NULL;
      } else {
        dst = memcpy(dst, mem, size < node->size ? size : node->size);
        proxy_free(data, mem);
        return dst;
      }
    }
  }
}

static size_t
proxy_mem_size(void* data, void* mem)
{
  const uintptr_t alignment = (uintptr_t)
    (((char*)mem)[-1] | (((char*)mem)[-2] << 8));
  struct mem_node* node = (struct mem_node*)
    ((uintptr_t)mem - ALIGN_SIZE(sizeof(struct mem_node), alignment));
  struct proxy_data* proxy_data = (struct proxy_data*)data;
  ASSERT(data);
  return MEM_SIZE(proxy_data->allocator, node);
}

static size_t
proxy_allocated_size(const void* data)
{
  const struct proxy_data* proxy_data = NULL;
  struct mem_node* node = NULL;
  size_t allocated_size = 0;

  ASSERT(data);
  proxy_data = data;
  mutex_lock(proxy_data->mutex);
  for(node = proxy_data->node_list; node != NULL; node = node->next) {
    allocated_size += MEM_SIZE(proxy_data->allocator, node);
  }
  mutex_unlock(proxy_data->mutex);
  return allocated_size;
}

static size_t
proxy_dump
  (const void* data,
   char* dump,
   const size_t max_dump_len)
{
  const struct proxy_data* proxy_data = NULL;
  struct mem_node* node = NULL;
  size_t dump_len = 0;
  size_t avaible_dump_space = max_dump_len ? max_dump_len - 1 /*NULL char*/ : 0;

  ASSERT(data && (!max_dump_len || dump));
  proxy_data = data;

  mutex_lock(proxy_data->mutex);
  for(node = proxy_data->node_list; node != NULL; node = node->next) {
    if(dump) {
      const int len = snprintf
        (dump,
         avaible_dump_space,
         "%lu bytes allocated at %s:%u%s",
         (long unsigned)MEM_SIZE(proxy_data->allocator, node),
         node->filename ? node->filename : "none",
         node->fileline,
         node->next ? ".\n" : ".");
      ASSERT(len >= 0);
      dump_len += (size_t)len;

      if((size_t)len < avaible_dump_space) {
        dump += len;
        avaible_dump_space -= (size_t)len;
      } else if(dump) {
        dump[avaible_dump_space] = '\0';
        avaible_dump_space = 0;
        dump = NULL;
      }
    }
  }
  mutex_unlock(proxy_data->mutex);
  return dump_len;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
mem_init_proxy_allocator
  (struct mem_allocator* proxy_allocator,
   struct mem_allocator* allocator)
{
  struct proxy_data* proxy_data = NULL;
  res_T res = RES_OK;

  if(!allocator || !proxy_allocator) {
    res = RES_BAD_ARG;
    goto error;
  }
  memset(proxy_allocator, 0, sizeof(struct mem_allocator));
  proxy_data = MEM_CALLOC(allocator, 1, sizeof(struct proxy_data));
  if(!proxy_data) {
    res = RES_MEM_ERR;
    goto error;
  }
  proxy_allocator->data = (void*)proxy_data;

  proxy_data->allocator = allocator;
  proxy_data->mutex = mutex_create();
  if(!proxy_data->mutex) {
    res = RES_MEM_ERR;
    goto error;
  }
  proxy_allocator->alloc = proxy_alloc;
  proxy_allocator->calloc = proxy_calloc;
  proxy_allocator->realloc = proxy_realloc;
  proxy_allocator->mem_size = proxy_mem_size;
  proxy_allocator->alloc_aligned = proxy_alloc_aligned;
  proxy_allocator->rm = proxy_free;
  proxy_allocator->allocated_size = proxy_allocated_size;
  proxy_allocator->dump = proxy_dump;

exit:
  return res;
error:
  if(proxy_allocator)
    mem_shutdown_proxy_allocator(proxy_allocator);
  goto exit;
}

void
mem_shutdown_proxy_allocator(struct mem_allocator* proxy)
{
  struct proxy_data* proxy_data = NULL;

  /* DIAGNOSTIC MODE: Dump before any assertions */
  fprintf(stderr, "[RSYS-DEBUG] mem_shutdown_proxy_allocator called, proxy=%p\n", (void*)proxy);
  fflush(stderr);
  
  ASSERT(proxy);
  proxy_data = proxy->data;
  
  fprintf(stderr, "[RSYS-DEBUG] proxy_data=%p\n", (void*)proxy_data);
  fflush(stderr);
  
  if(proxy_data) {
    fprintf(stderr, "[RSYS-DEBUG] node_list=%p\n", (void*)proxy_data->node_list);
    fflush(stderr);
    
    if(proxy_data->node_list != NULL) {
      struct mem_node* node;
      size_t node_count = 0;
      size_t total_size_via_node = 0;
      size_t total_size_via_MEM_SIZE = 0;
      
      fprintf(stderr, "\n=== RSYS DIAGNOSTIC: node_list not empty at shutdown ===\n");
      
      mutex_lock(proxy_data->mutex);
      for(node = proxy_data->node_list; node != NULL; node = node->next) {
        size_t size_via_MEM_SIZE = MEM_SIZE(proxy_data->allocator, node);
        fprintf(stderr, "  Node #%lu:\n", (unsigned long)node_count);
        fprintf(stderr, "    - node->size: %lu bytes\n", (unsigned long)node->size);
        fprintf(stderr, "    - MEM_SIZE(allocator, node): %lu bytes\n", (unsigned long)size_via_MEM_SIZE);
        fprintf(stderr, "    - Allocated at: %s:%u\n",
          node->filename ? node->filename : "unknown",
          node->fileline);
        fprintf(stderr, "    - Node address: %p\n", (void*)node);
        fprintf(stderr, "    - Next: %p, Prev: %p\n", (void*)node->next, (void*)node->prev);
        
        node_count++;
        total_size_via_node += node->size;
        total_size_via_MEM_SIZE += size_via_MEM_SIZE;
      }
      mutex_unlock(proxy_data->mutex);
      
      fprintf(stderr, "  SUMMARY:\n");
      fprintf(stderr, "    - Total nodes: %lu\n", (unsigned long)node_count);
      fprintf(stderr, "    - Total size (node->size): %lu bytes\n", (unsigned long)total_size_via_node);
      fprintf(stderr, "    - Total size (MEM_SIZE): %lu bytes\n", (unsigned long)total_size_via_MEM_SIZE);
      fprintf(stderr, "    - allocated_size() returns: %lu bytes\n", (unsigned long)proxy_allocated_size(proxy_data));
      fprintf(stderr, "=== END DIAGNOSTIC ===\n\n");
      fflush(stderr);
    }
    
    fprintf(stderr, "[RSYS-DEBUG] About to assert node_list == NULL\n");
    fflush(stderr);
    ASSERT(proxy_data->node_list == NULL);
    if(proxy_data->mutex) mutex_destroy(proxy_data->mutex);
    MEM_RM(proxy_data->allocator, proxy_data);
  }
  memset(proxy, 0, sizeof(struct mem_allocator));
}
