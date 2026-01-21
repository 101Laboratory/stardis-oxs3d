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

int
main(int argc, char** argv)
{
  int i = 0;
  (void)argv;

#ifdef COMPILER_GCC
  printf("GCC version: %d.%d.%d\n",
    __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif

  /* Test fallthrough warning */
  switch(argc) {
    case 2: ++i; FALLTHROUGH;
    case 1: ++i; FALLTHROUGH;
    case 0: ++i; break;
    default: FATAL("Unreachable code.\n");
  }
  return 0;
}
