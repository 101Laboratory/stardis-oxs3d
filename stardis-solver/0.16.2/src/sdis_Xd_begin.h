/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "sdis.h"
#include <rsys/rsys.h>

#ifdef SDIS_XD_BEGIN_H__
  #error "This header is already included without its associated sdis_Xd_end.h file."
#endif

#define SDIS_XD_BEGIN_H__

/* Check prerequisite */
#ifndef SDIS_XD_DIMENSION
  #error "The SDIS_XD_DIMENSION macro must be defined."
#endif

#if SDIS_XD_DIMENSION == 2
#include <rsys/double2.h>
#include <rsys/float2.h>
#include <star/s2d.h>

  #define FORMAT_VECX "%g, %g"
  #define SPLITX(V) SPLIT2(V)

#elif SDIS_XD_DIMENSION == 3
#include <rsys/double3.h>
#include <rsys/float3.h>
#include <star/s3d.h>

  #define FORMAT_VECX "%g, %g, %g"
  #define SPLITX(V) SPLIT3(V)

#else
  #error "Invalid dimension."
#endif

/* Syntactic sugar */
#define DIM SDIS_XD_DIMENSION

/* Star-XD macros generic to SDIS_XD_DIMENSION */
#define sXd(Name) CONCAT(CONCAT(CONCAT(s, DIM), d_), Name)
#define SXD_HIT_NONE CONCAT(CONCAT(S,DIM), D_HIT_NONE)
#define SXD_HIT_NULL CONCAT(CONCAT(S,DIM), D_HIT_NULL)
#define SXD_HIT_NULL__ CONCAT(CONCAT(S, DIM), D_HIT_NULL__)
#define SXD_POSITION CONCAT(CONCAT(S, DIM), D_POSITION)
#define SXD_GEOMETRY_NORMAL CONCAT(CONCAT(S, DIM), D_GEOMETRY_NORMAL)
#define SXD_VERTEX_DATA_NULL CONCAT(CONCAT(S, DIM), D_VERTEX_DATA_NULL)
#define SXD CONCAT(CONCAT(S, DIM), D)
#define SXD_FLOAT2 CONCAT(CONCAT(S, DIM), D_FLOAT2)
#define SXD_FLOAT3 CONCAT(CONCAT(S, DIM), D_FLOAT3)
#define SXD_FLOATX CONCAT(CONCAT(CONCAT(S,DIM), D_FLOAT), DIM)
#define SXD_GET_PRIMITIVE CONCAT(CONCAT(S, DIM), D_GET_PRIMITIVE)
#define SXD_SAMPLE CONCAT(CONCAT(S, DIM), D_SAMPLE)
#define SXD_TRACE CONCAT(CONCAT(S, DIM), D_TRACE)
#define SXD_PRIMITIVE_NULL CONCAT(CONCAT(S, DIM), D_PRIMITIVE_NULL)
#define SXD_PRIMITIVE_EQ CONCAT(CONCAT(S, DIM), D_PRIMITIVE_EQ)

/* Vector macros generic to SDIS_XD_DIMENSION */
#define dX(Func) CONCAT(CONCAT(CONCAT(d, DIM), _), Func)
#define fX(Func) CONCAT(CONCAT(CONCAT(f, DIM), _), Func)
#define fX_set_dX CONCAT(CONCAT(CONCAT(f, DIM), _set_d), DIM)
#define fXX_mulfX CONCAT(CONCAT(CONCAT(CONCAT(f, DIM), DIM), _mulf), DIM)
#define dX_set_fX CONCAT(CONCAT(CONCAT(d, DIM), _set_f), DIM)

/* Macro making generic its submitted name to SDIS_XD_DIMENSION */
#define XD(Name) CONCAT(CONCAT(CONCAT(Name, _), DIM), d)
