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

#include "rsys.h"
#include <string.h>

static void foo(void) { CHK(strcmp(FUNC_NAME, "foo") == 0); }
static void bar(void) { CHK(strcmp(FUNC_NAME, "bar") == 0); }
static void foo_bar(int i)
{
  (void)i;
  CHK(strcmp(FUNC_NAME, "foo_bar") == 0);
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  CHK(strcmp(FUNC_NAME, "main") == 0);
  foo();
  bar();
  foo_bar(0);
  return 0;
}
