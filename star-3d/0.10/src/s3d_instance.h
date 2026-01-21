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

#ifndef S3D_INSTANCE_H
#define S3D_INSTANCE_H

#include "s3d_geometry.h"
#include <rsys/dynamic_array.h>
#include <rsys/ref_count.h>

struct instance {
  float transform[12]; /* local to world 3x4 column major matrix */
  struct s3d_scene* scene; /* Instantiated scene */
  /* Current view of the instantiated scene. Note that the instance does not
   * own the scnview; the instance scnview lifetime is managed by the scnview
   * into which the instance lies */
  struct s3d_scene_view* scnview;
  ref_T ref;
};

extern LOCAL_SYM res_T
instance_create
  (struct s3d_scene* scene,
   struct instance** inst);

extern LOCAL_SYM void
instance_ref_get
  (struct instance* inst);

extern LOCAL_SYM void
instance_ref_put
  (struct instance* inst);

#endif /* S3D_INSTANCE_H */

