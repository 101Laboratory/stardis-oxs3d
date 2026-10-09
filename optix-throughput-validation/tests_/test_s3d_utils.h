/*
 * test_s3d_utils.h — Test utilities adapted for ox_s3d (no rsys dependency)
 */
#ifndef TEST_S3D_UTILS_H
#define TEST_S3D_UTILS_H

#include "test_compat.h"
#include <cstdio>
#include <cstdlib>

static INLINE float
rand_canonic(void)
{
  int r;
  while((r = rand()) == RAND_MAX);
  return (float)r / (float)RAND_MAX;
}

static void
check_memory_allocator(struct mem_allocator* /*allocator*/)
{
  /* No-op: ox_s3d does not use rsys memory allocator */
}

#endif /* TEST_S3D_UTILS_H */
