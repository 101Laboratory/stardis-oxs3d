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

#include "../condition.h"
#include "../mem_allocator.h"
#include <pthread.h>

#ifdef NDEBUG
  #define PTHREAD(Func) pthread_##Func
#else
  #define PTHREAD(Func) ASSERT(pthread_##Func == 0)
#endif

struct cond*
cond_create(void)
{
  pthread_cond_t* cond = mem_alloc(sizeof(pthread_cond_t));
  if(cond)
    PTHREAD(cond_init(cond, NULL));
  return (struct cond*)cond;
}

void
cond_destroy(struct cond* cond)
{
  ASSERT(cond);
  PTHREAD(cond_destroy((pthread_cond_t*)cond));
  mem_rm(cond);
}

void
cond_wait(struct cond* cond, struct mutex* mutex)
{
  ASSERT(cond);
  PTHREAD(cond_wait((pthread_cond_t*)cond, (pthread_mutex_t*)mutex));
}

void
cond_signal(struct cond* cond)
{
  ASSERT(cond);
  PTHREAD(cond_signal((pthread_cond_t*)cond));
}

void
cond_broadcast(struct cond* cond)
{
  ASSERT(cond);
  PTHREAD(cond_broadcast((pthread_cond_t*)cond));
}

#undef PTHREAD
