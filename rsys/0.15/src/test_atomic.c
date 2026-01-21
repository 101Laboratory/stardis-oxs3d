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
  ATOMIC atom = 0;
  ATOMIC tmp;
  (void)argc, (void)argv;

  tmp = ATOMIC_INCR(&atom);
  CHK(atom == 1);
  CHK(tmp == 1);
  tmp = ATOMIC_ADD(&atom, 5);
  CHK(atom == 6);
  CHK(tmp == 6);
  tmp = ATOMIC_DECR(&atom);
  CHK(atom == 5);
  CHK(tmp == 5);
  tmp = ATOMIC_SUB(&atom, 7);
  CHK(atom == -2);
  CHK(tmp == -2);

  tmp = ATOMIC_CAS(&atom, 0, -1);
  CHK(atom == -2);
  CHK(tmp == -2);
  tmp = ATOMIC_CAS(&atom, 0, -2);
  CHK(atom == 0);
  CHK(tmp == -2);
  tmp = ATOMIC_SET(&atom, 9);
  CHK(atom == 9);
  CHK(tmp == 0);
  tmp = ATOMIC_GET(&atom);
  CHK(tmp == 9);

  return 0;
}
