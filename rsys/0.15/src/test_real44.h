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
#define REALY_DIMENSION__ 4
#include "realXY_begin.h"

#define REAL REAL_TYPE__
#define REAL_COMPATIBLE REAL_TYPE_COMPATIBLE__

#define CHECK_REAL4(A, B)                                                      \
  {                                                                            \
    const REAL* a__ = (A);                                                     \
    const REAL* b__ = (B);                                                     \
    int i__;                                                                   \
    FOR_EACH(i__, 0, 4)                                                        \
      CHK(a__[i__] == b__[i__]);                                               \
  } (void)0
#define CHECK_REAL44(A, B)                                                     \
  {                                                                            \
    const REAL* a__ = (A);                                                     \
    const REAL* b__ = (B);                                                     \
    int i__;                                                                   \
    FOR_EACH(i__, 0, 16)                                                       \
      CHK(a__[i__] == b__[i__]);                                               \
  } (void)0

int
main(int argc, char** argv)
{
  REAL a[16], b[16], dst[16], c[16];
  REAL_COMPATIBLE d[16];
  int i;
  (void)argc, (void)argv;

  REALXY_FUNC__(set)(a, REALXY_FUNC__(splat)(c, -1.0));
  FOR_EACH(i, 0, 16) {
    CHK(a[i] == -1.0);
    CHK(c[i] == -1.0);
  }
  REALXY_CTOR__
    (a,
     0.0, 1.0, 2.0, 3.0,
     4.0, 5.0, 6.0, 7.0,
     8.0, 9.0, 10.0, 11.0,
     12.0, 13.0, 14.0, 15.0);
  FOR_EACH(i, 0, 16) {
    CHK(a[i] == (REAL)i);
  }
  CHECK_REAL44
    (REALXY_FUNC__(set_identity)(a),REALXY_CTOR__(c,
     1.0, 0.0, 0.0, 0.0,
     0.0, 1.0, 0.0, 0.0,
     0.0, 0.0, 1.0, 0.0,
     0.0, 0.0, 0.0, 1.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
     1.0, 0.0, 0.0, 0.0,
     0.0, 1.0, 0.0, 0.0,
     0.0, 0.0, 1.0, 0.0,
     0.0, 0.0, 0.0, 1.0));
  CHK(REALXY_FUNC__(is_identity)(a) == 1);
  REALXY_FUNC__(set)(c, a);
  FOR_EACH(i, 0, REALX_DIMENSION__*REALY_DIMENSION__) {
    REAL_TYPE__ r = c[i];
    c[i] = c[i] + REAL_EPSILON__;
    CHK(REALXY_FUNC__(is_identity)(c) == 0);
    c[i] = r;
  }

  d[0] = (REAL_COMPATIBLE)0.1;
  d[1] = (REAL_COMPATIBLE)(1.0/3.0);
  d[2] = (REAL_COMPATIBLE)0.3;
  d[3] = (REAL_COMPATIBLE)-0.7;
  d[4] = (REAL_COMPATIBLE)0.9;
  d[5] = (REAL_COMPATIBLE)-0.41;
  d[6] = (REAL_COMPATIBLE)0.22;
  d[7] = (REAL_COMPATIBLE)-0.01;
  d[8] = (REAL_COMPATIBLE)0.02;
  d[9] = (REAL_COMPATIBLE)1.1;
  d[10] = (REAL_COMPATIBLE)0.05;
  d[11] = (REAL_COMPATIBLE)-0.0125;
  d[12] = (REAL_COMPATIBLE)3.14;
  d[13] = (REAL_COMPATIBLE)1.23;
  d[14] = (REAL_COMPATIBLE)-4.56;
  d[15] = (REAL_COMPATIBLE)2.02;

  REALXY_CTOR__(c,
    (REAL)(REAL_COMPATIBLE)0.1,
    (REAL)(REAL_COMPATIBLE)(1.0/3.0),
    (REAL)(REAL_COMPATIBLE)0.3,
    (REAL)(REAL_COMPATIBLE)-0.7,
    (REAL)(REAL_COMPATIBLE)0.9,
    (REAL)(REAL_COMPATIBLE)-0.41,
    (REAL)(REAL_COMPATIBLE)0.22,
    (REAL)(REAL_COMPATIBLE)-0.01,
    (REAL)(REAL_COMPATIBLE)0.02,
    (REAL)(REAL_COMPATIBLE)1.1,
    (REAL)(REAL_COMPATIBLE)0.05,
    (REAL)(REAL_COMPATIBLE)-0.0125,
    (REAL)(REAL_COMPATIBLE)3.14,
    (REAL)(REAL_COMPATIBLE)1.23,
    (REAL)(REAL_COMPATIBLE)-4.56,
    (REAL)(REAL_COMPATIBLE)2.02);
  CHECK_REAL44(REALXY_CAST__(dst, d), c);

  REALXY_FUNC__(splat)(a, -1.0);
  CHECK_REAL44
    (REALXY_FUNC__(set_row)(a, REALX_CTOR__(c, 0.0, 1.0, 2.0, 3.0), 0),
     REALXY_CTOR__(c,
       0.0, -1.0, -1.0, -1.0,
       1.0, -1.0, -1.0, -1.0,
       2.0, -1.0, -1.0, -1.0,
       3.0, -1.0, -1.0, -1.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
       0.0, -1.0, -1.0, -1.0,
       1.0, -1.0, -1.0, -1.0,
       2.0, -1.0, -1.0, -1.0,
       3.0, -1.0, -1.0, -1.0));
  CHECK_REAL44
    (REALXY_FUNC__(set_row)(a, REALX_CTOR__(c, 4.0, 5.0, 6.0, 7.0), 1),
     REALXY_CTOR__(c,
       0.0, 4.0, -1.0, -1.0,
       1.0, 5.0, -1.0, -1.0,
       2.0, 6.0, -1.0, -1.0,
       3.0, 7.0, -1.0, -1.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
       0.0, 4.0, -1.0, -1.0,
       1.0, 5.0, -1.0, -1.0,
       2.0, 6.0, -1.0, -1.0,
       3.0, 7.0, -1.0, -1.0));
  CHECK_REAL44
    (REALXY_FUNC__(set_row)(a, REALX_CTOR__(c, 8.0, 9.0, 10.0, 11.0), 2),
     REALXY_CTOR__(c,
       0.0, 4.0, 8.0, -1.0,
       1.0, 5.0, 9.0, -1.0,
       2.0, 6.0, 10.0, -1.0,
       3.0, 7.0, 11.0, -1.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
       0.0, 4.0, 8.0, -1.0,
       1.0, 5.0, 9.0, -1.0,
       2.0, 6.0, 10.0, -1.0,
       3.0, 7.0, 11.0, -1.0));
  CHECK_REAL44
    (REALXY_FUNC__(set_row)(a, REALX_CTOR__(c, 12.0, 13.0, 14.0, 15.0), 3),
     REALXY_CTOR__(c,
       0.0, 4.0, 8.0, 12.0,
       1.0, 5.0, 9.0, 13.0,
       2.0, 6.0, 10.0, 14.0,
       3.0, 7.0, 11.0, 15.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
       0.0, 4.0, 8.0, 12.0,
       1.0, 5.0, 9.0, 13.0,
       2.0, 6.0, 10.0, 14.0,
       3.0, 7.0, 11.0, 15.0));

  CHECK_REAL44
    (REALXY_FUNC__(transpose)(a, a), REALXY_CTOR__(c,
       0.0, 1.0, 2.0, 3.0,
       4.0, 5.0, 6.0, 7.0,
       8.0, 9.0, 10.0, 11.0,
       12.0, 13.0, 14.0, 15.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
       0.0, 1.0, 2.0, 3.0,
       4.0, 5.0, 6.0, 7.0,
       8.0, 9.0, 10.0, 11.0,
       12.0, 13.0, 14.0, 15.0));
  CHECK_REAL44
    (REALXY_FUNC__(transpose)(b, a),REALXY_CTOR__(c,
       0.0, 4.0, 8.0, 12.0,
       1.0, 5.0, 9.0, 13.0,
       2.0, 6.0, 10.0, 14.0,
       3.0, 7.0, 11.0, 15.0));
  CHECK_REAL44
    (b, REALXY_CTOR__(c,
       0.0, 4.0, 8.0, 12.0,
       1.0, 5.0, 9.0, 13.0,
       2.0, 6.0, 10.0, 14.0,
       3.0, 7.0, 11.0, 15.0));

  REALXY_FUNC__(splat)(a, -1.0);
  CHECK_REAL44
    (REALXY_FUNC__(set_col)(a, REALX_CTOR__(c, 0.0, 1.0, 2.0, 3.0), 0),
     REALXY_CTOR__(c,
       0.0, 1.0, 2.0, 3.0,
       -1.0, -1.0, -1.0, -1.0,
       -1.0, -1.0, -1.0, -1.0,
       -1.0, -1.0, -1.0, -1.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
       0.0, 1.0, 2.0, 3.0,
       -1.0, -1.0, -1.0, -1.0,
       -1.0, -1.0, -1.0, -1.0,
       -1.0, -1.0, -1.0, -1.0));
  CHECK_REAL44
    (REALXY_FUNC__(set_col)(a, REALX_CTOR__(c, 4.0, 5.0, 6.0, 7.0), 1),
     REALXY_CTOR__(c,
       0.0, 1.0, 2.0, 3.0,
       4.0, 5.0, 6.0, 7.0,
       -1.0, -1.0, -1.0, -1.0,
       -1.0, -1.0, -1.0, -1.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
       0.0, 1.0, 2.0, 3.0,
       4.0, 5.0, 6.0, 7.0,
       -1.0, -1.0, -1.0, -1.0,
       -1.0, -1.0, -1.0, -1.0));
  CHECK_REAL44
    (REALXY_FUNC__(set_col)(a, REALX_CTOR__(c, 8.0, 9.0, 10.0, 11.0), 2),
     REALXY_CTOR__(c,
       0.0, 1.0, 2.0, 3.0,
       4.0, 5.0, 6.0, 7.0,
       8.0, 9.0, 10.0, 11.0,
       -1.0, -1.0, -1.0, -1.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
       0.0, 1.0, 2.0, 3.0,
       4.0, 5.0, 6.0, 7.0,
       8.0, 9.0, 10.0, 11.0,
       -1.0, -1.0, -1.0, -1.0));
  CHECK_REAL44
    (REALXY_FUNC__(set_col)(a, REALX_CTOR__(c, 12.0, 13.0, 14.0, 15.0), 3),
     REALXY_CTOR__(c,
       0.0, 1.0, 2.0, 3.0,
       4.0, 5.0, 6.0, 7.0,
       8.0, 9.0, 10.0, 11.0,
       12.0, 13.0, 14.0, 15.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
       0.0, 1.0, 2.0, 3.0,
       4.0, 5.0, 6.0, 7.0,
       8.0, 9.0, 10.0, 11.0,
       12.0, 13.0, 14.0, 15.0));

  CHECK_REAL4(REALXY_FUNC__(row)(b + 1, a, 0), REALX_CTOR__(c, 0.0, 4.0, 8.0, 12.0));
  CHECK_REAL4(b + 1, REALX_CTOR__(c, 0.0, 4.0, 8.0, 12.0));
  CHECK_REAL4(REALXY_FUNC__(row)(b + 2, a, 1), REALX_CTOR__(c, 1.0, 5.0, 9.0, 13.0));
  CHECK_REAL4(b + 2, REALX_CTOR__(c, 1.0, 5.0, 9.0, 13.0));
  CHECK_REAL4(REALXY_FUNC__(row)(b + 8, a, 2), REALX_CTOR__(c, 2.0, 6.0, 10.0, 14.0));
  CHECK_REAL4(b + 8, REALX_CTOR__(c, 2.0, 6.0, 10.0, 14.0));
  CHECK_REAL4(REALXY_FUNC__(row)(b + 5, a, 2), REALX_CTOR__(c, 2.0, 6.0, 10.0, 14.0));
  CHECK_REAL4(b + 5, REALX_CTOR__(c, 2.0, 6.0, 10.0, 14.0));

  CHECK_REAL4(REALXY_FUNC__(col)(b + 2, a, 0), REALX_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
  CHECK_REAL4(b + 2, REALX_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
  CHECK_REAL4(REALXY_FUNC__(col)(b + 1, a, 1), REALX_CTOR__(c, 4.0, 5.0, 6.0, 7.0));
  CHECK_REAL4(b + 1, REALX_CTOR__(c, 4.0, 5.0, 6.0, 7.0));
  CHECK_REAL4(REALXY_FUNC__(col)(b + 5, a, 2), REALX_CTOR__(c, 8.0, 9.0, 10.0, 11.0));
  CHECK_REAL4(b + 5, REALX_CTOR__(c, 8.0, 9.0, 10.0, 11.0));
  CHECK_REAL4(REALXY_FUNC__(col)(b + 5, a, 3), REALX_CTOR__(c, 12.0, 13.0, 14.0, 15.0));
  CHECK_REAL4(b + 5, REALX_CTOR__(c, 12.0, 13.0, 14.0, 15.0));
  CHECK_REAL4(REALXY_FUNC__(col_ptr)(a, 0), REALX_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
  CHECK_REAL4(REALXY_FUNC__(col_ptr)(a, 1), REALX_CTOR__(c, 4.0, 5.0, 6.0, 7.0));
  CHECK_REAL4(REALXY_FUNC__(col_ptr)(a, 2), REALX_CTOR__(c, 8.0, 9.0, 10.0, 11.0));
  CHECK_REAL4(REALXY_FUNC__(col_ptr)(a, 3), REALX_CTOR__(c, 12.0, 13.0, 14.0, 15.0));
  CHECK_REAL4(REALXY_FUNC__(col_cptr)(a, 0), REALX_CTOR__(c, 0.0, 1.0, 2.0, 3.0));
  CHECK_REAL4(REALXY_FUNC__(col_cptr)(a, 1), REALX_CTOR__(c, 4.0, 5.0, 6.0, 7.0));
  CHECK_REAL4(REALXY_FUNC__(col_cptr)(a, 2), REALX_CTOR__(c, 8.0, 9.0, 10.0, 11.0));
  CHECK_REAL4(REALXY_FUNC__(col_cptr)(a, 3), REALX_CTOR__(c, 12.0, 13.0, 14.0, 15.0));

  CHECK_REAL4
    (REALXY_REALX_FUNC__(mul)(dst, a, REALX_CTOR__(c, 1.0, 2.0, 3.0, 1.0)),
     REALX_CTOR__(c, 44.0, 51.0, 58.0, 65.0));
  CHECK_REAL4(dst, REALX_CTOR__(c, 44.0, 51.0, 58.0, 65.0));
  CHECK_REAL4
    (REALX_REALXY_FUNC__(mul)(dst, REALX_CTOR__(c, 1.0, 2.0, 3.0, 1.0), a),
     REALX_CTOR__(c, 11.0, 39.0, 67.0, 95.0));
  CHECK_REAL4(dst, REALX_CTOR__(c, 11.0, 39.0, 67.0, 95.0));
  CHECK_REAL44
    (REALXY_FUNC__(mul)(dst, a, -1.0), REALXY_CTOR__(c,
     0.0, -1.0, -2.0, -3.0,
     -4.0, -5.0, -6.0, -7.0,
     -8.0, -9.0, -10.0, -11.0,
     -12.0, -13.0, -14.0, -15.0));
  CHECK_REAL44
    (dst, REALXY_CTOR__(c,
     0.0, -1.0, -2.0, -3.0,
     -4.0, -5.0, -6.0, -7.0,
     -8.0, -9.0, -10.0, -11.0,
     -12.0, -13.0, -14.0, -15.0));

  REALXY_CTOR__
    (a,
     1.0, 2.0, 3.0, 4.0,
     4.0, 5.0, 6.0, 7.0,
     7.0, 8.0, 9.0, 10.0,
     10.0, 11.0, 12.0, 13.0);
  REALXY_CTOR__
    (b,
     2.0, 9.0, 8.0, 1.0,
     1.0, -2.0, 2.0, 1.0,
     1.0, -8.0, -4.0, 2.0,
     1.0, 3.0, 4.0, 2.0);
  CHECK_REAL44
    (REALXY_REALXY_FUNC__(mul)(dst, a, b), REALXY_CTOR__(c,
     104.0, 124.0, 144.0, 164.0,
     17.0, 19.0, 21.0, 23.0,
     -39.0, -48.0, -57.0, -66.0,
     61.0, 71.0, 81.0, 91.0));
  CHECK_REAL44
    (dst, REALXY_CTOR__(c,
     104.0, 124.0, 144.0, 164.0,
     17.0, 19.0, 21.0, 23.0,
     -39.0, -48.0, -57.0, -66.0,
     61.0, 71.0, 81.0, 91.0));
  CHECK_REAL44
    (REALXY_REALXY_FUNC__(mul)(a, a, b), REALXY_CTOR__(c,
     104.0, 124.0, 144.0, 164.0,
     17.0, 19.0, 21.0, 23.0,
     -39.0, -48.0, -57.0, -66.0,
     61.0, 71.0, 81.0, 91.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
     104.0, 124.0, 144.0, 164.0,
     17.0, 19.0, 21.0, 23.0,
     -39.0, -48.0, -57.0, -66.0,
     61.0, 71.0, 81.0, 91.0));
  REALXY_CTOR__
    (a,
     1.0, 2.0, 3.0, 4.0,
     4.0, 5.0, 6.0, 7.0,
     7.0, 8.0, 9.0, 10.0,
     10.0, 11.0, 12.0, 13.0);
  CHECK_REAL44
    (REALXY_REALXY_FUNC__(mul)(b, a, b), REALXY_CTOR__(c,
     104.0, 124.0, 144.0, 164.0,
     17.0, 19.0, 21.0, 23.0,
     -39.0, -48.0, -57.0, -66.0,
     61.0, 71.0, 81.0, 91.0));
  CHECK_REAL44
    (b, REALXY_CTOR__(c,
     104.0, 124.0, 144.0, 164.0,
     17.0, 19.0, 21.0, 23.0,
     -39.0, -48.0, -57.0, -66.0,
     61.0, 71.0, 81.0, 91.0));

  REALXY_CTOR__
    (a,
     0.0, 1.0, 2.0, 3.0,
     4.0, 5.0, 6.0, 7.0,
     8.0, 9.0, 10.0, 11.0,
     12.0, 13.0, 14.0, 15.0);
  REALXY_CTOR__
    (b,
     0.0, 2.0, 1.0, 3.0,
     1.0, -2.0, -1.0, -3.0,
     1.0, 0.0, 0.0, 2.0,
     3.0, 2.0, 1.0, 0.0);
  CHECK_REAL44
    (REALXY_FUNC__(add)(dst, a, b), REALXY_CTOR__(c,
     0.0, 3.0, 3.0, 6.0,
     5.0, 3.0, 5.0, 4.0,
     9.0, 9.0, 10.0, 13.0,
     15.0, 15.0, 15.0, 15.0));
  CHECK_REAL44
    (dst, REALXY_CTOR__(c,
     0.0, 3.0, 3.0, 6.0,
     5.0, 3.0, 5.0, 4.0,
     9.0, 9.0, 10.0, 13.0,
     15.0, 15.0, 15.0, 15.0));
  CHECK_REAL44
    (REALXY_FUNC__(sub)(dst, a, b), REALXY_CTOR__(c,
     0.0, -1.0, 1.0, 0.0,
     3.0, 7.0, 7.0, 10.0,
     7.0, 9.0, 10.0, 9.0,
     9.0, 11.0, 13.0, 15.0));
  CHECK_REAL44
    (dst, REALXY_CTOR__(c,
     0.0, -1.0, 1.0, 0.0,
     3.0, 7.0, 7.0, 10.0,
     7.0, 9.0, 10.0, 9.0,
     9.0, 11.0, 13.0, 15.0));
  CHECK_REAL44
    (REALXY_FUNC__(minus)(a, b), REALXY_CTOR__(c,
     0.0, -2.0, -1.0, -3.0,
     -1.0, 2.0, 1.0, 3.0,
     -1.0, 0.0, 0.0, -2.0,
     -3.0, -2.0, -1.0, 0.0));
  CHECK_REAL44
    (a, REALXY_CTOR__(c,
     0.0, -2.0, -1.0, -3.0,
     -1.0, 2.0, 1.0, 3.0,
     -1.0, 0.0, 0.0, -2.0,
     -3.0, -2.0, -1.0, 0.0));

  REALXY_FUNC__(set)(a, b);
  CHK(REALXY_FUNC__(eq)(a, b) == 1);
  REALXY_FUNC__(add)(a, a, REALXY_FUNC__(splat)(c, FLT_EPSILON));
  CHK(REALXY_FUNC__(eq_eps)(a, b, FLT_EPSILON) == 1);
  CHK(REALXY_FUNC__(eq_eps)(a, b, FLT_EPSILON * (REAL)0.9) == 0);

  REALXY_FUNC__(set)
    (a, REALXY_CTOR__(c,
     2.0, 9.0, 8.0, 1.0,
     1.0, -2.0, 2.0, 1.0,
     1.0, -8.0, -4.0, 2.0,
     1.0, 3.0, 4.0, 2.0));
  CHK(REALXY_FUNC__(det)(a) == 78.0);
  CHK(REALXY_FUNC__(inverse)(b, a) == 78.0);
  REALXY_REALXY_FUNC__(mul)(dst, a, b);
  CHK(REALXY_FUNC__(eq_eps)
    (dst, REALXY_CTOR__(c,
     1.0, 0.0, 0.0, 0.0,
     0.0, 1.0, 0.0, 0.0,
     0.0, 0.0, 1.0, 0.0,
     0.0, 0.0, 0.0, 1.0),
     1.e-6f));
  CHK(REALXY_FUNC__(invtrans)(a, a) == 78.0);
  REALXY_FUNC__(transpose)(a, a);
  CHECK_REAL44(a, b);

  return 0;
}
