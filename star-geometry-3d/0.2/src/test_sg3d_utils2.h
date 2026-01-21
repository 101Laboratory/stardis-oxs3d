/* Copyright (C) 2019, 2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_SG3D_UTILS2_H
#define TEST_SG3D_UTILS2_H

#include <star/sg3d.h>

#include <rsys/double3.h>
#include <star/s3dut.h>

struct s3dut_context {
  struct s3dut_mesh_data data;
  struct context ctx;
};

static INLINE void
get_s3dut_indices(const unsigned itri, unsigned ids[3], void* context)
{
  const struct s3dut_context* ctx = context;
  ASSERT(ids && ctx);
  ASSERT(itri < ctx->data.nprimitives);
  ASSERT(ctx->data.indices[itri * 3 + 0] < UINT_MAX
    && ctx->data.indices[itri * 3 + 1] < UINT_MAX
    && ctx->data.indices[itri * 3 + 2] < UINT_MAX);
  ids[0] = (unsigned)ctx->data.indices[itri * 3 + 0];
  ids[ctx->ctx.reverse_vrtx ? 2 : 1] = (unsigned)ctx->data.indices[itri * 3 + 1];
  ids[ctx->ctx.reverse_vrtx ? 1 : 2] = (unsigned)ctx->data.indices[itri * 3 + 2];
}

static INLINE void
get_s3dut_position(const unsigned ivert, double pos[3], void* context)
{
  const struct s3dut_context* ctx = context;
  double tmp[3];
  ASSERT(pos && ctx);
  ASSERT(ivert < ctx->data.nvertices);
  (void)ivert;
  d3_add(pos, d3_mul(tmp, ctx->data.positions + ivert * 3, ctx->ctx.scale),
    ctx->ctx.offset);
}

static INLINE void
get_s3dut_properties
  (const unsigned itri,
   unsigned property[SG3D_PROP_TYPES_COUNT__],
   void* context)
{
  const struct s3dut_context* ctx = context;
  ASSERT(property && ctx);
  (void)itri;
  property[ctx->ctx.reverse_med ? SG3D_BACK : SG3D_FRONT] = *ctx->ctx.front_media;
  property[ctx->ctx.reverse_med ? SG3D_FRONT : SG3D_BACK] = *ctx->ctx.back_media;
  property[SG3D_INTFACE] = *ctx->ctx.intface;
}

#endif /* TEST_SG3D_UTILS2_H */
