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

#include "rsys.h"
#include <float.h>

#define REALX_DIMENSION__ 4
#include "realX_begin.h"
#define REAL REAL_TYPE__
#define REAL_COMPATIBLE REAL_TYPE_COMPATIBLE__

#define CHECK_REAL4(a, b)                                                      \
  {                                                                            \
    REAL* a__ = (a);                                                           \
    REAL* b__ = (b);                                                           \
    CHK((a__)[0] ==  (b__)[0]);                                                \
    CHK((a__)[1] ==  (b__)[1]);                                                \
    CHK((a__)[2] ==  (b__)[2]);                                                \
    CHK((a__)[3] ==  (b__)[3]);                                                \
  } (void) 0

int
main(int argc, char** argv)
{
  REAL a[4], b[4], dst[4], f, c[4];
  REAL_COMPATIBLE d[4];
  (void)argc, (void)argv;

  REALX_FUNC__(set)(a, REALX_FUNC__(splat)(c, -1.0));
  CHK(a[0] ==  -1.0);
  CHK(a[1] ==  -1.0);
  CHK(a[2] ==  -1.0);
  CHK(a[3] ==  -1.0);
  REALX_FUNC__(set)(a, REALX_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
  CHK(a[0] ==  0.0);
  CHK(a[1] ==  1.0);
  CHK(a[2] ==  2.0);
  CHK(a[3] ==  3.0);
  REALX_FUNC__(splat)(a, -2.0);
  CHK(a[0] ==  -2.0);
  CHK(a[1] ==  -2.0);
  CHK(a[2] ==  -2.0);
  CHK(a[3] ==  -2.0);

  REALX_FUNC__(set)(a, REALX_CTOR__(c, -1.0, 2.0, -3.0, 4.0));
  CHECK_REAL4(REALX_FUNC__(minus)(b, a), REALX_CTOR__(c, 1.0, -2.0, 3.0, -4.0));
  CHECK_REAL4(b, REALX_CTOR__(c, 1.0, -2.0, 3.0, -4.0));

  d[0] = (REAL_COMPATIBLE)0.1;
  d[1] = (REAL_COMPATIBLE)(1.0/3.0);
  d[2] = (REAL_COMPATIBLE)0.3;
  d[3] = (REAL_COMPATIBLE)-0.7;
  REALX_CTOR__(c,
    (REAL)(REAL_COMPATIBLE)0.1,
    (REAL)(REAL_COMPATIBLE)(1.0/3.0),
    (REAL)(REAL_COMPATIBLE)0.3,
    (REAL)(REAL_COMPATIBLE)-0.7);
  CHECK_REAL4(REALX_CAST__(dst, d), c);


  CHECK_REAL4(REALX_FUNC__(add)(dst, a, b), REALX_FUNC__(splat)(c, 0.0));
  CHECK_REAL4(dst, REALX_FUNC__(splat)(c, 0.0));
  CHECK_REAL4(REALX_REAL_FUNC__(add)(dst, a, 1.0), REALX_CTOR__(c, 0.0, 3.0, -2.0, 5.0));
  CHECK_REAL4(dst, REALX_CTOR__(c, 0.0, 3.F, -2.0, 5.0));
  CHECK_REAL4(REALX_FUNC__(sub)(dst, a, b), REALX_CTOR__(c, -2.0, 4.0, -6.0, 8.0));
  CHECK_REAL4(dst, REALX_CTOR__(c, -2.0, 4.0, -6.0, 8.0));
  CHECK_REAL4(REALX_REAL_FUNC__(sub)(dst, a, 1.0), REALX_CTOR__(c, -2.0, 1.0, -4.0, 3.0));
  CHECK_REAL4(dst, REALX_CTOR__(c, -2.0, 1.0, -4.0, 3.0));
  CHECK_REAL4(REALX_REAL_FUNC__(mul)(dst, a, 2.0), REALX_CTOR__(c, -2.0, 4.0, -6.0, 8.0));
  CHECK_REAL4(dst, REALX_CTOR__(c, -2.0, 4.0, -6.0, 8.0));
  CHECK_REAL4(REALX_FUNC__(mul)(dst, a, b), REALX_CTOR__(c, -1.0, -4.0, -9.0, -16.0));
  CHECK_REAL4(dst, REALX_CTOR__(c, -1.0, -4.0, -9.0, -16.0));
  CHECK_REAL4(REALX_FUNC__(div)(dst, dst, a), REALX_CTOR__(c, 1.0, -2.0, 3.0, -4.0));
  CHECK_REAL4(dst, REALX_CTOR__(c, 1.0, -2.0, 3.0, -4.0));
  CHECK_REAL4(REALX_REAL_FUNC__(div)(dst, a, 2.0), REALX_CTOR__(c, -0.5, 1.0, -1.5, 2.0));
  CHECK_REAL4(dst, REALX_CTOR__(c, -0.5, 1.0, -1.5, 2.0));

  REALX_FUNC__(set)(a, REALX_CTOR__(c, 0.0, 1.0, 2.0, 4.0));
  REALX_FUNC__(set)(b, REALX_CTOR__(c, 1.0, 2.0, -1.0, 1.0));
  CHECK_REAL4(REALX_FUNC__(lerp)(dst, a, b, 0.5), REALX_CTOR__(c, 0.5, 1.5, 0.5, 2.5));
  CHECK_REAL4(dst, REALX_CTOR__(c, 0.5, 1.5, 0.5, 2.5));
  CHK(REALX_FUNC__(sum)(b) ==  3.0);
  CHK(REALX_FUNC__(dot)(a, b) ==  4.0);
  CHK(eq_eps(REALX_FUNC__(len)(a), sqrt(21.0), REAL_EPSILON__) ==  1);

  REALX_FUNC__(set)(a, REALX_CTOR__(c, 0.0, 4.0, 2.0, 3.0));
  CHK(REALX_FUNC__(is_normalized)(a) ==  0);
  f = REALX_FUNC__(normalize)(dst, a);
  CHK(REALX_FUNC__(is_normalized)(a) ==  0);
  CHK(REALX_FUNC__(is_normalized)(dst) != 0);
  CHK(eq_eps(f, sqrt(29.0), REAL_EPSILON__) ==  1);
  CHK(eq_eps(dst[0], a[0] / f, REAL_EPSILON__) ==  1);
  CHK(eq_eps(dst[1], a[1] / f, REAL_EPSILON__) ==  1);
  CHK(eq_eps(dst[2], a[2] / f, REAL_EPSILON__) ==  1);
  CHK(eq_eps(dst[3], a[3] / f, REAL_EPSILON__) ==  1);

  CHK(REALX_FUNC__(eq)(a, a) ==  1);
  CHK(REALX_FUNC__(eq)(a, b) ==  0);
  CHK(REALX_FUNC__(eq)(a, REALX_CTOR__(c, b[0], a[1], a[2], a[3])) ==  0);
  CHK(REALX_FUNC__(eq)(a, REALX_CTOR__(c, a[0], b[1], a[2], a[3])) ==  0);
  CHK(REALX_FUNC__(eq)(a, REALX_CTOR__(c, a[0], a[1], b[2], a[3])) ==  0);
  CHK(REALX_FUNC__(eq)(a, REALX_CTOR__(c, a[0], a[1], a[2], b[3])) ==  0);

  REALX_FUNC__(set)(b, a);
  REALX_FUNC__(add)(b, b, REALX_FUNC__(splat)(c, (REAL)(REAL_EPSILON__ * 0.5)));
  CHK(REALX_FUNC__(eq)(a, b) ==  0);
  CHK(REALX_FUNC__(eq_eps)(a, b, REAL_EPSILON__) ==  1);
  CHK(REALX_FUNC__(eq_eps)(a, b, (REAL)(REAL_EPSILON__ * 0.25)) ==  0);

  REALX_FUNC__(set)(a, REALX_CTOR__(c, 1.0, 3.0, -2.0, (REAL)0.1));
  REALX_FUNC__(set)(b, REALX_CTOR__(c, -1.0, (REAL)3.1, (REAL)-2.1, 1.0));
  CHECK_REAL4(REALX_FUNC__(max)(dst, a, b), REALX_CTOR__(c, 1.0, (REAL)3.1, -2.0, (REAL)1.0));
  CHECK_REAL4(dst, REALX_CTOR__(c, 1.0, (REAL)3.1, -2.0, 1.0));
  CHECK_REAL4(REALX_FUNC__(min)(dst, a, b), REALX_CTOR__(c, -1.0, 3.0, (REAL)-2.1, (REAL)0.1));
  CHECK_REAL4(dst, REALX_CTOR__(c, -1.0, 3.0, (REAL)-2.1, (REAL)0.1));
  return 0;
}
