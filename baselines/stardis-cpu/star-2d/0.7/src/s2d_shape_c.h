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

#ifndef S2D_SHAPE_C_H
#define S2D_SHAPE_C_H

#include <rsys/list.h>
#include <rsys/ref_count.h>

struct s3d_device;
struct line_segments;

struct s2d_shape {
  struct list_node scene_attachment;
  struct fid id; /* Uname identifier of the shape */
  
  char flip_contour;
  char is_enabled;

  struct line_segments* lines;
  struct s2d_device* dev;
  ref_T ref;
};

#endif /* S2D_SHAPE_C_H */

