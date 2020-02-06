/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_XD_BEGIN_H
#define SDIS_XD_BEGIN_H

#include <rsys/rsys.h>

/* Forward declaration */
struct green_path_handle;
struct sdis_heat_path;

struct rwalk_context {
  struct green_path_handle* green_path;
  struct sdis_heat_path* heat_path;
  double Tarad; /* Ambient radiative temperature */
  double Tref3; /* Reference temperature ^ 3 */
};
#define RWALK_CONTEXT_NULL__ {NULL, NULL, 0, 0}
static const struct rwalk_context RWALK_CONTEXT_NULL = RWALK_CONTEXT_NULL__;

#endif /* SDIS_XD_BEGIN_H */

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
#elif SDIS_XD_DIMENSION == 3
  #include <rsys/double3.h>
  #include <rsys/float3.h>
  #include <star/s3d.h>
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
#define SXD_SAMPLE CONCAT(CONCAT(S, DIM), D_SAMPLE)

/* Vector macros generic to SDIS_XD_DIMENSION */
#define dX(Func) CONCAT(CONCAT(CONCAT(d, DIM), _), Func)
#define fX(Func) CONCAT(CONCAT(CONCAT(f, DIM), _), Func)
#define fX_set_dX CONCAT(CONCAT(CONCAT(f, DIM), _set_d), DIM)
#define dX_set_fX CONCAT(CONCAT(CONCAT(d, DIM), _set_f), DIM)

/* Macro making generic its submitted nae to SDIS_XD_DIMENSION */
#define XD(Name) CONCAT(CONCAT(CONCAT(Name, _), DIM), d)

/* Generate the generic data structures and constants */
#if (SDIS_XD_DIMENSION == 2 && !defined(SDIS_2D_H)) \
||  (SDIS_XD_DIMENSION == 3 && !defined(SDIS_3D_H))
  #if SDIS_XD_DIMENSION == 2
    #define SDIS_2D_H
  #else
    #define SDIS_3D_H
  #endif

/* Current state of the random walk */
struct XD(rwalk) {
  struct sdis_rwalk_vertex vtx; /* Position and time of the Random walk */
  struct sdis_medium* mdm; /* Medium in which the random walk lies */
  struct sXd(hit) hit; /* Hit of the random walk */
  enum sdis_side hit_side;
};
static const struct XD(rwalk) XD(RWALK_NULL) = {
  SDIS_RWALK_VERTEX_NULL__, NULL, SXD_HIT_NULL__, SDIS_SIDE_NULL__
};

struct XD(temperature) {
  res_T (*func)/* Next function to invoke in order to compute the temperature */
    (struct sdis_scene* scn,
     const double fp_to_meter,
     const struct rwalk_context* ctx,
     struct XD(rwalk)* rwalk,
     struct ssp_rng* rng,
     struct XD(temperature)* temp);
  double value; /* Current value of the temperature */
  int done;
};
static const struct XD(temperature) XD(TEMPERATURE_NULL) = { NULL, 0, 0 };

#endif /* SDIX_<2|3>D_H */

