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

/* This file can be included once */
#if defined(REALX_BEGIN_H)
  #error The realX_begin.h header is already included
#endif
#define REALX_BEGIN_H

/* Check that the expected arguments are defined */
#if !defined(REALX_DIMENSION__) || !defined(REAL_TYPE__)
  #error Missing arguments
#endif

/* Check that internal macros are not already defined */
#if defined(REAL_EPSILON__)                                                    \
 || defined(REAL_EPSILON_double)                                               \
 || defined(REAL_EPSILON_float)                                                \
 || defined(REAL_EQ_EPS__)                                                     \
 || defined(REAL_EQ_EPS_double)                                                \
 || defined(REAL_EQ_EPS_float)                                                 \
 || defined(REAL_LETTER__)                                                     \
 || defined(REAL_LETTER_double)                                                \
 || defined(REAL_LETTER_float)                                                 \
 || defined(REAL_LETTER_TYPE_COMPATIBLE__)                                     \
 || defined(REAL_TYPE_COMPATIBLE__)                                            \
 || defined(REAL_TYPE_COMPATIBLE_double)                                       \
 || defined(REAL_TYPE_COMPATIBLE_float)                                        \
 || defined(REALX_CAST__)                                                      \
 || defined(REALX_CTOR__)                                                      \
 || defined(REALX_FUNC__)                                                      \
 || defined(REALX_REAL_FUNC__)                                                 \
 || defined(SIZEOF_REALX__)
  #error Unexpected macro definition
#endif

/* Define the epsilon for each real type */
#define REAL_EPSILON__ CONCAT(REAL_EPSILON_, REAL_TYPE__)
#define REAL_EPSILON_double 1.e-8
#define REAL_EPSILON_float 1.e-6f

/* Define the eq_eps function for each type */
#define REAL_EQ_EPS__ CONCAT(REAL_EQ_EPS_, REAL_TYPE__)
#define REAL_EQ_EPS_double eq_eps
#define REAL_EQ_EPS_float eq_epsf

/* Define the suffix/prefix letter for each real type */
#define REAL_LETTER__ CONCAT(REAL_LETTER_, REAL_TYPE__)
#define REAL_LETTER_float f
#define REAL_LETTER_double d
#define REAL_LETTER_TYPE_COMPATIBLE__ \
  CONCAT(REAL_LETTER_, REAL_TYPE_COMPATIBLE__)

/* Define the type that is compatible to the current real type */
#define REAL_TYPE_COMPATIBLE__ CONCAT(REAL_TYPE_COMPATIBLE_, REAL_TYPE__)
#define REAL_TYPE_COMPATIBLE_double float
#define REAL_TYPE_COMPATIBLE_float double

/* Define name of the cast function */
#define REALX_CAST__ REALX_FUNC__ \
  (CONCAT(set_, CONCAT(REAL_LETTER_TYPE_COMPATIBLE__, REALX_DIMENSION__)))

/* Define the realX constructor from reals */
#define REALX_CTOR__ CONCAT(REAL_LETTER__, REALX_DIMENSION__)


/* Define the function name generator */
#define REALX_FUNC__(Func) \
  CONCAT(CONCAT(CONCAT(REAL_LETTER__, REALX_DIMENSION__), _), Func)

/* Define the name generator for the realX x real -> realX functions */
#define REALX_REAL_FUNC__(Func) CONCAT(REALX_FUNC__(Func), REAL_LETTER__)

/* Helper macro */
#define SIZEOF_REALX__ sizeof(REAL_TYPE__[REALX_DIMENSION__])

/* Check the validity of the vector dimension */
#include "rsys.h"
STATIC_ASSERT(REALX_DIMENSION__ > 1, Unexpected_value);
