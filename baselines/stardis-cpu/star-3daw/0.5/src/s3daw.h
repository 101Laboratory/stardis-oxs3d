/* Copyright (C) 2015, 2016, 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#ifndef S3DAW_H
#define S3DAW_H

#include <rsys/rsys.h>

/* Library symbol management */
#if defined(S3DAW_SHARED_BUILD) /* Build shared library */
  #define S3DAW_API extern EXPORT_SYM
#elif defined(S3DAW_STATIC) /* Use/build static library */
  #define S3DAW_API extern LOCAL_SYM
#else /* Use shared library */
  #define S3DAW_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the s3daw function `Func'
 * returns an error. One should use this macro on smc function calls for which
 * no explicit error checking is performed */
#ifndef NDEBUG
  #define S3DAW(Func) ASSERT(s3daw_ ## Func == RES_OK)
#else
  #define S3DAW(Func) s3daw_ ## Func
#endif

/* Forward declaration of external types */
struct aw_obj;
struct aw_mtl;
struct logger;
struct mem_allocator;
struct s3d_device;
struct s3d_shape;
struct s3d_scene;

/* Forward declaration *of opaque s3daw types */
struct s3daw;

/*******************************************************************************
 * S3DAW API
 ******************************************************************************/
BEGIN_DECLS

S3DAW_API res_T
s3daw_create
  (struct logger* logger, /* May be NULL <=> use default logger */
   struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   struct aw_obj* loader_obj, /* May be NULL <=> Internally create the loader */
   struct aw_mtl* loader_mtl, /* May be nULL <=> Internally create the loader */
   struct s3d_device* s3d,
   const int verbose, /* Verbosity level */
   struct s3daw** s3daw);

S3DAW_API res_T
s3daw_ref_get
  (struct s3daw* s3daw);

S3DAW_API res_T
s3daw_ref_put
  (struct s3daw* s3daw);

S3DAW_API res_T
s3daw_get_loaders
  (struct s3daw* s3daw,
   struct aw_obj** obj, /* May be NULL <=> Do not get the obj loader */
   struct aw_mtl** mtl); /* May be NULL <=> Do not get the mtl loader */

/* Return the internally used Star-3D device. Note that the reference counter
 * of device is not incremented. */
S3DAW_API res_T
s3daw_get_s3d_device
  (struct s3daw* s3daw,
   struct s3d_device** device);

S3DAW_API res_T
s3daw_load
  (struct s3daw* s3daw,
   const char* filename);

S3DAW_API res_T
s3daw_load_stream
  (struct s3daw* s3daw,
   FILE* stream);

/* Remove all loaded shapes */
S3DAW_API res_T
s3daw_clear
  (struct s3daw* s3daw);

S3DAW_API res_T
s3daw_get_shapes_count
  (const struct s3daw* s3daw,
   size_t* nshapes);

S3DAW_API res_T
s3daw_get_shape
  (struct s3daw* s3daw,
   const size_t ishape, /* in [0, s3d_get_shapes_count) */
   struct s3d_shape** shape);

/* Attach the loaded s3d shapes to the submitted scene. */
S3DAW_API res_T
s3daw_attach_to_scene
  (struct s3daw* s3daw,
   struct s3d_scene* scene);

/* Detach the loaded s3d shapes from the submitted scene. */
S3DAW_API res_T
s3daw_detach_from_scene
  (struct s3daw* s3daw,
   struct s3d_scene* scene);

END_DECLS

#endif /* S3DAW_H */

