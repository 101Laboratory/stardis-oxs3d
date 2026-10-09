/* copyright (c) 2013-2015 vincent forest (vaplv@free.fr)
 *
 * the rsys library is free software: you can redistribute it and/or modify
 * it under the terms of the gnu lesser general public license as published
 * by the free software foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * the rsys library is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * merchantability or fitness for a particular purpose. see the
 * gnu lesser general public license for more details.
 *
 * you should have received a copy of the gnu lesser general public license
 * along with the rsys library. if not, see <http://www.gnu.org/licenses/>. */

#include "stretchy_array.h"
#include "rsys_math.h"

static void
test_double(void)
{
  double* array = NULL;

  CHK(sa_size(array) == 0);
  sa_clear(array);

  CHK(sa_add(array, 4) == array);
  CHK(sa_size(array) == 4);

  array[0] = PI;
  array[1] = PI;
  array[2] = PI;
  array[3] = PI;
  CHK(sa_last(array) == PI);

  sa_push(array, 1.2345);
  CHK(sa_last(array) == 1.2345);
  sa_push(array, 6.7890);
  CHK(sa_last(array) == 6.7890);

  CHK(array[0] == PI);
  CHK(array[1] == PI);
  CHK(array[2] == PI);
  CHK(array[3] == PI);
  CHK(array[4] == 1.2345);
  CHK(array[5] == 6.7890);
  CHK(sa_size(array) == 6);

  sa_clear(array);
  CHK(sa_size(array) == 0);

  sa_release(array);
}

static void
test_int(void)
{
  int* array = NULL;
  int* p;
  size_t i;

  CHK(sa_size(array) == 0);

  FOR_EACH(i, 0, 1024)
    sa_push(array, (int)i);

  CHK(sa_size(array) == 1024);

  FOR_EACH(i, 0, 1024)
    CHK(array[i] == (int)i);

  CHK(sa_last(array) == 1023);

  p = sa_add(array, 32);
  CHK(sa_size(array) == 1056);

  FOR_EACH(i, 0, 32)
    p[i] = (int)i;

  CHK(sa_last(array) == 31);
  FOR_EACH(i, 0, 32)
    array[1024 + i] = (int)i;

  sa_clear(array);
  CHK(sa_size(array) == 0);
  p = sa_add(array, 16);
  CHK(p == array);
  CHK(sa_size(array) == 16);
  FOR_EACH(i, 0, 16)
    p[i] = -(int)i;

  FOR_EACH(i, 0, 16)
    CHK(array[i] == -(int)i);

  sa_release(array);
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  test_int();
  test_double();
  CHK(mem_allocated_size() == 0);
  return 0;
}
