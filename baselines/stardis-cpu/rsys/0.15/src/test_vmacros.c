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
  (void)argc, (void)argv;
  #define PRINTF(Fmt, Args) printf(Fmt COMMA_##Args LIST_##Args)
  PRINTF("Arg0\n", ARG0());
  PRINTF("Arg1 %d\n", ARG1(1));
  PRINTF("Arg2 %d %d\n", ARG2(1, 2));
  PRINTF("Arg3 %d %d %d\n", ARG3(1, 2, 3));
  PRINTF("Arg4 %d %d %d %d\n", ARG4(1, 2, 3, 4));
  PRINTF("Arg5 %d %d %d %d %d\n", ARG5(1, 2, 3, 4, 5));
  PRINTF("Arg6 %d %d %d %d %d %d\n", ARG6(1, 2, 3, 4, 5, 6));
  PRINTF("Arg7 %d %d %d %d %d %d %d\n", ARG7(1, 2, 3, 4, 5, 6, 7));
  PRINTF("Arg8 %d %d %d %d %d %d %d %d\n", ARG8(1, 2, 3, 4, 5, 6, 7, 8));
  PRINTF("Arg9 %d %d %d %d %d %d %d %d %d\n", ARG9(1, 2, 3, 4, 5, 6, 7, 8, 9));
  #undef PRINTF
  return 0;
}
