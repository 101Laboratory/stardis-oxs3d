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

/*
 * Internal header used to generate funcs on column major REAL_TYPE__ matrix
 * of X x Y dimensions
 */

#include "rsys_math.h"
#include "rsys.h"

#ifdef COMPILER_GCC
  #pragma GCC push_options
  #pragma GCC optimize("unroll-loops")
#endif

static FINLINE REAL_TYPE__*
REALXY_CAST__(REAL_TYPE__* dst, const REAL_TYPE_COMPATIBLE__* src)
{
  int x, y, i;
  ASSERT(dst && src);
  i = 0;
  FOR_EACH(x, 0, REALX_DIMENSION__) {
  FOR_EACH(y, 0, REALY_DIMENSION__) {
    dst[i] = (REAL_TYPE__)src[i];
    ++i;
  }}
  return dst;
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(splat)(REAL_TYPE__* dst, const REAL_TYPE__ val)
{
  int i = 0;
  ASSERT(dst);
  FOR_EACH(i, 0, REALX_DIMENSION__*REALY_DIMENSION__) {
    dst[i] = val;
  }
  return dst;
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(set__)(REAL_TYPE__* dst, const REAL_TYPE__* src)
{
  int x, y, i;
  ASSERT(dst && src);
  ASSERT(!MEM_AREA_OVERLAP(dst, SIZEOF_REALXY__, src, SIZEOF_REALXY__));
  i = 0;
  FOR_EACH(x, 0, REALX_DIMENSION__) {
  FOR_EACH(y, 0, REALY_DIMENSION__) {
    dst[i] = src[i];
    ++i;
  }}
  return dst;
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(set)(REAL_TYPE__* dst, const REAL_TYPE__* src)
{
  ASSERT(dst && src);
  if(!MEM_AREA_OVERLAP(dst, SIZEOF_REALXY__, src, SIZEOF_REALXY__)) {
    return REALXY_FUNC__(set__)(dst, src);
  } else {
    REAL_TYPE__ tmp[REALX_DIMENSION__*REALY_DIMENSION__];
    return REALXY_FUNC__(set__)(dst, REALXY_FUNC__(set__)(tmp, src));
  }
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(set_row)(REAL_TYPE__* mat, const REAL_TYPE__* row, const int irow)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int x;
  ASSERT(mat && row && irow < REALY_DIMENSION__ && irow >= 0);

  FOR_EACH(x, 0, REALX_DIMENSION__)
    tmp[x] = row[x];
  FOR_EACH(x, 0, REALX_DIMENSION__)
    mat[x * REALY_DIMENSION__ + irow] = tmp[x];
  return mat;
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(set_col)(REAL_TYPE__* mat, const REAL_TYPE__* col, const int icol)
{
  REAL_TYPE__ tmp[REALY_DIMENSION__];
  int y;
  ASSERT(mat && col && icol < REALX_DIMENSION__ && icol >= 0);
  FOR_EACH(y, 0, REALY_DIMENSION__)
    tmp[y] = col[y];
  REALX_FUNC__(set)(mat + (icol * REALY_DIMENSION__), tmp);
  return mat;
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(row)(REAL_TYPE__* row, const REAL_TYPE__* mat, const int irow)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int x;
  ASSERT(row && mat && irow < REALY_DIMENSION__);

  FOR_EACH(x, 0, REALX_DIMENSION__)
    tmp[x] = mat[x * REALY_DIMENSION__ + irow];
  FOR_EACH(x, 0, REALX_DIMENSION__)
    row[x] = tmp[x];
  return row;
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(col_ptr)(REAL_TYPE__* mat, const int icol)
{
  ASSERT(mat && icol < REALX_DIMENSION__);
  return mat + (icol * REALY_DIMENSION__);
}

static FINLINE const REAL_TYPE__*
REALXY_FUNC__(col_cptr)(const REAL_TYPE__* mat, const int icol)
{
  ASSERT(mat && icol < REALX_DIMENSION__);
  return mat + (icol * REALY_DIMENSION__);
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(col)(REAL_TYPE__* col, const REAL_TYPE__* mat, const int icol)
{
  REAL_TYPE__ tmp[REALY_DIMENSION__];
  const REAL_TYPE__* col_ptr = REALXY_FUNC__(col_cptr)(mat, icol);
  int x, y;
  ASSERT(mat && icol < REALY_DIMENSION__);

  FOR_EACH(y, 0, REALY_DIMENSION__)
    tmp[y] = col_ptr[y];
  FOR_EACH(x, 0, REALX_DIMENSION__)
    col[x] = tmp[x];
  return col;
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(add)(REAL_TYPE__* dst, const REAL_TYPE__* a, const REAL_TYPE__* b)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__*REALY_DIMENSION__];
  int i;
  FOR_EACH(i, 0, REALX_DIMENSION__ * REALY_DIMENSION__)
    tmp[i] =  a[i] + b[i];
  return REALXY_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(sub)(REAL_TYPE__* dst, const REAL_TYPE__* a, const REAL_TYPE__* b)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__*REALY_DIMENSION__];
  int i;
  FOR_EACH(i, 0, REALX_DIMENSION__ * REALY_DIMENSION__)
    tmp[i] =  a[i] - b[i];
  return REALXY_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(minus)(REAL_TYPE__* dst, const REAL_TYPE__* a)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__*REALY_DIMENSION__];
  int i;
  FOR_EACH(i, 0, REALX_DIMENSION__ * REALY_DIMENSION__)
    tmp[i] = -a[i];
  return REALXY_FUNC__(set__)(dst, tmp);
}

static FINLINE int
REALXY_FUNC__(eq)(const REAL_TYPE__* a, const REAL_TYPE__* b)
{
  int is_eq = 1;
  int x = 0;
  ASSERT(a && b);

  do {
    const int i = x * REALY_DIMENSION__;
    is_eq = REALX_FUNC__(eq)(a + i, b + i);
    ++x;
  } while(x < REALX_DIMENSION__ && is_eq);
  return is_eq;
}

static FINLINE int
REALXY_FUNC__(eq_eps)
  (const REAL_TYPE__* a,
   const REAL_TYPE__* b,
   const REAL_TYPE__ eps)
{
  int is_eq = 1;
  int x = 0;
  ASSERT(a && b);

  do {
    const int i = x * REALY_DIMENSION__;
    is_eq = REALX_FUNC__(eq_eps)(a + i, b + i, eps);
    ++x;
  } while(x < REALX_DIMENSION__ && is_eq);
  return is_eq;
}

static FINLINE REAL_TYPE__*
REALXY_REALX_FUNC__(mul)
  (REAL_TYPE__* dst, const REAL_TYPE__* mat, const REAL_TYPE__* vec)
{
  REAL_TYPE__ row[REALX_DIMENSION__];
  REAL_TYPE__ tmp[REALY_DIMENSION__];
  int y;
  ASSERT(dst && mat && vec);

  FOR_EACH(y, 0, REALY_DIMENSION__) {
    REALXY_FUNC__(row)(row, mat, y);
    tmp[y] = REALX_FUNC__(dot)(row, vec);
  }
  return REALY_FUNC__(set)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(mul)(REAL_TYPE__* dst, const REAL_TYPE__* mat, const REAL_TYPE__ b)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__*REALY_DIMENSION__];
  int i;
  ASSERT(dst && mat);
  FOR_EACH(i, 0, REALX_DIMENSION__ * REALY_DIMENSION__)
    tmp[i] = mat[i] * b;
  return REALXY_FUNC__(set__)(dst, tmp);;
}

static FINLINE REAL_TYPE__*
REALX_REALXY_FUNC__(mul)
  (REAL_TYPE__* dst, const REAL_TYPE__* vec, const REAL_TYPE__* mat)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__];
  int x;
  ASSERT(dst && vec && mat);
  FOR_EACH(x, 0, REALX_DIMENSION__) {
    const REAL_TYPE__* col = mat + (x * REALY_DIMENSION__);
    tmp[x] = REALX_FUNC__(dot)(vec, col);
  }
  return REALX_FUNC__(set)(dst, tmp);
}

#if REALX_DIMENSION__ == REALY_DIMENSION__
static FINLINE REAL_TYPE__*
REALXY_FUNC__(set_identity)(REAL_TYPE__* mat)
{
  int x, y, i;
  ASSERT(mat);
  i = 0;
  FOR_EACH(x, 0, REALX_DIMENSION__) {
  FOR_EACH(y, 0, REALY_DIMENSION__) {
    mat[i] = x == y ? 1.f : 0.f;
    ++i;
  }}
  return mat;
}

static FINLINE int
REALXY_FUNC__(is_identity)(const REAL_TYPE__* mat)
{
  int is_identity;
  int x = 0;
  do {
    int y = 0;
    do  {
      is_identity = mat[x*REALY_DIMENSION__ + y] == (REAL_TYPE__)(x==y);
      ++y;
    } while(y < REALY_DIMENSION__ && is_identity);
    ++x;
  } while(x < REALX_DIMENSION__ && is_identity);
  return is_identity;
}

static FINLINE REAL_TYPE__*
REALXY_FUNC__(transpose)(REAL_TYPE__* dst, const REAL_TYPE__* src)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__ * REALY_DIMENSION__];
  int x, y, i;
  ASSERT(dst && src);

  i = 0;
  FOR_EACH(x, 0, REALX_DIMENSION__) {
  FOR_EACH(y, 0, REALY_DIMENSION__) {
    tmp[y * REALY_DIMENSION__ + x] = src[i];
    ++i;
  }}
  return REALXY_FUNC__(set__)(dst, tmp);
}

static FINLINE REAL_TYPE__*
REALXY_REALXY_FUNC__(mul)
  (REAL_TYPE__* dst, const REAL_TYPE__* a, const REAL_TYPE__* b)
{
  REAL_TYPE__ tmp[REALX_DIMENSION__ * REALY_DIMENSION__];
  REAL_TYPE__ a_trans[REALX_DIMENSION__ * REALY_DIMENSION__];
  int x, y, i;

  /* Transpose the a matrix */
  i = 0;
  FOR_EACH(x, 0, REALX_DIMENSION__) {
  FOR_EACH(y, 0, REALY_DIMENSION__) {
    a_trans[y * REALY_DIMENSION__ + x] = a[i];
    ++i;
  }}
  /* Compute the a x b and store the result into tmp */
  i = 0;
  FOR_EACH(x, 0, REALX_DIMENSION__) {
  FOR_EACH(y, 0, REALY_DIMENSION__) {
    tmp[i] = REALX_FUNC__(dot)
      (a_trans + (y * REALY_DIMENSION__), b + (x * REALY_DIMENSION__));
    ++i;
  }}
  return REALXY_FUNC__(set__)(dst, tmp);
}
#endif /* REALX_DIMENSION__ == REALY_DIMENSION__ */

#ifdef COMPILER_GCC
  #pragma GCC pop_options
#endif
