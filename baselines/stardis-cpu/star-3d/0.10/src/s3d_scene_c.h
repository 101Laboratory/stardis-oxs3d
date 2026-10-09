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

#ifndef S3D_SCENE_C_H
#define S3D_SCENE_C_H

#include <rsys/hash_table.h>
#include <rsys/list.h>
#include <rsys/ref_count.h>
#include <rsys/signal.h>

/* Generate the htable_shape hash table */
#define HTABLE_NAME shape
#define HTABLE_DATA struct s3d_shape*
#define HTABLE_KEY unsigned /* Id of the shape */
#include <rsys/hash_table.h>

/* Declare the scene_shape_cb_T callback data type */
CLBK(scene_shape_cb_T, ARG2
  (const struct s3d_scene* scn,
   const struct s3d_shape* shape));

struct s3d_scene {
  struct htable_shape shapes; /* List of attached shapes */
  size_t instances_count; /* # instances in the scene */
  struct list_node scnviews; /* Pool of available s3d_scene_view */

  signal_T sig_shape_detach;

  struct s3d_device* dev;
  ref_T ref;
};

#endif /* S3D_SCENE_C_H */

