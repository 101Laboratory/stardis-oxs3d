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
#if defined(REALXY_BEGIN_H)
  #error The realXY_begin.h header is already included
#endif
#define REALXY_BEGIN_H

/* Check that the expected arguments are defined */
#if !defined(REALX_DIMENSION__)                                                \
 || !defined(REALY_DIMENSION__)                                                \
 || !defined(REAL_TYPE__)
  #error Missing arguments
#endif

#if defined(REALX_REALXY_FUNC__)                                               \
 || defined(REALXY_CAST__)                                                     \
 || defined(REALXY_CTOR__)                                                     \
 || defined(REALXY_FUNC__)                                                     \
 || defined(REALXY_REALX_FUNC__)                                               \
 || defined(REALXY_REALXY_FUNC__)                                              \
 || defined(REALY_FUNC__)                                                      \
 || defined(SIZEOF_REALXY__)
  #error Unexpected macro definition
#endif

/* Define the name generator for the realX x realXY -> realXY functions */
#define REALX_REALXY_FUNC__(Func)                                              \
  REALX_FUNC__(CONCAT(CONCAT(CONCAT(                                           \
    mul, REAL_LETTER__), REALX_DIMENSION__), REALY_DIMENSION__))

/* Define the realXY cast functions name */
#define REALXY_CAST__ REALXY_FUNC__ (CONCAT(set_, CONCAT(                      \
  REAL_LETTER_TYPE_COMPATIBLE__, CONCAT(REALX_DIMENSION__, REALY_DIMENSION__))))

/* Define the realXY constructor name */
#define REALXY_CTOR__                                                          \
  CONCAT(CONCAT(REAL_LETTER__, REALX_DIMENSION__), REALY_DIMENSION__)

/* Define the function name generators */
#define REALXY_FUNC__(Func)                                                    \
  CONCAT(CONCAT(CONCAT(CONCAT                                                  \
    (REAL_LETTER__, REALX_DIMENSION__), REALY_DIMENSION__), _), Func)

/* Define the name generator for the realXY x realX -> realX functions */
#define REALXY_REALX_FUNC__(Func)                                              \
  REALXY_FUNC__(CONCAT(CONCAT(mul, REAL_LETTER__), REALX_DIMENSION__))

/* Define the name generator for the realXY x realXY -> realXY functions */
#define REALXY_REALXY_FUNC__(Func)                                             \
  REALXY_FUNC__(CONCAT(CONCAT(CONCAT(                                          \
    Func, REAL_LETTER__), REALX_DIMENSION__), REALY_DIMENSION__))

/* Define the function name generator for the realY functions */
#define REALY_FUNC__(Func)                                                     \
  CONCAT(CONCAT(CONCAT(REAL_LETTER__, REALY_DIMENSION__), _), Func)

#define SIZEOF_REALXY__ sizeof(REAL_TYPE__[REALX_DIMENSION__*REALY_DIMENSION__])

/* Check the validity of the dimensions */
#include "rsys.h"
STATIC_ASSERT
  (REALX_DIMENSION__ > 1 && REALY_DIMENSION__ > 1, Unexpected_value);

#include "realX_begin.h"
