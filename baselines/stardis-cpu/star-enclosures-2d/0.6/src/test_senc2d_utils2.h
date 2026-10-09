/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_SENC2_UTILS2_H
#define TEST_SENC2_UTILS2_H

#if !defined(NB_CIRC_X) ||  !defined(NB_CIRC_Y) || !defined(NB_CIRC_Z) | !defined(NB_CIRC)
#error "Macro definitions are missing"
#endif

#include "test_senc2d_utils.h"

#include <rsys/double2.h>
#include <rsys/stretchy_array.h>

static void
get_ctx_indices(const unsigned iseg, unsigned ids[2], void* context)
{
  struct context* ctx = context;
  unsigned template_iseg, circ_idx, v_offset;
  unsigned circ_seg_count, circ_vrtx_count;
  ASSERT(ids && ctx);
  /* Get circ_idx along with the template vertice index */
  circ_seg_count = (unsigned)sa_size(ctx->indices) / 2;
  circ_vrtx_count = (unsigned)sa_size(ctx->positions) / 2;
  ASSERT(iseg < NB_CIRC * circ_seg_count);
  template_iseg = iseg % circ_seg_count;
  circ_idx = iseg / circ_seg_count;
  ASSERT(ctx->indices[template_iseg * 2 + 0] <= UINT_MAX
    && ctx->indices[template_iseg * 2 + 1] <= UINT_MAX);
  /* Compute the vertex index in the user numbering
   * from circ_idx and template data; vertex related getters
   * will have to get the template index back */
  v_offset = circ_idx * circ_vrtx_count;
  ids[ctx->reverse_vrtx ? 1 : 0]
    = v_offset + (unsigned)ctx->indices[template_iseg * 2 + 0];
  ids[ctx->reverse_vrtx ? 0 : 1]
    = v_offset + (unsigned)ctx->indices[template_iseg * 2 + 1];
}

static void
get_ctx_position(const unsigned ivert, double pos[2], void* context)
{
  struct context* ctx = context;
  unsigned ctx_ivert, circ_idx;
  unsigned circ_seg_count, circ_vrtx_count;
  int i, j, k;
  double offset[2], tmp[2];
  double center_x, center_y, scale, misalignment = 0;
  ASSERT(pos && ctx); (void)circ_vrtx_count;
  /* Get circ_idx and circle imbrication along with the vertice index */
  circ_seg_count = (unsigned)sa_size(ctx->indices) / 2;
  circ_vrtx_count = (unsigned)sa_size(ctx->positions) / 2;
  ASSERT(ivert < NB_CIRC * circ_vrtx_count);
  ctx_ivert = ivert % circ_seg_count;
  circ_idx = ivert / circ_seg_count;
  /* k th circle of the imbrication at grid position i,j */
  i = (int)circ_idx / (NB_CIRC_Y * NB_CIRC_Z);
  j = (circ_idx / NB_CIRC_Z) % NB_CIRC_Y;
  k = circ_idx % NB_CIRC_Z;
  ASSERT(i < NB_CIRC_X && j < NB_CIRC_Y && k < NB_CIRC_Z);
  ASSERT((unsigned)(i * NB_CIRC_Y * NB_CIRC_Z + j * NB_CIRC_Z + k)
    * circ_vrtx_count + ctx_ivert == ivert);
  center_x = 2 * (1 + NB_CIRC_Z) * (i - NB_CIRC_X / 2);
  center_y = 2 * (1 + NB_CIRC_Z) * (j - NB_CIRC_Y / 2);
  /* Compute scale and offset from imbrication */
  scale = k + 1;
#ifdef MITIGATE_EMBREE_181
  /* Mitigate Embree issue #181
   * We cannot keep perfect alignment of circles
   * or some hits are missed */
  misalignment = (k % 2) ? -0.01 : +0.01;
#endif
  d2(offset, center_x + misalignment, center_y + misalignment);
  d2_add(pos, d2_muld(tmp, ctx->positions + ctx_ivert * 2, scale),
    offset);
}

static void
get_ctx_media(const unsigned iseg, unsigned medium[2], void* context)
{
  struct context* ctx = context;
  unsigned circ_idx;
  unsigned circ_seg_count;
  int k;
  ASSERT(medium && ctx);
  /* Get circ_idx */
  circ_seg_count = (unsigned)sa_size(ctx->indices) / 2;
  ASSERT(iseg < NB_CIRC * circ_seg_count);
  circ_idx = iseg / circ_seg_count;
  /* k th circle of the imbrication at some grid position */
  k = circ_idx % NB_CIRC_Z;
  medium[ctx->reverse_med ? SENC2D_BACK : SENC2D_FRONT] = (unsigned)k;
  medium[ctx->reverse_med ? SENC2D_FRONT : SENC2D_BACK] = (unsigned)(k + 1);
}

#endif /* TEST_SENC2_UTILS2_H */
