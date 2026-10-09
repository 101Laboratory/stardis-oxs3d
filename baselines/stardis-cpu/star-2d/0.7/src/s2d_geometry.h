/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef S2D_GEOMETRY_H
#define S2D_GEOMETRY_H

#include "s2d.h"
#include "s2d_backend.h"
#include <rsys/ref_count.h>

enum embree_attrib {
  EMBREE_ENABLE = BIT(0),
  EMBREE_FILTER_FUNCTION = BIT(1),
  EMBREE_INDICES = BIT(2),
  EMBREE_VERTICES = BIT(4)
};

/* Backend geometry */
struct geometry {
  unsigned name; /* Client side identifier */
  RTCGeometry rtc; /* Embree geometry */
  unsigned rtc_id; /* Embree geometry identifier */
  unsigned scene_prim_id_offset; /* Offset from local to scene prim_id */

  int embree_outdated_mask; /* Combination of embree_attrib */

  char flip_contour; /* Is the geometry contour flipped? */
  char is_enabled; /* Is the geometry enabled? */

  struct line_segments* lines; /* Reference toward the client side data */

  struct s2d_device* dev;
  ref_T ref;
};

extern LOCAL_SYM res_T
geometry_create
  (struct s2d_device* dev,
   struct geometry** geom);

extern LOCAL_SYM void
geometry_ref_get
  (struct geometry* geometry);

extern LOCAL_SYM void
geometry_ref_put
  (struct geometry* geometry);

#endif /* S2D_GEOMETRY_H */

