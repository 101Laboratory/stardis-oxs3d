/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_MISC_H
#define SDIS_MISC_H

#include <rsys/float2.h>
#include <rsys/float3.h>

/* Empirical scale factor to apply to the upper bound of the ray range in order
 * to handle numerical imprecisions */
#define RAY_RANGE_MAX_SCALE 1.001f

/* Define a new result code from RES_BAD_OP saying that the bad operation is
 * definitive, i.e. in the current state, the realisation will inevitably fail.
 * It is thus unecessary to retry a specific section of the random walk */
#define RES_BAD_OP_IRRECOVERABLE (-RES_BAD_OP)

#define BOLTZMANN_CONSTANT 5.6696e-8 /* W/m^2/K^4 */

/* Reflect the V wrt the normal N. By convention V points outward the surface */
static FINLINE float*
reflect_2d(float res[2], const float V[2], const float N[2])
{
  float tmp[2];
  float cos_V_N;
  ASSERT(res && V && N);
  ASSERT(f2_is_normalized(V) && f2_is_normalized(N));
  cos_V_N = f2_dot(V, N);
  f2_mulf(tmp, N, 2*cos_V_N);
  f2_sub(res, tmp, V);
  return res;
}

/* Reflect the V wrt the normal N. By convention V points outward the surface */
static FINLINE float*
reflect_3d(float res[3], const float V[3], const float N[3])
{
  float tmp[3];
  float cos_V_N;
  ASSERT(res && V && N);
  ASSERT(f3_is_normalized(V) && f3_is_normalized(N));
  cos_V_N = f3_dot(V, N);
  f3_mulf(tmp, N, 2*cos_V_N);
  f3_sub(res, tmp, V);
  return res;
}

static FINLINE double*
move_pos_2d(double pos[2], const float dir[2], const float delta)
{
  ASSERT(pos && dir);
  pos[0] += dir[0] * delta;
  pos[1] += dir[1] * delta;
  return pos;
}

static FINLINE double*
move_pos_3d(double pos[3], const float dir[3], const float delta)
{
  ASSERT(pos && dir);
  pos[0] += dir[0] * delta;
  pos[1] += dir[1] * delta;
  pos[2] += dir[2] * delta;
  return pos;
}

#endif /* SDIS_MISC_H */
