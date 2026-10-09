/* Copyright (C) 2016, 2018, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef S3DSTL_H
#define S3DSTL_H

#include <rsys/rsys.h>

/* Library symbol management */
#if defined(S3DSTL_SHARED_BUILD)
  #define S3DSTL_API extern EXPORT_SYM /* Build shared library */
#elif defined(S3DSTL_STATUC) /* Use/build statuc library */
  #define S3DSTL_API extern LOCAL_SYM
#else /* Use shared library */
  #define S3DSTL_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the s3dstl function `Func'
 * returns an error. One should use this macro on s3dstl function calls for which
 * no explicit error checking is performed. */
#ifndef NDEBUG
  #define S3DSTL(Func) ASSERT(s3dstl_ ## Func == RES_OK)
#else
  #define S3DSTL(Func) s3dstl_ ## Func
#endif

/* Forward declaration of external types */
struct logger;
struct mem_allocator;
struct s3d_device;
struct s3d_shape;
struct sstl;

/* Forward declaration of opaque data types */
struct s3dstl;

/*******************************************************************************
 * Star-3DSTL API
 ******************************************************************************/
BEGIN_DECLS

S3DSTL_API res_T
s3dstl_create
  (struct logger* logger, /* May be NULL <=> use default logger */
   struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   struct sstl* loader_sstl, /* May be NULL <=> Internally create the loader */
   struct s3d_device* s3d,
   const int verbose, /* Verbosity level */
   struct s3dstl** s3dstl);

S3DSTL_API res_T
s3dstl_ref_get
  (struct s3dstl* s3dstl);

S3DSTL_API res_T
s3dstl_ref_put
  (struct s3dstl* s3dstl);

S3DSTL_API res_T
s3dstl_get_sstl
  (struct s3dstl* s3dstl,
   struct sstl** sstl);

S3DSTL_API res_T
s3dstl_load
  (struct s3dstl* s3dstl,
   const char* filename);

S3DSTL_API res_T
s3dstl_load_stream
  (struct s3dstl* s3dstl,
   FILE* file);

/* Remove the loaded shape */
S3DSTL_API res_T
s3dstl_clear
  (struct s3dstl* s3dstl);

S3DSTL_API res_T
s3dstl_get_shape
  (struct s3dstl* s3dstl,
   struct s3d_shape** shape); /* The returned value may be NULL <=> no shape */

END_DECLS

#endif /* S3DSTL_H */

