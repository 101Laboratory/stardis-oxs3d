/* Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#ifndef TEST_SCAM_CBOX_H
#define TEST_SCAM_CBOX_H

#include <rsys/rsys.h>

/*******************************************************************************
 * Mesh & properties
 ******************************************************************************/
static const float cbox_vtxs[] = {
  /* Walls */
  552.f, 0.f,   0.f,
  0.f,   0.f,   0.f,
  0.f,   559.f, 0.f,
  552.f, 559.f, 0.f,
  552.f, 0.f,   548.f,
  0.f,   0.f,   548.f,
  0.f,   559.f, 548.f,
  552.f, 559.f, 548.f,

  /* Short block */
  130.f, 65.f,  0.f,
  82.f,  225.f, 0.f,
  240.f, 272.f, 0.f,
  290.f, 114.f, 0.f,
  130.f, 65.f,  165.f,
  82.f,  225.f, 165.f,
  240.f, 272.f, 165.f,
  290.f, 114.f, 165.f,

  /* Tall block */
  423.0f, 247.0f, 0.f,
  265.0f, 296.0f, 0.f,
  314.0f, 456.0f, 0.f,
  472.0f, 406.0f, 0.f,
  423.0f, 247.0f, 330.f,
  265.0f, 296.0f, 330.f,
  314.0f, 456.0f, 330.f,
  472.0f, 406.0f, 330.f
};
const unsigned cbox_tris[] = {
  /* Walls */
  0, 1, 2, 2, 3, 0, /* Bottom */
  4, 5, 6, 6, 7, 4, /* Top */
  1, 2, 6, 6, 5, 1, /* Left */
  0, 3, 7, 7, 4, 0, /* Right */
  2, 3, 7, 7, 6, 2,  /* Back */

  /* Short block */
  12, 13, 14, 14, 15, 12,
  9, 10, 14, 14, 13, 9,
  8, 11, 15, 15, 12, 8,
  10, 11, 15, 15, 14, 10,
  8, 9, 13, 13, 12, 8,

  /* Tall block */
  20, 21, 22, 22, 23, 20,
  17, 18, 22, 22, 21, 17,
  16, 19, 23, 23, 20, 16,
  18, 19, 23, 23, 22, 18,
  16, 17, 21, 21, 20, 16
};

#define WHITE 1.0,1.0,1.0
#define RED   1.0,0.0,0.0
#define GREEN 0.0,1.0,0.0

const double cbox_cols[] = {
  /* Walls */
  WHITE, WHITE, /* Bottom */
  WHITE, WHITE, /* Top */
  RED, RED, /* Left */
  GREEN, GREEN, /* Right */
  WHITE, WHITE, /* Back */

  /* Short block */
  WHITE, WHITE,
  WHITE, WHITE,
  WHITE, WHITE,
  WHITE, WHITE,
  WHITE, WHITE,

  /* Tall block */
  WHITE, WHITE,
  WHITE, WHITE,
  WHITE, WHITE,
  WHITE, WHITE,
  WHITE, WHITE
};

#undef WHITE
#undef RED
#undef GREEN

const unsigned cbox_nvtxs = sizeof(cbox_vtxs) / (sizeof(float)*3);
const unsigned cbox_ntris = sizeof(cbox_tris) / (sizeof(unsigned)*3);
STATIC_ASSERT
  (  sizeof(cbox_tris)/(sizeof(unsigned)*3)
  == sizeof(cbox_cols)/(sizeof(double)*3), Unexpected_data_layout);

/*******************************************************************************
 * Functions
 ******************************************************************************/
static INLINE void
cbox_get_vtx(const unsigned ivtx, float vtx[3], void* data)
{
  const unsigned id = ivtx * 3;
  (void)data;
  CHK(ivtx < cbox_nvtxs);
  vtx[0] = cbox_vtxs[id+0];
  vtx[1] = cbox_vtxs[id+1];
  vtx[2] = cbox_vtxs[id+2];
}

static INLINE void
cbox_get_tri(const unsigned itri, unsigned tri[3], void* data)
{
  const unsigned id = itri * 3;
  (void)data;
  CHK(itri < cbox_ntris);
  tri[0] = cbox_tris[id+0];
  tri[1] = cbox_tris[id+1];
  tri[2] = cbox_tris[id+2];
}

static INLINE void
cbox_get_col(const unsigned itri, double col[3])
{
  const unsigned id = itri * 3;
  CHK(itri < cbox_ntris);
  col[0] = cbox_cols[id+0];
  col[1] = cbox_cols[id+1];
  col[2] = cbox_cols[id+2];
}

static INLINE void
cbox_dump(FILE* stream)
{
  unsigned i;

  FOR_EACH(i, 0, cbox_nvtxs) {
    fprintf(stream, "v %g %g %g\n",
      cbox_vtxs[i*3+0],
      cbox_vtxs[i*3+1],
      cbox_vtxs[i*3+2]);
  }

  FOR_EACH(i, 0, cbox_ntris) {
    fprintf(stream, "f %u %u %u\n",
      cbox_tris[i*3+0]+1,
      cbox_tris[i*3+1]+1,
      cbox_tris[i*3+2]+1);
  }
}

#endif /* TEST_SCAM_CBOX_H */
