/* Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef S3D_SHAPE_C_H
#define S3D_SHAPE_C_H

#include "s3d_mesh.h"
#include "s3d_instance.h"
#include "s3d_sphere.h"

#include <rsys/dynamic_array_u32.h>
#include <rsys/dynamic_array_float.h>
#include <rsys/list.h>
#include <rsys/mutex.h>
#include <rsys/ref_count.h>

#include <limits.h>

struct s3d_shape {
  struct fid id;

  char flip_surface;
  char is_enabled;
  enum geometry_type type;

  union {
    struct instance* instance;
    struct mesh* mesh;
    struct sphere* sphere;
  } data;

  struct s3d_device* dev;
  ref_T ref;
};

/* Create an Untyped shape */
extern LOCAL_SYM res_T
shape_create
  (struct s3d_device* dev,
   struct s3d_shape** shape);

#endif /* S3D_SHAPE_C_H */

