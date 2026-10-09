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
#include <rsys_math.h>

#define REALX_DIMENSION__ 2
#define REALY_DIMENSION__ 2
#include "realXY_begin.h"

#define REAL REAL_TYPE__
#define REAL_COMPATIBLE REAL_TYPE_COMPATIBLE__

#define CHECK_REAL2(A, B)                                                      \
  {                                                                            \
    const REAL* a__ = (A);                                                     \
    const REAL* b__ = (B);                                                     \
    int i__;                                                                   \
    FOR_EACH(i__, 0, 2)                                                        \
      CHK(a__[i__] ==  b__[i__]);                                              \
  } (void)0
#define CHECK_REAL22(A, B)                                                     \
  {                                                                            \
    const REAL* a__ = (A);                                                     \
    const REAL* b__ = (B);                                                     \
    int i__;                                                                   \
    FOR_EACH(i__, 0, 4)                                                        \
      CHK(a__[i__] ==  b__[i__]);                                              \
  } (void)0

int
main(int argc, char** argv)
{
  REAL a[4], b[4], dst[4], c[4];
  REAL_COMPATIBLE d[4];
  int i;
  (void)argc, (void)argv;

  REALXY_FUNC__(set)(c, REALXY_FUNC__(splat)(a, -1.0));
  FOR_EACH(i, 0, 4) {
    CHK(a[i] ==  -1.0);
    CHK(c[i] ==  -1.0);
  }
  REALXY_FUNC__(set)(a, REALXY_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
  FOR_EACH(i, 0, 4) {
    CHK(a[i] ==  (REAL)i);
  }
  CHECK_REAL22(REALXY_FUNC__(set_identity)(a),
    REALXY_CTOR__(c, 1.0, 0.0, 0.0, 1.0));
  CHECK_REAL22(a, REALXY_CTOR__(c, 1.0, 0.0, 0.0, 1.0));
  CHK(REALXY_FUNC__(is_identity)(a) ==  1);

  REALXY_FUNC__(set)(c, a);
  FOR_EACH(i, 0, REALX_DIMENSION__*REALY_DIMENSION__) {
    REAL_TYPE__ r = c[i];
    c[i] = c[i] + REAL_EPSILON__;
    CHK(REALXY_FUNC__(is_identity)(c) ==  0);
    c[i] = r;
  }

  d[0] = (REAL_COMPATIBLE)0.1;
  d[1] = (REAL_COMPATIBLE)(1.0/3.0);
  d[2] = (REAL_COMPATIBLE)0.3;
  d[3] = (REAL_COMPATIBLE)-0.7;
  REALXY_CTOR__(c,
    (REAL)(REAL_COMPATIBLE)0.1,
    (REAL)(REAL_COMPATIBLE)(1.0/3.0),
    (REAL)(REAL_COMPATIBLE)0.3,
    (REAL)(REAL_COMPATIBLE)-0.7);
  CHECK_REAL22(REALXY_CAST__(dst, d), c);

  REALXY_FUNC__(splat)(a, -1.0);
  CHECK_REAL22(REALXY_FUNC__(set_row)(a, REALX_CTOR__(c, 0.0, 1.0), 0),
    REALXY_CTOR__(c, 0.0, -1.0, 1.0, -1.0));
  CHECK_REAL22(a, REALXY_CTOR__(c, 0.0, -1.0, 1.0, -1.0));
  CHECK_REAL22(REALXY_FUNC__(set_row)(a, REALX_CTOR__(c, 2.0, 3.0), 1),
    REALXY_CTOR__(c, 0.0, 2.0, 1.0, 3.0));
  CHECK_REAL22(a, REALXY_CTOR__(c, 0.0, 2.0, 1.0, 3.0));

  CHECK_REAL22(REALXY_FUNC__(transpose)(a, a),
    REALXY_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
  CHECK_REAL22(a, REALXY_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
  CHECK_REAL22(REALXY_FUNC__(transpose)(b, a),
    REALXY_CTOR__(c, 0.0, 2.0, 1.0, 3.0));
  CHECK_REAL22(b, REALXY_CTOR__(c, 0.0, 2.0, 1.0, 3.0));

  REALXY_FUNC__(splat)(a, -1.0);
  CHECK_REAL22(REALXY_FUNC__(set_col)(a, REALX_CTOR__(c, 0.0, 1.0), 0),
    REALXY_CTOR__(c, 0.0, 1.0, -1.0, -1.0));
  CHECK_REAL22(a, REALXY_CTOR__(c, 0.0, 1.0, -1.0, -1.0));
  CHECK_REAL22(REALXY_FUNC__(set_col)(a, REALX_CTOR__(c, 2.0, 3.0), 1),
    REALXY_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
  CHECK_REAL22(a, REALXY_CTOR__(c, 0.0, 1.0, 2.0, 3.0));

  CHECK_REAL2(REALXY_FUNC__(row)(b, a, 0), REALX_CTOR__(c, 0.0, 2.0));
  CHECK_REAL2(REALXY_FUNC__(row)(b, a, 1), REALX_CTOR__(c, 1.0, 3.0));

  CHECK_REAL2(REALXY_FUNC__(col)(b + 2, a, 0), REALX_CTOR__(c, 0.0, 1.0));
  CHECK_REAL2(REALXY_FUNC__(col)(b, a, 1), REALX_CTOR__(c, 2.0, 3.0));

  CHECK_REAL2(REALXY_FUNC__(col_ptr)(a, 0), REALX_CTOR__(c, 0.0, 1.0));
  CHECK_REAL2(REALXY_FUNC__(col_ptr)(a, 1), REALX_CTOR__(c, 2.0, 3.0));
  CHECK_REAL2(REALXY_FUNC__(col_cptr)(a, 0), REALX_CTOR__(c, 0.0, 1.0));
  CHECK_REAL2(REALXY_FUNC__(col_cptr)(a, 1), REALX_CTOR__(c, 2.0, 3.0));

  REALXY_CTOR__(a, 1.0, 2.0, 3.0, 4.0);
  CHECK_REAL2(REALXY_REALX_FUNC__(mul)(dst, a, REALX_CTOR__(c, 1.0, 2.0)),
    REALX_CTOR__(c, 7.0, 10.0));
  CHECK_REAL2(dst, REALX_CTOR__(c, 7.0, 10.0));
  CHECK_REAL2(REALX_REALXY_FUNC__(mul)(dst, REALX_CTOR__(c, 1.0, 2.0), a),
    REALX_CTOR__(c, 5.0, 11.0));
  CHECK_REAL2(dst, REALX_CTOR__(c, 5.0, 11.0));
  CHECK_REAL22(REALXY_FUNC__(mul)(dst, a, -1.0),
    REALXY_CTOR__(c, -1.0, -2.0, -3.0, -4.0));
  CHECK_REAL22(dst, REALXY_CTOR__(c, -1.0, -2.0, -3.0, -4.0));

  REALXY_CTOR__(b, 2.0, 9.0, 8.0, 1.0);
  CHECK_REAL22(REALXY_REALXY_FUNC__(mul)(dst, a, b),
    REALXY_CTOR__(c, 29.0, 40.0, 11.0, 20.0));
  CHECK_REAL22(dst, REALXY_CTOR__(c, 29.0, 40.0, 11.0, 20.0));
  CHECK_REAL22(REALXY_REALXY_FUNC__(mul)(b, a, b),
    REALXY_CTOR__(c, 29.0, 40.0, 11.0, 20.0));
  CHECK_REAL22(b, REALXY_CTOR__(c, 29.0, 40.0, 11.0, 20.0));

  REALXY_CTOR__(a, 0.0, 1.0, 2.0, 3.0);
  REALXY_CTOR__(b, 1.0, 2.0, 3.0, 4.0);
  CHECK_REAL22(REALXY_FUNC__(add)(dst, a, b),
    REALXY_CTOR__(c, 1.0, 3.0, 5.0, 7.0));
  CHECK_REAL22(dst, REALXY_CTOR__(c, 1.0, 3.0, 5.0, 7.0));
  CHECK_REAL22(REALXY_FUNC__(sub)(dst, dst, b),
    REALXY_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
	CHECK_REAL22(dst, REALXY_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
  CHECK_REAL22(REALXY_FUNC__(minus)(a, b),
    REALXY_CTOR__(c,  -1.0, -2.0, -3.0, -4.0));
  CHECK_REAL22(a, REALXY_CTOR__(c,  -1.0, -2.0, -3.0, -4.0));

  REALXY_FUNC__(set)(a, b);
  CHK(REALXY_FUNC__(eq)(a, b) ==  1);
  REALXY_FUNC__(add)(a, a, REALXY_FUNC__(splat)(c, REAL_EPSILON__));
  CHK(REALXY_FUNC__(eq)(a, b) ==  0);
  CHK(REALXY_FUNC__(eq_eps)(a, b, REAL_EPSILON__) ==  1);
  CHK(REALXY_FUNC__(eq_eps)(a, b, REAL_EPSILON__ * (REAL)0.9) ==  0);

  REALXY_FUNC__(set)(a, REALXY_CTOR__(c, 1, 3, 2, 4));
  CHK(REALXY_FUNC__(det)(a) ==  -2.0);
  CHK(REALXY_FUNC__(inverse)(b, a) ==  -2.0);
  CHECK_REAL22(b, REALXY_CTOR__(c, -2.0, 1.5, 1.0, -0.5));
  CHK(REALXY_FUNC__(invtrans)(a, a) ==  -2.0);
  CHECK_REAL22(a, REALXY_CTOR__(c, -2.0, 1.0, 1.5, -0.5));

  CHK(REALXY_FUNC__(rotation)(c, (REAL_TYPE__)PI/4) == c);


#if defined(OS_WINDOWS)
  #define ROTATION_EPS (REAL_TYPE__)1.e-6
#else
  #define ROTATION_EPS (REAL_TYPE__)1.e-6
#endif

  REALX_CTOR__(a, 1, 0);
  CHK(REALXY_REALX_FUNC__(mul)(a, c, a) == a);
  REALX_FUNC__(splat)(c, (REAL_TYPE__)(sqrt(2.0)/2.0));
  CHK(REALX_FUNC__(eq_eps)(a, c, ROTATION_EPS));

  REALX_CTOR__(b, 0, -1);
  CHK(REALXY_REALX_FUNC__(mul)(b, c, b) == b);
  REALX_CTOR__(c, (REAL_TYPE__)(sqrt(2.0)/2.0), -(REAL_TYPE__)(sqrt(2.0)/2.0));
  CHK(REALX_FUNC__(eq_eps)(b, c, ROTATION_EPS));

  CHK(REALXY_FUNC__(rotation)(c, -(REAL_TYPE__)PI/2) == c);

  REALX_CTOR__(a, 1, 0);
  CHK(REALXY_REALX_FUNC__(mul)(a, c, a) == a);
  REALX_CTOR__(c, 0, -1);
  CHK(REALX_FUNC__(eq_eps)(a, c, ROTATION_EPS));

  REALX_CTOR__(b, 0, -1);
  CHK(REALXY_REALX_FUNC__(mul)(b, c, b) == b);
  REALX_CTOR__(c, -1, 0);
  CHK(REALX_FUNC__(eq_eps)(b, c, ROTATION_EPS));

#undef ROTATION_EPS

  return 0;
}

#include "realXY_end.h"
