/* Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_S3D_CBOX_H
#define TEST_S3D_CBOX_H

#include <rsys/rsys.h>
#include <stdint.h>

struct cbox_desc {
  const float* vertices;
  const unsigned* indices;
};

/*******************************************************************************
 * Box
 ******************************************************************************/
static const float cbox_walls[] = {
  552.f, 0.f,   0.f,
  0.f,   0.f,   0.f,
  0.f,   559.f, 0.f,
  552.f, 559.f, 0.f,
  552.f, 0.f,   548.f,
  0.f,   0.f,   548.f,
  0.f,   559.f, 548.f,
  552.f, 559.f, 548.f
};
const unsigned cbox_walls_nverts = sizeof(cbox_walls) / (sizeof(float)*3);

const unsigned cbox_walls_ids[] = {
  0, 1, 2, 2, 3, 0, /* Bottom */
  4, 5, 6, 6, 7, 4, /* Top */
  1, 2, 6, 6, 5, 1, /* Left */
  0, 3, 7, 7, 4, 0, /* Right */
  2, 3, 7, 7, 6, 2  /* Back */
};
const unsigned cbox_walls_ntris = sizeof(cbox_walls_ids) / (sizeof(unsigned)*3);

static const struct cbox_desc cbox_walls_desc = { cbox_walls, cbox_walls_ids };

/*******************************************************************************
 * Short/tall blocks
 ******************************************************************************/
static const float cbox_short_block[] = {
  130.f, 65.f,  0.f,
  82.f,  225.f, 0.f,
  240.f, 272.f, 0.f,
  290.f, 114.f, 0.f,
  130.f, 65.f,  165.f,
  82.f,  225.f, 165.f,
  240.f, 272.f, 165.f,
  290.f, 114.f, 165.f
};

static const float cbox_tall_block[] = {
  423.0f, 247.0f, 0.f,
  265.0f, 296.0f, 0.f,
  314.0f, 456.0f, 0.f,
  472.0f, 406.0f, 0.f,
  423.0f, 247.0f, 330.f,
  265.0f, 296.0f, 330.f,
  314.0f, 456.0f, 330.f,
  472.0f, 406.0f, 330.f
};

static const unsigned cbox_block_ids[] = {
  4, 5, 6, 6, 7, 4,
  1, 2, 6, 6, 5, 1,
  0, 3, 7, 7, 4, 0,
  2, 3, 7, 7, 6, 2,
  0, 1, 5, 5, 4, 0
};

const unsigned cbox_block_nverts = sizeof(cbox_short_block) / (sizeof(float)*3);
const unsigned cbox_block_ntris = sizeof(cbox_block_ids) / (sizeof(unsigned)*3);

/*******************************************************************************
 * Callbacks
 ******************************************************************************/
static INLINE void
cbox_get_ids(const unsigned itri, unsigned ids[3], void* data)
{
  const unsigned id = itri * 3;
  struct cbox_desc* desc = data;
  CHK(desc != NULL);
  ids[0] = desc->indices[id + 0];
  ids[1] = desc->indices[id + 1];
  ids[2] = desc->indices[id + 2];
}

static INLINE void
cbox_get_position(const unsigned ivert, float position[3], void* data)
{
  struct cbox_desc* desc = data;
  CHK(desc != NULL);
  position[0] = desc->vertices[ivert*3 + 0];
  position[1] = desc->vertices[ivert*3 + 1];
  position[2] = desc->vertices[ivert*3 + 2];
}

static INLINE void
cbox_get_normal(const unsigned ivert, float normal[3], void* data)
{
  (void)ivert, (void)data;
  normal[0] = 1.f;
  normal[1] = 0.f;
  normal[2] = 0.f;
}

static INLINE void
cbox_get_uv(const unsigned ivert, float uv[2], void* data)
{
  (void)ivert, (void)data;
  uv[0] = -1.f;
  uv[1] = 1.f;
}

#endif /* TEST_S3D_CBOX_H */

