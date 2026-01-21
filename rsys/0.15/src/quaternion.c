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

#include "quaternion.h"

float*
quat_slerp(float dst[4], const float from[4], const float to[4], const float t)
{
  float cos_omega;
  float scale0, scale1;
  float tmp0[4], tmp1[4];
  ASSERT(dst && from && to && t >= 0.f && t <= 1.f);

  if(eq_eps(t, 0.f, 1.e-6f))
    return f4_set(dst, from);
  if(eq_eps(t, 1.f, 1.e-6f))
    return f4_set(dst, to);

  cos_omega = f4_dot(from, to);
  if(cos_omega < 0.f) {
    f4_minus(tmp0, to);
    cos_omega = -cos_omega;
  } else {
    f4_set(tmp0, to);
  }
  if((1.f - cos_omega) > 1.e-6f) {
    const double omega = acos((double)cos_omega);
    const double sin_omega = sin(omega);
    scale0 = (float)(sin(omega * t) / sin_omega);
    scale1 = (float)(sin((1.0 - t) * omega) / sin_omega);
  } else {
    scale0 = t;
    scale1 = 1 - t;

  }
  f4_mulf(tmp0, tmp0, scale0);
  f4_mulf(tmp1, from, scale1);
  return f4_add(dst, tmp0, tmp1);
}

float*
quat_to_f33(float mat33[9], const float quat[4])
{
  float i2j2k2[3];
  float ijka[4];
  ASSERT(mat33 && quat);

  f3_mul(i2j2k2, quat, quat);
  f4_set(ijka, quat);

  mat33[0] = 1.f - 2.f * (i2j2k2[1] + i2j2k2[2]);
  mat33[1] = 2.f * (ijka[0]*ijka[1] + ijka[2]*ijka[3]);
  mat33[2] = 2.f * (ijka[0]*ijka[2] - ijka[1]*ijka[3]);

  mat33[3] = 2.f * (ijka[0]*ijka[1] - ijka[2]*ijka[3]);
  mat33[4] = 1.f - 2.f * (i2j2k2[0] + i2j2k2[2]);
  mat33[5] = 2.f * (ijka[1]*ijka[2] + ijka[0]*ijka[3]);

  mat33[6] = 2.f * (ijka[0]*ijka[2] + ijka[1]*ijka[3]);
  mat33[7] = 2.f * (ijka[1]*ijka[2] - ijka[0]*ijka[3]);
  mat33[8] = 1.f - 2.f * (i2j2k2[0] + i2j2k2[1]);

  return mat33;
}
