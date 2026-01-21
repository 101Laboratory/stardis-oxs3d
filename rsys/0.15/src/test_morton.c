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

#include "rsys_math.h"
#include "morton.h"

#include <stdlib.h>

static INLINE uint16_t
rand_u16()
{
  return (uint16_t)(rand() & (BIT(16)-1));
}

static INLINE uint32_t
rand_u21()
{
  return (uint32_t)(rand() & (BIT(21)-1));
}

static INLINE uint64_t
encode_u21(uint32_t u21)
{
  uint64_t mcode = 0;
  uint64_t i;
  for(i = (uint64_t)round_up_pow2(u21); i != 0; i >>= 1) {
    if(i <= (uint64_t)u21) {
      mcode |= morton3D_encode_u21((uint32_t)i);
      u21 -= (uint32_t)i;
    }
  }
  return mcode;
}

static INLINE uint32_t
encode_u16(uint32_t u16)
{
  uint32_t mcode = 0;
  uint32_t i;
  for(i = (uint32_t)round_up_pow2(u16); i != 0; i >>= 1) {
    if(i <= (uint32_t)u16) {
      mcode |= morton2D_encode_u16((uint16_t)i);
      u16 -= (uint16_t)i;
    }
  }
  return mcode;
}

static void
test_morton2D(void)
{
  int bit = 0;
  int i = 0;

  FOR_EACH(bit, 0, 16) {
    CHK(morton2D_encode_u16(BIT_U16(bit)) == BIT_U32(2*bit));
    CHK(morton2D_decode_u16(BIT_U32(2*bit)) == BIT_U16(bit));
  }

  FOR_EACH(i, 0, 10000) {
    const uint16_t u16 = rand_u16();
    const uint32_t u32 = morton2D_encode_u16(u16);
    CHK(u32 == encode_u16(u16));
    CHK(u16 == morton2D_decode_u16(u32));
  }

  FOR_EACH(i, 0, 10000) {
    uint16_t xy[2];
    uint16_t xy2[2];
    uint32_t xy_mcode[2];
    uint32_t mcode;

    xy[0] = rand_u16();
    xy[1] = rand_u16();
    mcode = morton_xy_encode_u16(xy);

    xy_mcode[0] = morton2D_encode_u16(xy[0]);
    xy_mcode[1] = morton2D_encode_u16(xy[1]);
    CHK(mcode == ((xy_mcode[0]<<1) | (xy_mcode[1]<<0)));

    morton_xy_decode_u16(mcode, xy2);
    CHK(xy[0] == xy2[0]);
    CHK(xy[1] == xy2[1]);
  }

}

static void
test_morton3D(void)
{
  int bit = 0;
  int i = 0;

  FOR_EACH(bit, 0, 21) {
    CHK(morton3D_encode_u21(BIT_U32(bit)) == BIT_U64(3*bit));
    CHK(morton3D_decode_u21(BIT_U64(3*bit)) == BIT_U32(bit));
  }

  FOR_EACH(i, 0, 10000) {
    const uint32_t u21 = rand_u21();
    const uint64_t u64 = morton3D_encode_u21(u21);
    CHK(u64 == encode_u21(u21));
    CHK(u21 == morton3D_decode_u21(u64));
  }

  FOR_EACH(i, 0, 10000) {
    uint32_t xyz[3];
    uint32_t xyz2[3];
    uint64_t xyz_mcode[3];
    uint64_t mcode;

    xyz[0] = rand_u21();
    xyz[1] = rand_u21();
    xyz[2] = rand_u21();
    mcode = morton_xyz_encode_u21(xyz);

    xyz_mcode[0] = morton3D_encode_u21(xyz[0]);
    xyz_mcode[1] = morton3D_encode_u21(xyz[1]);
    xyz_mcode[2] = morton3D_encode_u21(xyz[2]);
    CHK(mcode == ((xyz_mcode[0]<<2) | (xyz_mcode[1]<<1) | (xyz_mcode[2]<<0)));

    morton_xyz_decode_u21(mcode, xyz2);
    CHK(xyz[0] == xyz2[0]);
    CHK(xyz[1] == xyz2[1]);
    CHK(xyz[2] == xyz2[2]);
  }
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  test_morton2D();
  test_morton3D();
  return 0;
}
