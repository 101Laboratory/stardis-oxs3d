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

#ifndef QUATERNION_H
#define QUATERNION_H

#include "float3.h"
#include "float4.h"

/*
 * Quaternion encoded in a float4 as { i, j, k, a }
 */

static FINLINE float*
quat_identity(float quat[4])
{
  return f4(quat, 0.f, 0.f, 0.f, 1.f);
}

static FINLINE float*
quat_set_axis_angle(float quat[4], const float axis[3], const float angle)
{
  const float hangle = angle * 0.5f;
  const float s = (float)sin((double)hangle);
  const float c = (float)cos((double)hangle);
  ASSERT(quat && axis);

  f3_set(quat, axis);
  f3_mulf(quat, quat, s);
  quat[3] = c;
  return quat;
}

static FINLINE float*
quat_conj(float dst[4], const float quat[4]) /* { -ix, -jy, -kz, a } */
{
  ASSERT(dst && quat);
  return f4(dst, -quat[0], -quat[1], -quat[2], quat[3]);
}

static FINLINE float*
quat_mul(float dst[4], const float q0[4], const float q1[4])
{
  float res[4];
  ASSERT(dst && q0 && q1);
  res[0] = q0[3]*q1[0] + q0[0]*q1[3] + q0[1]*q1[2] - q0[2]*q1[1];
  res[1] = q0[3]*q1[1] - q0[0]*q1[2] + q0[1]*q1[3] + q0[2]*q1[0];
  res[2] = q0[3]*q1[2] + q0[0]*q1[1] - q0[1]*q1[0] + q0[2]*q1[3];
  res[3] = q0[3]*q1[3] - q0[0]*q1[0] - q0[1]*q1[1] - q0[2]*q1[2];
  return f4_set__(dst, res);
}

static FINLINE float
quat_calca(const float ijk[3])
{
  float ijk_sqr;
  ASSERT(ijk);
  ijk_sqr = f3_dot(ijk, ijk);
  return (float)sqrt((double)absf(1.f - ijk_sqr));
}

static FINLINE float* /* Normalized Linear interpolation */
quat_nlerp(float dst[4], const float from[4], const float to[4], const float t)
{
  f4_normalize(dst, f4_lerp(dst, from, to, t));
  return dst;
}

RSYS_API float*
quat_slerp /* Spherical linear interplation */
  (float dst[4],
   const float from[4],
   const float to[4],
   const float t);

RSYS_API float*
quat_to_f33
  (float mat33[9] /* column major matrix */,
   const float quat[4]);

#endif /* QUATERNION_H */
