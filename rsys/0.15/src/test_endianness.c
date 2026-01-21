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

#include "endianness.h"

int
main(int argc, char** argv)
{
  uint16_t ui16 = 0x0123;
  uint32_t ui32 = 0x01234567;
  uint64_t ui64 = 0x0123456789ABCDEF;
  (void)argc, (void)argv;

  CHK(byte_swap_16(ui16) == 0x2301);
  CHK(byte_swap_32(ui32) == 0x67452301);
  CHK(byte_swap_64(ui64) == 0xEFCDAB8967452301);

#if BYTE_ORDER == LITTLE_ENDIAN
  CHK(little_endian_16(ui16) == ui16);
  CHK(little_endian_32(ui32) == ui32);
  CHK(little_endian_64(ui64) == ui64);
  CHK(big_endian_16(ui16) == byte_swap_16(ui16));
  CHK(big_endian_32(ui32) == byte_swap_32(ui32));
  CHK(big_endian_64(ui64) == byte_swap_64(ui64));

#elif BYTE_ORDER == BIG_ENDIAN
  CHK(little_endian_16(ui16) == byte_swap_16(ui16));
  CHK(little_endian_32(ui32) == byte_swap_32(ui32));
  CHK(little_endian_64(ui64) == byte_swap_64(ui64));
  CHK(big_endian_16(ui16) == ui16);
  CHK(big_endian_32(ui32) == ui32);
  CHK(big_endian_64(ui64) == ui64);

#else
  #error "Undefined endianness"
#endif

  return 0;
}
