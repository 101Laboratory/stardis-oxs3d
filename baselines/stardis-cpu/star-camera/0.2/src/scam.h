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

#ifndef SCAM_H
#define SCAM_H

#include <rsys/rsys.h>

/* Library symbol management */
#if defined(SCAM_SHARED_BUILD) /* Build shared library */
  #define SCAM_API extern EXPORT_SYM
#elif defined(SCAM_STATIC) /* Use/build static library */
  #define SCAM_API extern LOCAL_SYM
#else /* Use shared library */
  #define SCAM_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the suvm function `Func'
 * returns an error. One should use this macro on scam function calls for
 * which no explicit error checking is performed */
#ifndef NDEBUG
  #define SCAM(Func) ASSERT(scam_ ## Func == RES_OK)
#else
  #define SCAM(Func) scam_ ## Func
#endif

enum scam_type {
  SCAM_ORTHOGRAPHIC,
  SCAM_PERSPECTIVE,
  SCAM_TYPES_COUNT__,
  SCAM_NONE = SCAM_TYPES_COUNT__
};

/* Samples used to generate a camera ray */
struct scam_sample {
  double film[2]; /* Samples on the image plane in [0, 1[^2 */
  double lens[2]; /* Samples on the lens */
};
#define SCAM_SAMPLE_NULL__ {{0,0},{0,0}}
static const struct scam_sample SCAM_SAMPLE_NULL = SCAM_SAMPLE_NULL__;

struct scam_ray {
  double org[3];
  double dir[3];
};
#define SCAM_RAY_NULL__ {{0,0,0},{0,0,0}}
static const struct scam_ray SCAM_RAY_NULL = SCAM_RAY_NULL__;

struct scam_orthographic_args {
  double position[3]; /* Lens position */
  double target[3]; /* Targeted point. target-position = image plane normal */
  double up[3]; /* Vector defining the upward orientation */
  double height; /* Height of the image plane */
  double aspect_ratio;  /* Image plane aspect ratio (width / height) */
};
#define SCAM_ORTHOGRAPHIC_ARGS_DEFAULT__ {                                     \
  {0,0,0}, /* Position */                                                      \
  {0,1,0}, /* Target */                                                        \
  {0,0,1}, /* Up */                                                            \
  1.0, /* Height of the image plane */                                         \
  1.0, /* Aspect ratio */                                                      \
}
static const struct scam_orthographic_args SCAM_ORTHOGRAPHIC_ARGS_DEFAULT =
  SCAM_ORTHOGRAPHIC_ARGS_DEFAULT__;

struct scam_perspective_args {
  double position[3]; /* Lens position */
  double target[3]; /* Targeted point. target-position = image plane normal */
  double up[3]; /* Vector defining the upward orientation */
  double aspect_ratio; /* Image plane aspect ratio (width / height) */
  double lens_radius; /* Radius of 0 <=> pinhole */

  /* Vertical field of view in radians. It indirectly defined the focal length*/
  double field_of_view;

  /* Distance to focus on. Used when lens_radius != 0. Note that the focal
   * distance is not the focal length that is the distance to the focal point
   * behind the lens */
  double focal_distance;
};
#define SCAM_PERSPECTIVE_ARGS_DEFAULT__ {                                      \
  {0,0,0}, /* Position */                                                      \
  {0,1,0}, /* Target */                                                        \
  {0,0,1}, /* Up */                                                            \
  1.0, /* Aspect ratio */                                                      \
  0.0, /* Lens radius */                                                       \
  1.22173047639603070383, /* Fov ~70 degrees */                                \
  1.0 /* Focal distance. Unused for radius == 0 */                             \
}
static const struct scam_perspective_args SCAM_PERSPECTIVE_ARGS_DEFAULT =
  SCAM_PERSPECTIVE_ARGS_DEFAULT__;

/* Forward declaration of external data types */
struct logger;
struct mem_allocator;

/* Opaque data type */
struct scam;

/*******************************************************************************
 * Star-Camera API
 ******************************************************************************/
BEGIN_DECLS

SCAM_API res_T
scam_create_perspective
  (struct logger* logger, /* NULL <=> use builtin logger */
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   struct scam_perspective_args* args,
   struct scam** camera);

SCAM_API res_T
scam_create_orthographic
  (struct logger* logger, /* NULL <=> use builtin logger */
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   struct scam_orthographic_args* args,
   struct scam** camera);

SCAM_API res_T
scam_generate_ray
  (const struct scam* cam,
   const struct scam_sample* sample,
   struct scam_ray* ray);

SCAM_API res_T
scam_get_type
  (const struct scam* cam,
   enum scam_type* type);

SCAM_API res_T
scam_ref_get
  (struct scam* camera);

SCAM_API res_T
scam_ref_put
  (struct scam* camera);

SCAM_API res_T
scam_focal_length_to_field_of_view
  (const double lens_radius,
   const double focal_length,
   double* field_of_view);

SCAM_API res_T
scam_field_of_view_to_focal_length
  (const double lens_radius,
   const double field_of_view,
   double* focal_length);

SCAM_API res_T
scam_perspective_get_solid_angle
  (const struct scam* camera,
   double* solid_angle); /* In sr */

END_DECLS

#endif /* SCAM_H */
