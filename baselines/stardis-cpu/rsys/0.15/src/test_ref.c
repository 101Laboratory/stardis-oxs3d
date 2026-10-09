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

#include "ref_count.h"

struct test {
  ref_T ref;
  int val;
};

static void
release(ref_T* ref)
{
  ASSERT(NULL != ref);
  CHK(CONTAINER_OF(ref, struct test, ref)->val == (int)0xDEADBEEF);
}

int
main(int argc, char** argv)
{
  struct test test;
  (void)argc, (void)argv;

  ref_init(&test.ref);
  test.val = (int)0xDEADBEEF;

  ref_get(&test.ref);
  ref_get(&test.ref);
  ref_get(&test.ref);

  CHK(ref_put(&test.ref, release) == 0);
  CHK(ref_put(&test.ref, release) == 0);
  CHK(ref_put(&test.ref, release) == 0);
  CHK(ref_put(&test.ref, release) == 1);
  return 0;
}
