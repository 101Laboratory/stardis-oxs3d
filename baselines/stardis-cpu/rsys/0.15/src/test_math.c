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

#ifdef COMPILER_MSVC
  #pragma warning(disable:4127) /* Constant conditional expression */
  #pragma warning(disable:4723) /* Division by zero */
#endif

int
main(int argc, char** argv)
{
  double d;
  float f, g;
  int i;
  volatile float zero_f = 0.f;
  volatile double zero_d = 0.0;
  (void)argc, (void)argv;

  f = -3.14159f; CHK(absf(f) == -f);
  f =  3.14159f; CHK(absf(f) == f);
  f = -3.14159f; g = 2.71828f;
  CHK(MMAX(f, g) == g);
  CHK(MMIN(f, g) == f);
  CHK(MMAX(-10, 0) == 0);
  CHK(MMAX(10, 0) == 10);
  CHK(MMIN(-10, 0) == -10);
  CHK(MMIN(10, 0) == 0);

  CHK(CLAMP(0.1f, 0.f, 1.f) == 0.1f);
  CHK(CLAMP(-0.1f, 0.f, 1.f) == 0.f);
  CHK(CLAMP(1.2f, 0.f, 1.f) == 1.f);
  CHK(CLAMP(1.f, 0.f, 1.f) == 1.f);
  CHK(CLAMP(0, -127, 127) == 0);
  CHK(CLAMP(255, -127, 127) == 127);
  CHK(CLAMP(-255, -127, 127) == -127);

  CHK(eq_epsf((float)PI, 3.14159265358979323846f, 1.e-6f) == 1);
  CHK(eq_epsf((float)RCP_PI, 1.f / (float)PI, 1.e-6f) == 1);
  CHK(eq_eps(PI, 3.14159265358979323846, 1.e-8) == 1);
  CHK(eq_eps(RCP_PI, 1.0/PI, 1.e-8) == 1);

  CHK(1.f/zero_f == (float)INF);
  CHK(-1.f/zero_f == (float)-INF);
  CHK(1.0/zero_d != -INF);
  CHK(1.0/zero_d == INF);
  CHK(-1.0/zero_d != INF);
  CHK(-1.0/zero_d == -INF);
  CHK(IS_INF(PI/zero_d) != 0);
  CHK(IS_INF(-PI/zero_d) != 0);
  CHK(IS_INF(PI) == 0);

  d = NaN;
  CHK(d != d);
  CHK(IS_NaN(d) == 1);
  d = -NaN;
  CHK(d!=d);
  CHK(IS_NaN(d) == 1);
  d = sqrt(-1);
  CHK(IS_NaN(d) == 1);
  f = (float)NaN;
  CHK(f!=f);
  CHK(IS_NaN(f) == 1);
  f = (float)sqrt(-1);
  CHK(f!=f);
  CHK(IS_NaN(f) == 1);
  CHK(IS_NaN(NaN) == 1);
  CHK(IS_NaN(-NaN) == 1);
  CHK(IS_NaN(INF) == 0);
  CHK(IS_NaN(-INF) == 0);
  CHK(IS_NaN(1.0/3.0) == 0);

  CHK(IS_POW2(0) == 0);
  CHK(IS_POW2(1) == 1);
  CHK(IS_POW2(2) == 1);
  CHK(IS_POW2(3) == 0);
  CHK(IS_POW2(31) == 0);
  CHK(IS_POW2(64) == 1);
  CHK(IS_POW2(1 << 16) == 1);

  CHK(log2i(3) == 1);
  CHK(log2i(4) == 2);
  CHK(log2i(5) == 2);
  CHK(log2i(7) == 2);
  CHK(log2i(8) == 3);
  CHK(log2i(12) == 3);
  CHK(log2i(511) == 8);
  CHK(log2i(512) == 9);

  CHK(round_up_pow2(0) == 1);
  CHK(round_up_pow2(3) == 4);
  CHK(round_up_pow2(4) == 4);
  CHK(round_up_pow2(100) == 128);

  CHK(absf(-1.f) == 1.f);
  CHK(absf(10.f) == 10.f);
  CHK(absf(-3.14159f) == (float)3.14159f);

  CHK(eq_eps(MDEG2RAD(90.0), PI*0.5, 1.e-8) == 1);
  CHK(eq_eps(MDEG2RAD(45.0), PI*0.25, 1.e-8) == 1);
  CHK(eq_eps(MRAD2DEG(PI), 180.0, 1.e-8) == 1);
  CHK(eq_eps(MRAD2DEG(PI*0.75), 135.0, 1.e-8) == 1);
  CHK(eq_eps(MRAD2DEG(MDEG2RAD(70.0)), 70.0, 1.e-8) == 1);

  FOR_EACH(i, 0, 16) {
    double c, s, tmp;
    d = ((float)rand() / (float)RAND_MAX) * PI;
    c = cos(d);
    s = sin(d);
    tmp = cos2sin(c);
    CHK(eq_eps(s, tmp, 1.0e-8) || eq_eps(s, -tmp, 1.0e-8) == 1);
    tmp = sin2cos(s);
    CHK(eq_eps(c, tmp, 1.0e-8) || eq_eps(c, -tmp, 1.0e-8) == 1);
  }

  CHK(signf(-3.14159f) == -1.f);
  CHK(signf(1.23f) == 1.f);
  CHK(signf(0.f) == 1.f);
  CHK(sign(-3.14159) == -1.0);
  CHK(sign(1.23) == 1.0);
  CHK(sign(0.0) == 1.0);

  return 0;
}
