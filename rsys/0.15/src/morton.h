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

#ifndef MORTON_H
#define MORTON_H

#include "rsys.h"

static FINLINE uint32_t
morton2D_encode_u16(const uint16_t u16)
{
  uint32_t u32 = u16;
  u32 = (u32 | (u32 << 8)) & 0x00FF00FF;
  u32 = (u32 | (u32 << 4)) & 0X0F0F0F0F;
  u32 = (u32 | (u32 << 2)) & 0x33333333;
  u32 = (u32 | (u32 << 1)) & 0x55555555;
  return u32;
}

static FINLINE uint16_t
morton2D_decode_u16(const uint32_t u32)
{
  uint32_t x = u32 & 0x55555555;
  x = (x | (x >> 1)) & 0x33333333;
  x = (x | (x >> 2)) & 0x0F0F0F0F;
  x = (x | (x >> 4)) & 0x00FF00FF;
  x = (x | (x >> 8)) & 0x0000FFFF;
  return (uint16_t)x;
}

static INLINE uint64_t
morton3D_encode_u21(const uint32_t u21)
{
  uint64_t u64 = u21 & ((1<<21) - 1);
  ASSERT(u21 <= ((1 << 21) - 1));
  u64 = (u64 | (u64 << 32)) & 0xFFFF00000000FFFF;
  u64 = (u64 | (u64 << 16)) & 0x00FF0000FF0000FF;
  u64 = (u64 | (u64 << 8))  & 0xF00F00F00F00F00F;
  u64 = (u64 | (u64 << 4))  & 0x30C30C30C30C30C3;
  u64 = (u64 | (u64 << 2))  & 0x9249249249249249;
  return u64;
}

static INLINE uint32_t
morton3D_decode_u21(const uint64_t u64)
{
  uint64_t tmp = (u64 & 0x9249249249249249);
  tmp = (tmp | (tmp >> 2))  & 0x30C30C30C30C30C3;
  tmp = (tmp | (tmp >> 4))  & 0xF00F00F00F00F00F;
  tmp = (tmp | (tmp >> 8))  & 0x00FF0000FF0000FF;
  tmp = (tmp | (tmp >> 16)) & 0xFFFF00000000FFFF;
  tmp = (tmp | (tmp >> 32)) & 0x00000000FFFFFFFF;
  ASSERT(tmp <= ((1<<21)-1));
  return (uint32_t)tmp;
}

static INLINE uint32_t
morton_xy_encode_u16(const uint16_t xy[2])
{
  ASSERT(xy);
  return (morton2D_encode_u16(xy[0]) << 1)
       | (morton2D_encode_u16(xy[1]) << 0);
}

static INLINE void
morton_xy_decode_u16(const uint32_t code, uint16_t xy[2])
{
  ASSERT(xy);
  xy[0] = (uint16_t)morton2D_decode_u16(code >> 1);
  xy[1] = (uint16_t)morton2D_decode_u16(code >> 0);
}

static INLINE uint64_t
morton_xyz_encode_u21(const uint32_t xyz[3])
{
  ASSERT(xyz);
  return (morton3D_encode_u21(xyz[0]) << 2)
       | (morton3D_encode_u21(xyz[1]) << 1)
       | (morton3D_encode_u21(xyz[2]) << 0);
}

static INLINE void
morton_xyz_decode_u21(const uint64_t code, uint32_t xyz[3])
{
  ASSERT(xyz && code < (((uint64_t)1 << 63)-1));
  xyz[0] = (uint32_t)morton3D_decode_u21(code >> 2);
  xyz[1] = (uint32_t)morton3D_decode_u21(code >> 1);
  xyz[2] = (uint32_t)morton3D_decode_u21(code >> 0);
}

#endif /* MORTON_H */
