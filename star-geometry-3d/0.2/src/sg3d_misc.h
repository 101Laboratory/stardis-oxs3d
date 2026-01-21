/* Copyright (C) 2019, 2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SG3D_MISC_H__
#define SG3D_MISC_H__

#include <rsys/dynamic_array.h>

#define LIB_NAME "star-geometry-3d"

#ifdef NDEBUG
#define ERR(Expr) if((res = (Expr)) != RES_OK) goto error;
#else
#define ERR(Expr) \
  if((res = (Expr)) != RES_OK) {\
    fprintf(stderr, LIB_NAME":%s: error code set to %d at line %d\n",\
      FUNC_NAME, res, __LINE__);\
    goto error;\
  }
#endif

/* The following types must be defined accordingly with the types
 * used in sg3d.h */

/* Trg IDs use the same type than Side IDs */
typedef unsigned trg_id_t;
/* TRG_MAX__ is limited to half the max of the base type to allow to count
* sides */
#define TRG_MAX__ (UINT_MAX/2)
#define TRG_NULL__ UINT_MAX
#define PRTF_TRG "%u"
#define TRG_TYPE_NAME "unsigned"

/* Side IDs type  use the same base type than Trg IDs */
typedef trg_id_t side_id_t;
#define SIDE_MAX__ (2*TRG_MAX__)
#define SIDE_NULL__ TRG_NULL__

/* Vertex IDs type */
typedef unsigned vrtx_id_t;
#define VRTX_MAX__ (UINT_MAX-1)
#define VRTX_NULL__ UINT_MAX
#define PRTF_VRTX "%u"
#define VRTX_TYPE_NAME "unsigned"

#define DARRAY_NAME vertice_ids
#define DARRAY_DATA vrtx_id_t
#include <rsys/dynamic_array.h>

/* Property IDs type.
 * Cannot be larger than unsigned, as the API uses it. */
typedef unsigned prop_id_t;
#define PROP_MAX__ (UINT_MAX-1) /* MAX is for unspecified medium */
#define PROP_NULL__ UINT_MAX
#define PRTF_PROP "%u"
#define PROP_TYPE_NAME "unsigned"

#if (PROP_MAX__+1 != SG3D_UNSPECIFIED_PROPERTY)
#error "Inconsistant values"
#endif

/* Types of the callbacks.
 * Provided callbacks are cast into these types to avoid arguments cast
 * and to check types coherency */
typedef void(*get_indices_t) (const trg_id_t, prop_id_t*, void*);
typedef void(*get_properties_t) (const trg_id_t, prop_id_t*, void*);
typedef void(*get_position_t) (const vrtx_id_t, double*, void*);
typedef res_T(*add_triangle_t) (const trg_id_t, const trg_id_t, void*);
typedef res_T(*merge_triangle_t)
  (const trg_id_t, const trg_id_t, const int, prop_id_t*, const prop_id_t*,
   void*, int*);
typedef res_T(*degenerated_triangle_t) (const trg_id_t, void*, int*);

#endif /* SG3D_MISC_H__ */
