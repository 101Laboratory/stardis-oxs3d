/* Copyright (C) 2018-2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_UTILS2_H
#define TEST_UTILS2_H

#if !defined(NB_CYL_X) ||  !defined(NB_CYL_Y) || !defined(NB_CYL_Z) | !defined(NB_CYL)
#error "Macro definitions are missing"
#endif

#include "test_senc3d_utils.h"

#include <star/s3dut.h>
#include <rsys/double3.h>

struct s3dut_context {
  struct s3dut_mesh_data data;
  struct context ctx;
};

static void
get_s3dut_indices(const unsigned itri, unsigned ids[3], void* context)
{
  struct s3dut_context* ctx = context;
  unsigned s3dut_itri, cyl_idx, v_offset;
  ASSERT(ids && ctx);
  ASSERT(itri < NB_CYL * ctx->data.nprimitives);
  /* Get cyl_idx along with the s3dut vertice index */
  s3dut_itri = itri % (unsigned)ctx->data.nprimitives;
  cyl_idx = itri / (unsigned)ctx->data.nprimitives;
  ASSERT(ctx->data.indices[s3dut_itri * 3 + 0] <= UINT_MAX
    && ctx->data.indices[s3dut_itri * 3 + 1] <= UINT_MAX
    && ctx->data.indices[s3dut_itri * 3 + 2] <= UINT_MAX);
  /* Compute the vertex index in the user numbering
   * from cyl_idx and s3dut data; vertex related getters
   * will have to get the s3dut index back */
  v_offset = cyl_idx * (unsigned)ctx->data.nvertices;
  ids[0] = v_offset + (unsigned)ctx->data.indices[s3dut_itri * 3 + 0];
  ids[ctx->ctx.reverse_vrtx ? 2 : 1]
    = v_offset + (unsigned)ctx->data.indices[s3dut_itri * 3 + 1];
  ids[ctx->ctx.reverse_vrtx ? 1 : 2]
    = v_offset + (unsigned)ctx->data.indices[s3dut_itri * 3 + 2];
}

static void
get_s3dut_position(const unsigned ivert, double pos[3], void* context)
{
  struct s3dut_context* ctx = context;
  unsigned s3dut_ivert, cyl_idx;
  int i, j, k;
  double offset[3], tmp[3];
  double center_x, center_y, scale, misalignment = 0;
  ASSERT(pos && ctx);
  ASSERT(ivert < NB_CYL * ctx->data.nvertices);
  /* Get cyl_idx and cylinder imbrication along with the s3dut vertice index */
  s3dut_ivert = ivert % (unsigned)ctx->data.nvertices;
  cyl_idx = ivert / (unsigned)ctx->data.nvertices;
  /* k th cylinder of the imbrication at grid position i,j */
  i = (int)cyl_idx / (NB_CYL_Y * NB_CYL_Z);
  j = (cyl_idx / NB_CYL_Z) % NB_CYL_Y;
  k = cyl_idx % NB_CYL_Z;
  ASSERT(i < NB_CYL_X && j < NB_CYL_Y && k < NB_CYL_Z);
  ASSERT((unsigned)(i * NB_CYL_Y * NB_CYL_Z + j * NB_CYL_Z + k)
    * ctx->data.nvertices + s3dut_ivert == ivert);
  center_x = 2 * (1 + NB_CYL_Z) * (i - NB_CYL_X / 2);
  center_y = 2 * (1 + NB_CYL_Z) * (j - NB_CYL_Y / 2);
  /* Compute scale and offset from imbrication */
  scale = k + 1;
#ifdef MITIGATE_EMBREE_181
  /* Mitigate Embree issue #181
   * We cannot keep perfect alignment of cylinders
   * or some hits are missed */
  misalignment = (k % 2) ? -0.01 : +0.01;
#endif
  d3(offset, center_x + misalignment, center_y + misalignment, 0);
  d3_add(pos, d3_muld(tmp, ctx->data.positions + s3dut_ivert * 3, scale),
    offset);
}

static void
get_s3dut_media(const unsigned itri, unsigned medium[2], void* context)
{
  struct s3dut_context* ctx = context;
  unsigned cyl_idx;
  int k;
  ASSERT(medium && ctx);
  ASSERT(itri < NB_CYL * ctx->data.nprimitives);
  /* Get cyl_idx */
  cyl_idx = itri / (unsigned)ctx->data.nprimitives;
  /* k th cylinder of the imbrication at some grid position */
  k = cyl_idx % NB_CYL_Z;
  medium[ctx->ctx.reverse_med ? SENC3D_BACK : SENC3D_FRONT] = (unsigned)k;
  medium[ctx->ctx.reverse_med ? SENC3D_FRONT : SENC3D_BACK] = (unsigned)(k + 1);
}

#endif /* TEST_UTILS2_H */
