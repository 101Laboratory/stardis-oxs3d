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

int
main(int argc, char** argv)
{
  float v3[3], m33[9];
  float q0[4], q1[4], q2[4];
  (void)argc, (void)argv;

  CHK(quat_identity(q0) == q0);
  CHK(q0[0] == 0.f);
  CHK(q0[1] == 0.f);
  CHK(q0[2] == 0.f);
  CHK(q0[3] == 1.f);

  f3(v3, 2.f, 5.f, 1.f);
  CHK(quat_set_axis_angle(q0, v3, (float)PI*0.3f) == q0);
  CHK(eq_eps(q0[0], 0.907981f, 1.e-6f) == 1);
  CHK(eq_eps(q0[1], 2.269953f, 1.e-6f) == 1);
  CHK(eq_eps(q0[2], 0.453991f, 1.e-6f) == 1);
  CHK(eq_eps(q0[3], 0.891007f, 1.e-6f) == 1);

  f4(q0, 1.f, 2.f, 3.f, 4.f);
  f4(q1, 5.f, 6.f, 7.f, 8.f);
  CHK(quat_mul(q2, q0, q1) == q2);
  CHK(q2[0] == 24.f);
  CHK(q2[1] == 48.f);
  CHK(q2[2] == 48.f);
  CHK(q2[3] == -6.f);

  CHK(quat_conj(q2, q0) == q2);
  CHK(q2[0] == -1.f);
  CHK(q2[1] == -2.f);
  CHK(q2[2] == -3.f);
  CHK(q2[3] == 4.f);

  f4_normalize(q0, f4(q0, 1.f, 2.f, 5.f, 0.5f));
  f3_set(q1, q0);
  q1[3] = quat_calca(q1);
  CHK(eq_eps(q1[3], q0[3], 1.e-6f) == 1);

  f4(q0, 1.f, 2.F, 3.f, 5.f);
  f4(q1, 2.f, 6.f, 7.f, 6.f);
  CHK(quat_slerp(q2, q0, q1, 0.3f) == q2);
  CHK(eq_eps(q2[0], 1.3f, 1.e-6f) == 1);
  CHK(eq_eps(q2[1], 3.2f, 1.e-6f) == 1);
  CHK(eq_eps(q2[2], 4.2f, 1.e-6f) == 1);
  CHK(eq_eps(q2[3], 5.3f, 1.e-6f) == 1);

  f4(q0, 2.f, 5.f, 17.f, 9.f);
  CHK(quat_to_f33(m33, q0) == m33);
  CHK(f3_eq_eps(m33 + 0, f3(v3, -627.f, 326.f, -22.f), 1.e-6f) == 1);
  CHK(f3_eq_eps(m33 + 3, f3(v3, -286.f, -585.f, 206.f), 1.e-6f) == 1);
  CHK(f3_eq_eps(m33 + 6, f3(v3, 158.f, 134.f, -57.f), 1.e-6f) == 1);

  f4_normalize(q0, q0);
  CHK(quat_to_f33(m33, q0) == m33);
  CHK(f3_eq_eps(m33 + 0, f3(v3,-0.573935f, 0.817043f,-0.055138f), 1.e-6f) == 1);
  CHK(f3_eq_eps(m33 + 3, f3(v3,-0.716792f,-0.468672f, 0.516291f), 1.e-6f) == 1);
  CHK(f3_eq_eps(m33 + 6, f3(v3, 0.395990f, 0.335840f, 0.854637f), 1.e-6f) == 1);

  return 0;
}
