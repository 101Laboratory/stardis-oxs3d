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

#ifndef REF_COUNT_H
#define REF_COUNT_H

#include "rsys.h"

typedef ATOMIC ref_T;

static FINLINE void
ref_init(ref_T* ref)
{
  ASSERT(NULL != ref);
  *ref = 1;
}

static FINLINE void
ref_get(ref_T* ref)
{
  ASSERT(NULL != ref);
  ATOMIC_INCR(ref);
}

static FINLINE int
ref_put(ref_T* ref, void (*release)(ref_T*))
{
  ATOMIC curr = 0;
  ASSERT(NULL != ref);
  ASSERT(NULL != release);

  curr = ATOMIC_DECR(ref);
  ASSERT(curr >= 0);

  if(0 == curr) {
    release(ref);
    return 1;
  }
  return 0;
}

#endif /* REF_COUNT_H */
