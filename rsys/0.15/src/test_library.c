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

#ifdef TEST_LIBRARY_BUILD_LIB
#include "rsys.h"
#include <stdio.h>

extern EXPORT_SYM void
exported_func(void);

void
exported_func(void)
{
  printf("exported_func\n");
}

#else
#include "library.h"
#include "mem_allocator.h"

int
main(int argc, char** argv)
{
  void* lib = NULL;
  (void)argc, (void)argv;

  CHK(library_open(NULL) ==  NULL);
  CHK(library_open("none") ==  NULL);
  lib = library_open("./" SHARED_LIBRARY_NAME("test_lib"));
  CHK(lib != NULL);

  CHK(library_get_symbol(lib, "exported_func_BAD") ==  NULL);
  CHK(library_get_symbol(lib, "exported_func") != NULL);

  CHK(library_close(NULL) ==  RES_BAD_ARG);
  CHK(library_close(lib) ==  RES_OK);

  CHK(MEM_ALLOCATED_SIZE(&mem_default_allocator) ==  0);

  return 0;
}
#endif
