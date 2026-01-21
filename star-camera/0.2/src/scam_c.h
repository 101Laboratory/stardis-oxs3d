/* Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef SCAM_C_H
#define SCAM_C_H

#include "scam.h" /* For enum scam_type */

#include <rsys/logger.h>
#include <rsys/ref_count.h>

struct perspective {
  double screen2world[9];
  double camera2world[9];

  double position[3]; /* Lens position */

  double solid_angle; /* In sr */
  double rcp_tan_half_fov; /* 1 / tan(vertical_fov / 2) */
  double aspect_ratio; /* width / height */
  double lens_radius; /* 0 <=> pinhole camera */
  double focal_distance; /* Unused when lens_radius == 0 */
};
#define PERSPECTIVE_DEFAULT__ {                                                \
  {1,0,0, 0,1,0, 0,0,1}, /* Screen to world transformation */                  \
  {1,0,0, 0,1,0, 0,0,1}, /* Camera to world transformation */                  \
  {0,0,0}, /* Lens position */                                                 \
  0.60111772988434627069, /* horizontal_fov * 2*sin(vertical_fov/2) */         \
  1.0, /* 1/tan(vertical_fov/2) */                                             \
  1.0, /* Aspect ratio */                                                      \
  0.0, /* Lens radius */                                                       \
  -1.0 /* Focal distance */                                                    \
}
static const struct perspective PERSPECTIVE_DEFAULT = PERSPECTIVE_DEFAULT__;

struct orthographic {
  double screen2world[9];
  double camera2world[9];

  double position[3]; /* Lens position */

  double height; /* Height of the image plane */
  double aspect_ratio; /* width / height */
};
#define ORTHOGRAPHIC_DEFAULT__ {                                               \
  {1,0,0, 0,1,0, 0,0,1}, /* Screen to world transformation */                  \
  {1,0,0, 0,1,0, 0,0,1}, /* Camera to world transformation */                  \
  {0,0,0}, /* Lens position */                                                 \
  1.0, /* Height */                                                            \
  1.0, /* Aspect ratio */                                                      \
}
static const struct orthographic ORTHOGRAPHIC_DEFAULT = ORTHOGRAPHIC_DEFAULT__;

struct scam {
  enum scam_type type;
  union {
    struct perspective persp;
    struct orthographic ortho;
  } param;

  int verbose;
  struct logger* logger;
  struct logger logger__;

  struct mem_allocator* allocator;
  ref_T ref;
};

extern LOCAL_SYM res_T
camera_create
  (struct logger* logger, /* NULL <=> use builtin logger */
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   const enum scam_type type,
   struct scam** scam);

extern LOCAL_SYM void
orthographic_generate_ray
  (const struct scam* cam,
   const struct scam_sample* sample,
   struct scam_ray* ray);

extern LOCAL_SYM void
perspective_generate_ray
  (const struct scam* cam,
   const struct scam_sample* sample,
   struct scam_ray* ray);

#endif /* SCAM_C_H */
