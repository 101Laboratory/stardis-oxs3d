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

#define _POSIX_C_SOURCE 200112L /* Spin lock and mutex rw */
#include "../mem_allocator.h"
#include "../mutex.h"
#include <pthread.h>

#ifdef NDEBUG
  #define PTHREAD(Func) pthread_##Func
#else
  #define PTHREAD(Func) ASSERT(pthread_##Func == 0)
#endif

/*******************************************************************************
 * Mutex
 ******************************************************************************/
struct mutex*
mutex_create(void)
{
  pthread_mutex_t* mutex = mem_alloc(sizeof(pthread_mutex_t));
  if(mutex)
    PTHREAD(mutex_init(mutex, NULL));
  return (struct mutex*)mutex;
}

void
mutex_destroy(struct mutex* mutex)
{
  ASSERT(mutex);
  PTHREAD(mutex_destroy((pthread_mutex_t*)mutex));
  mem_rm(mutex);
}

void
mutex_lock(struct mutex* mutex)
{
  ASSERT(mutex);
  PTHREAD(mutex_lock((pthread_mutex_t*)mutex));
}

void
mutex_unlock(struct mutex* mutex)
{
  ASSERT(mutex);
  PTHREAD(mutex_unlock((pthread_mutex_t*)mutex));
}

/*******************************************************************************
 * Read Write mutex
 ******************************************************************************/
struct mutex_rw*
mutex_rw_create(void)
{
  pthread_rwlock_t* mutex = mem_alloc(sizeof(pthread_rwlock_t));
  if(mutex)
    PTHREAD(rwlock_init(mutex, NULL));
  return (struct mutex_rw*)mutex;
}

void
mutex_rw_destroy(struct mutex_rw* mutex)
{
  ASSERT(mutex);
  PTHREAD(rwlock_destroy((pthread_rwlock_t*)mutex));
  mem_rm(mutex);
}

void
mutex_rw_rlock(struct mutex_rw* mutex)
{
  ASSERT(mutex);
  PTHREAD(rwlock_rdlock((pthread_rwlock_t*)mutex));
}

void
mutex_rw_wlock(struct mutex_rw* mutex)
{
  ASSERT(mutex);
  PTHREAD(rwlock_wrlock((pthread_rwlock_t*)mutex));
}

void
mutex_rw_unlock(struct mutex_rw* mutex)
{
  ASSERT(mutex);
  PTHREAD(rwlock_unlock((pthread_rwlock_t*)mutex));
}

#undef PTHREAD
