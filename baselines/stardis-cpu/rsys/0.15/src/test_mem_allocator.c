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

#include "mem_allocator.h"
#include "rsys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
test_regular(void)
{
  void* p = NULL;
  void* q[3] = {NULL, NULL, NULL};
  size_t i = 0;

  p = mem_alloc_aligned(1024, ALIGNOF(char));
  CHK(p != NULL);
  CHK(IS_ALIGNED((uintptr_t)p, ALIGNOF(char)) == 1);
  mem_rm( p );

  q[0] = mem_alloc_aligned(10, 64);
  q[1] = mem_alloc(58);
  q[2] = mem_alloc(78);
  CHK(q[0] != NULL);
  CHK(q[1] != NULL);
  CHK(q[2] != NULL);
  CHK(IS_ALIGNED((uintptr_t )q[0], 64 ) == 1);

  p = mem_calloc(1, 4);
  CHK(p != NULL);
  FOR_EACH(i, 0, 4) {
    CHK(((char*)p)[i] == 0);
  }
  FOR_EACH(i, 0, 4) {
    ((char*)p)[i] = (char)i;
  }

  mem_rm(q[1]);

  p = mem_realloc(p, 8);
  FOR_EACH(i, 0, 4) {
    CHK(((char*)p)[i] == (char)i);
  }
  FOR_EACH(i, 4, 8) {
    ((char*)p)[i] = (char)i;
  }

  mem_rm(q[2]);

  p = mem_realloc(p, 5);
  FOR_EACH(i, 0, 5) {
    CHK(((char*)p )[i] == (char)i);
  }

  mem_rm(p);

  p = NULL;
  p = mem_realloc(NULL, 16);
  CHK(p != NULL);
  p = mem_realloc(p, 0);

  mem_rm(q[0]);

  CHK(mem_alloc_aligned(1024, 0 ) == NULL);
  CHK(mem_alloc_aligned(1024, 3 ) == NULL);
}

static void
test_allocator(struct mem_allocator* allocator)
{
  char dump[24];
  void* p = NULL;
  void* q[3] = {NULL, NULL, NULL};
  size_t i = 0;

  p = MEM_ALLOC_ALIGNED(allocator, 1024, ALIGNOF(char));
  CHK(p != NULL);
  CHK(IS_ALIGNED((uintptr_t)p, ALIGNOF(char)) == 1);
  MEM_RM(allocator, p);

  q[0] = MEM_ALLOC_ALIGNED(allocator, 10, 8);
  q[1] = MEM_CALLOC(allocator, 1, 58);
  q[2] = MEM_ALLOC(allocator, 78);
  CHK(q[0] != NULL);
  CHK(q[1] != NULL);
  CHK(q[2] != NULL);
  CHK(IS_ALIGNED((uintptr_t)q[0], 8) == 1);

  p = MEM_CALLOC(allocator, 2, 2);
  CHK(p != NULL);
  for(i = 0; i < 4; ++i)
    CHK(((char*)p)[i] == 0);
  for(i = 0; i < 4; ++i)
    ((char*)p)[i] = (char)i;

  MEM_DUMP(allocator, dump, 24);
  printf("dump:\n%s\n", dump);
  MEM_DUMP(allocator, dump, 16);
  printf("truncated dump:\n%s\n", dump);
  MEM_DUMP(allocator, NULL, 0); /* may not crash */

  MEM_RM(allocator, q[1]);

  p = MEM_REALLOC(allocator, p, 8);
  for(i = 0; i < 4; ++i)
    CHK(((char*)p)[i] == (char)i);
  for(i = 4; i < 8; ++i)
    ((char*)p)[i] = (char)i;

  MEM_RM(allocator, q[2]);

  p = MEM_REALLOC(allocator, p, 5);
  for(i = 0; i < 5; ++i)
    CHK(((char*)p)[i] == (char)i);

  MEM_RM(allocator, p);

  p = NULL;
  p = MEM_REALLOC(allocator, NULL, 16);
  CHK(p != NULL);
  p = MEM_REALLOC(allocator, p, 0);

  MEM_RM(allocator, q[0]);

  CHK(MEM_ALLOC_ALIGNED(allocator, 1024, 0) == NULL);
  CHK(MEM_ALLOC_ALIGNED(allocator, 1024, 3) == NULL);
  CHK(MEM_ALLOCATED_SIZE(allocator) == 0);
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct mem_allocator allocator2;
  int mem[8];

  (void)argc;
  (void)argv;

  printf("-- Common allocation functions\n");
  test_regular();

  printf("-- Default allocator\n");
  test_allocator(&mem_default_allocator);

  printf("\n-- Regular allocator\n");
  CHK(mem_init_regular_allocator(NULL) == RES_BAD_ARG);
  CHK(mem_init_regular_allocator(&allocator) == RES_OK);
  test_allocator(&allocator);
  mem_shutdown_regular_allocator(&allocator);

  printf("\n-- Proxy allocator of default allocator\n");
  CHK(mem_init_proxy_allocator(NULL, NULL) == RES_BAD_ARG);
  CHK(mem_init_proxy_allocator(&allocator, NULL) == RES_BAD_ARG);
  CHK(mem_init_proxy_allocator(NULL, &mem_default_allocator) == RES_BAD_ARG);
  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  test_allocator(&allocator);

  printf("\n-- Proxy allocator of proxy allocator\n");
  CHK(mem_init_proxy_allocator(&allocator2, &allocator) == RES_OK);
  test_allocator(&allocator2);

  mem_shutdown_proxy_allocator(&allocator2);
  mem_shutdown_proxy_allocator(&allocator);

  printf("\n-- LIFO allocator\n");
  CHK(mem_init_lifo_allocator(NULL, NULL, 4096) == RES_BAD_ARG);
  CHK(mem_init_lifo_allocator(&allocator, NULL, 4096) == RES_BAD_ARG);
  CHK(mem_init_lifo_allocator(NULL, &mem_default_allocator, 4096) == RES_BAD_ARG);
  CHK(mem_init_lifo_allocator(&allocator, &mem_default_allocator, 4096) == RES_OK);
  CHK(MEM_ALLOC(&allocator, 4097) == NULL);
  test_allocator(&allocator);
  mem_shutdown_lifo_allocator(&allocator);

  CHK(MEM_AREA_OVERLAP(mem, sizeof(int[2]), mem + 2, sizeof(int[6])) == 0);
  CHK(MEM_AREA_OVERLAP(mem + 4, sizeof(int[4]), mem, sizeof(int[4])) == 0);
  CHK(MEM_AREA_OVERLAP(mem, sizeof(int[2]), mem + 1, sizeof(int[7])) == 1);
  CHK(MEM_AREA_OVERLAP(mem + 7, sizeof(int[1]), mem, sizeof(int[8])) == 1);

  CHK(MEM_ALLOCATED_SIZE(&mem_default_allocator) == 0);
  CHK(mem_allocated_size() == 0);

  return 0;
}
