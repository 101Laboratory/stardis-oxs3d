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

#ifndef REAL_TYPE__
  #error Missing arguments
#endif

#ifdef REAL33_FUNC__
  #error Unexpected macro definition
#endif

#define REAL33_FUNC__(Func) CONCAT(CONCAT(CONCAT(REAL_LETTER__, 33), _), Func)

/* Generate common realXY funcs */
#define REALX_DIMENSION__ 4
#define REALY_DIMENSION__ 4
#include "realXY_begin.h"
#include "realXY.h"

/* Specific REAL_TYPE__44 funcs*/
static FINLINE REAL_TYPE__*
REALXY_CTOR__
  (REAL_TYPE__* dst,
   const REAL_TYPE__ a, const REAL_TYPE__ b, const REAL_TYPE__ c, const REAL_TYPE__ d,
   const REAL_TYPE__ e, const REAL_TYPE__ f, const REAL_TYPE__ g, const REAL_TYPE__ h,
   const REAL_TYPE__ i, const REAL_TYPE__ j, const REAL_TYPE__ k, const REAL_TYPE__ l,
   const REAL_TYPE__ m, const REAL_TYPE__ n, const REAL_TYPE__ o, const REAL_TYPE__ p)
{
  ASSERT(dst);
  dst[0] = a;  dst[1] = b;  dst[2] = c;  dst[3] = d;
  dst[4] = e;  dst[5] = f;  dst[6] = g;  dst[7] = h;
  dst[8] = i;  dst[9] = j;  dst[10] = k; dst[11] = l;
  dst[12] = m; dst[13] = n; dst[14] = o; dst[15] = p;
  return dst;
}

static FINLINE REAL_TYPE__
REALXY_FUNC__(det)(const REAL_TYPE__* mat)
{
  REAL_TYPE__ m33[9];
  REAL_TYPE__ row3[4], tmp[4];
  ASSERT(mat);

  #define C3( Dst, Mat, ICol ) \
    (Dst)[0] = Mat[ICol*4], (Dst)[1] = Mat[ICol*4+1], (Dst)[2] = Mat[ICol*4+2]
  C3(m33 + 0, mat, 1); C3(m33 + 3, mat, 2);  C3(m33 + 6, mat, 3);
  tmp[0] = -REAL33_FUNC__(det)(m33);
  C3(m33 + 0, mat, 0); C3(m33 + 3, mat, 2);  C3(m33 + 6, mat, 3);
  tmp[1] =  REAL33_FUNC__(det)(m33);
  C3(m33 + 0, mat, 0); C3(m33 + 3, mat, 1);  C3(m33 + 6, mat, 3);
  tmp[2] = -REAL33_FUNC__(det)(m33);
  C3(m33 + 0, mat, 0); C3(m33 + 3, mat, 1);  C3(m33 + 6, mat, 2);
  #undef C3
  tmp[3] =  REAL33_FUNC__(det)(m33);
  REALXY_FUNC__(row)(row3, mat, 3);
  return REALX_FUNC__(dot)(tmp,  row3);
}

static INLINE REAL_TYPE__
REALXY_FUNC__(inverse)(REAL_TYPE__* dst, const REAL_TYPE__* m)
{
  REAL_TYPE__ tmp[9];
  REAL_TYPE__ det_012[4], det_023[4], det_123[4], det_013[4];
  REAL_TYPE__ cofacts[4];
  REAL_TYPE__ det, rcp_det, mpmp_rcp_det[4], pmpm_rcp_det[4];
  ASSERT( dst && m );

  /* Define the 3x3 sub matrices and compute their determinants */
  #define C3( Dst, Mat, ICol, X, Y, Z ) \
    (Dst)[0] = Mat[ICol*4+X], (Dst)[1] = Mat[ICol*4+Y], (Dst)[2] = Mat[ICol*4+Z]
  C3(tmp+0 ,m, 1, 0, 1, 2); C3(tmp+3, m, 2, 0, 1, 2); C3(tmp+6, m, 3, 0, 1, 2);
  det_012[0] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 0, 1, 2); C3(tmp+3, m, 2, 0, 1, 2); C3(tmp+6, m, 3, 0, 1, 2);
  det_012[1] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 0, 1, 2); C3(tmp+3, m, 1, 0, 1, 2); C3(tmp+6, m, 3, 0, 1, 2);
  det_012[2] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 0, 1, 2); C3(tmp+3, m, 1, 0, 1, 2); C3(tmp+6, m, 2, 0, 1, 2);
  det_012[3] = REAL33_FUNC__(det)(tmp);

  C3(tmp+0 ,m, 1, 0, 2, 3); C3(tmp+3, m, 2, 0, 2, 3); C3(tmp+6, m, 3, 0, 2, 3);
  det_023[0] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 0, 2, 3); C3(tmp+3, m, 2, 0, 2, 3); C3(tmp+6, m, 3, 0, 2, 3);
  det_023[1] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 0, 2, 3); C3(tmp+3, m, 1, 0, 2, 3); C3(tmp+6, m, 3, 0, 2, 3);
  det_023[2] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 0, 2, 3); C3(tmp+3, m, 1, 0, 2, 3); C3(tmp+6, m, 2, 0, 2, 3);
  det_023[3] = REAL33_FUNC__(det)(tmp);

  C3(tmp+0 ,m, 1, 1, 2, 3); C3(tmp+3, m, 2, 1, 2, 3); C3(tmp+6, m, 3, 1, 2, 3);
  det_123[0] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 1, 2, 3); C3(tmp+3, m, 2, 1, 2, 3); C3(tmp+6, m, 3, 1, 2, 3);
  det_123[1] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 1, 2, 3); C3(tmp+3, m, 1, 1, 2, 3); C3(tmp+6, m, 3, 1, 2, 3);
  det_123[2] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 1, 2, 3); C3(tmp+3, m, 1, 1, 2, 3); C3(tmp+6, m, 2, 1, 2, 3);
  det_123[3] = REAL33_FUNC__(det)(tmp);

  C3(tmp+0 ,m, 1, 0, 1, 3); C3(tmp+3, m, 2, 0, 1, 3); C3(tmp+6, m, 3, 0, 1, 3);
  det_013[0] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 0, 1, 3); C3(tmp+3, m, 2, 0, 1, 3); C3(tmp+6, m, 3, 0, 1, 3);
  det_013[1] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 0, 1, 3); C3(tmp+3, m, 1, 0, 1, 3); C3(tmp+6, m, 3, 0, 1, 3);
  det_013[2] = REAL33_FUNC__(det)(tmp);
  C3(tmp+0 ,m, 0, 0, 1, 3); C3(tmp+3, m, 1, 0, 1, 3); C3(tmp+6, m, 2, 0, 1, 3);
  det_013[3] = REAL33_FUNC__(det)(tmp);
  #undef C3

  cofacts[0] = -det_012[0];
  cofacts[1] =  det_012[1];
  cofacts[2] = -det_012[2];
  cofacts[3] =  det_012[3];

  /* Compute the determinant of src */
  tmp[0] = m[3], tmp[1] = m[7], tmp[2] = m[11], tmp[3] = m[15];
  det = REALX_FUNC__(dot)(cofacts, tmp);

  /* Invert the matrix */
  if(det == 0.f)
    return det;

  rcp_det = 1.f / det;

  mpmp_rcp_det[0] = mpmp_rcp_det[2] = -rcp_det;
  mpmp_rcp_det[1] = mpmp_rcp_det[3] =  rcp_det;
  pmpm_rcp_det[0] = pmpm_rcp_det[2] =  rcp_det;
  pmpm_rcp_det[1] = pmpm_rcp_det[3] = -rcp_det;
  REALX_FUNC__(mul)(REALXY_FUNC__(col_ptr)(dst, 0), det_123, pmpm_rcp_det);
  REALX_FUNC__(mul)(REALXY_FUNC__(col_ptr)(dst, 1), det_023, mpmp_rcp_det);
  REALX_FUNC__(mul)(REALXY_FUNC__(col_ptr)(dst, 2), det_013, pmpm_rcp_det);
  REALX_FUNC__(mul)(REALXY_FUNC__(col_ptr)(dst, 3), det_012, mpmp_rcp_det);
  return det;
}
static FINLINE REAL_TYPE__
REALXY_FUNC__(invtrans)(REAL_TYPE__* dst, const REAL_TYPE__* mat)
{
  const REAL_TYPE__ det = REALXY_FUNC__(inverse)(dst, mat);
  REALXY_FUNC__(transpose)(dst, dst);
  return det;
}

#undef REAL33_FUNC__

#include "realXY_end.h"
