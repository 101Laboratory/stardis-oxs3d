/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#ifndef SENC2D_INTERNAL_TYPES_H
#define SENC2D_INTERNAL_TYPES_H

#include "senc2d.h"

#include <rsys_math.h>

#include <stdio.h>
#include <stdint.h>

/* Utility macros */
#define LIB_NAME "star-enclosures-2d"

#ifdef NDEBUG
#define OK2(Expr)\
  if((tmp_res = (Expr)) != RES_OK) goto tmp_error;

#define OK(Expr)\
  if((res = (Expr)) != RES_OK) goto error;
#else
#define OK2(Expr)\
  if((tmp_res = (Expr)) != RES_OK) {\
    fprintf(stderr, LIB_NAME":%s: error code set to %d at line %d\n", \
      FUNC_NAME, tmp_res, __LINE__);\
    goto tmp_error;\
  }

#define OK(Expr)\
  if((res = (Expr)) != RES_OK) {\
    fprintf(stderr, LIB_NAME":%s: error code set to %d at line %d\n", \
      FUNC_NAME, res, __LINE__);\
    goto error;\
  }
#endif

/* Helper type */
typedef unsigned char uchar;

/* The following types must be defined accordingly with the types
 * used in senc2d.h */

/* Segment IDs use the same type than Side IDs */
typedef unsigned seg_id_t;
/* SEG_MAX__ is limited to half the max of the base type to allow to count
 * sides */
#define SEG_MAX__ (UINT_MAX/2)
#define SEG_NULL__ UINT_MAX
#define PRTF_SEG "%u"
static INLINE int
cmp_seg_id
  (const void* ptr1, const void* ptr2)
{
  const seg_id_t* t1 = ptr1;
  const seg_id_t* t2 = ptr2;
  return (int)(*t1) - (int)(*t2);
}

/* Side IDs type  use the same base type than Segment IDs */
typedef seg_id_t side_id_t;
#define SIDE_MAX__ (2*SEG_MAX__)
#define SIDE_NULL__ SEG_NULL__

/* Vertex IDs type */
typedef unsigned vrtx_id_t;
#define VRTX_MAX__ (UINT_MAX-1)
#define VRTX_NULL__ UINT_MAX
#define PRTF_VRTX "%u"

/* Edge IDs use the same type than vertex IDs */
typedef vrtx_id_t edge_id_t;
#define EDGE_MAX__ VRTX_MAX__
#define EDGE_NULL__ VRTX_NULL__

/* Medium IDs type */
typedef unsigned medium_id_t;
#define MEDIUM_MAX__ (UINT_MAX-1) /* MAX is for unspecified medium */
#define MEDIUM_NULL__ UINT_MAX
#define PRTF_MDM "%u"

static FINLINE medium_id_t
medium_idx_2_medium_id(int64_t m_idx) {
  return m_idx ? (medium_id_t)(m_idx - 1) : SENC2D_UNSPECIFIED_MEDIUM;
}

static FINLINE unsigned
medium_id_2_medium_idx(medium_id_t medium) {
  uint64_t tmp = (medium == SENC2D_UNSPECIFIED_MEDIUM) ? 0 : medium + 1;
  ASSERT(tmp <= UINT_MAX);
  return (unsigned)tmp;
}

/* Enclosure IDs type */
typedef unsigned enclosure_id_t;
#define ENCLOSURE_MAX__ (UINT_MAX-1)
#define ENCLOSURE_NULL__ UINT_MAX

/* Component IDs use the same type than enclosure IDs */
typedef enclosure_id_t component_id_t;
#define COMPONENT_MAX__ (UINT_MAX-2) /* To allow special values */
#define COMPONENT_NULL__ UINT_MAX
/* Special values */
#define CC_GROUP_ROOT_NONE UINT_MAX
#define CC_GROUP_ROOT_INFINITE (UINT_MAX-1)
#define CC_GROUP_ID_NONE UINT_MAX
#define CC_ID_NONE UINT_MAX

#if (MEDIUM_MAX__+1 != SENC2D_UNSPECIFIED_MEDIUM)
#error "Inconsistant values"
#endif

/* This one is used as flag */
enum side_flag {
  FLAG_FRONT = BIT(0),
  FLAG_BACK = BIT(1)
};

/* Utility macros */
static FINLINE seg_id_t
SEGSIDE_2_SEG(side_id_t s) {
  ASSERT(((size_t)s >> 1) <= SEG_MAX__);
  return s >> 1;
}

static FINLINE int
SEGSIDE_IS_FRONT(side_id_t s) {
  return (s & 1) == 0;
}

static FINLINE enum senc2d_side
SEGSIDE_2_SIDE(side_id_t s) {
  return (s & 1) ? SENC2D_BACK : SENC2D_FRONT;
}

static FINLINE enum side_flag
SEGSIDE_2_SIDEFLAG(side_id_t s) {
  return (s & 1) ? FLAG_BACK : FLAG_FRONT;
}

static FINLINE uchar
SIDE_CANCELED_FLAG(enum side_flag f) {
  ASSERT((f << 4) <= UCHAR_MAX);
  return (uchar)(f << 4);
}

static FINLINE side_id_t
SEGIDxSIDE_2_SEGSIDE(seg_id_t s, enum senc2d_side i) {
  size_t r;
  ASSERT(i == SENC2D_FRONT || i == SENC2D_BACK);
  r = (s << 1) | (i == SENC2D_BACK);
  ASSERT(r <= SIDE_MAX__);
  return (side_id_t)r;
}

static FINLINE side_id_t
SEGSIDE_OPPOSITE(side_id_t s) {
  return SEGIDxSIDE_2_SEGSIDE(SEGSIDE_2_SEG(s),
    SEGSIDE_IS_FRONT(s) ? SENC2D_BACK : SENC2D_FRONT);
}

#endif /* SENC2D_INTERNAL_TYPES_H */
