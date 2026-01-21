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

#ifndef ENDIANNESS_H
#define ENDIANNESS_H

#include "rsys.h"

#if defined(COMPILER_GCC)
  #define BYTE_ORDER __BYTE_ORDER__
  #define LITTLE_ENDIAN __ORDER_LITTLE_ENDIAN__
  #define BIG_ENDIAN __ORDER_BIG_ENDIAN__
  #include <byteswap.h>
#elif defined(_MSC_VER)
  #include <stdlib.h>
  #define BYTE_ORDER 1234
  #define LITTLE_ENDIAN 1234
  #define BIG_ENDIAN 4321
  #define bswap_16(x) _byteswap_ushort(x)
  #define bswap_32(x) _byteswap_ulong(x)
  #define bswap_64(x) _byteswap_uint64(x)
#else
  #error "Undefined byte ordering macros"
#endif

static FINLINE uint16_t
byte_swap_16(const uint16_t ui)
{
  union { uint16_t ui; uint8_t bytes[2]; } ucast;
  ucast.ui = ui;
  SWAP(uint8_t, ucast.bytes[0], ucast.bytes[1]);
  return ucast.ui;
}

static FINLINE uint32_t
byte_swap_32(const uint32_t ui)
{
  union { uint32_t ui; uint8_t bytes[4]; } ucast;
  ucast.ui = ui;
  SWAP(uint8_t, ucast.bytes[0], ucast.bytes[3]);
  SWAP(uint8_t, ucast.bytes[1], ucast.bytes[2]);
  return ucast.ui;
}
static FINLINE uint64_t
byte_swap_64(const uint64_t ui)
{
  union { uint64_t ui; uint8_t bytes[8]; } ucast;
  ucast.ui = ui;
  SWAP(uint8_t, ucast.bytes[0], ucast.bytes[7]);
  SWAP(uint8_t, ucast.bytes[1], ucast.bytes[6]);
  SWAP(uint8_t, ucast.bytes[2], ucast.bytes[5]);
  SWAP(uint8_t, ucast.bytes[3], ucast.bytes[4]);
  return ucast.ui;
}

static FINLINE uint16_t
little_endian_16(const uint16_t ui)
{
  union { uint16_t ui; uint8_t bytes[2]; } ucast;
  ucast.ui = ui;
  return ((uint16_t)ucast.bytes[0] << 0)
       | ((uint16_t)ucast.bytes[1] << 8);
}

static FINLINE uint32_t
little_endian_32(const uint32_t ui)
{
  union { uint32_t ui; uint8_t bytes[4]; } ucast;
  ucast.ui = ui;
  return ((uint32_t)ucast.bytes[0] << 0)
       | ((uint32_t)ucast.bytes[1] << 8)
       | ((uint32_t)ucast.bytes[2] << 16)
       | ((uint32_t)ucast.bytes[3] << 24);
}

static FINLINE uint64_t
little_endian_64(const uint64_t ui)
{
  union { uint64_t ui; uint8_t bytes[8]; } ucast;
  ucast.ui = ui;
  return ((uint64_t)ucast.bytes[0] << 0)
       | ((uint64_t)ucast.bytes[1] << 8)
       | ((uint64_t)ucast.bytes[2] << 16)
       | ((uint64_t)ucast.bytes[3] << 24)
       | ((uint64_t)ucast.bytes[4] << 32)
       | ((uint64_t)ucast.bytes[5] << 40)
       | ((uint64_t)ucast.bytes[6] << 48)
       | ((uint64_t)ucast.bytes[7] << 56);
}

static FINLINE uint16_t
big_endian_16(const uint16_t ui)
{
  union { uint16_t ui; uint8_t bytes[2]; } ucast;
  ucast.ui = ui;
  return ((uint16_t)ucast.bytes[0] << 8)
       | ((uint16_t)ucast.bytes[1] << 0);
}

static FINLINE uint32_t
big_endian_32(const uint32_t ui)
{
  union { uint32_t ui; uint8_t bytes[2]; } ucast;
  ucast.ui = ui;
  return ((uint32_t)ucast.bytes[0] << 24)
       | ((uint32_t)ucast.bytes[1] << 16)
       | ((uint32_t)ucast.bytes[2] << 8)
       | ((uint32_t)ucast.bytes[3] << 0);
}

static FINLINE uint64_t
big_endian_64(const uint64_t ui)
{
  union { uint64_t ui; uint8_t bytes[8]; } ucast;
  ucast.ui = ui;
  return ((uint64_t)ucast.bytes[0] << 56)
       | ((uint64_t)ucast.bytes[1] << 48)
       | ((uint64_t)ucast.bytes[2] << 40)
       | ((uint64_t)ucast.bytes[3] << 32)
       | ((uint64_t)ucast.bytes[4] << 24)
       | ((uint64_t)ucast.bytes[5] << 16)
       | ((uint64_t)ucast.bytes[6] << 8)
       | ((uint64_t)ucast.bytes[7] << 0);
}

#endif /* ENDIANNESS_H */
