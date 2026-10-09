/* Copyright (C) 2013-2023, 2025 Vincent Forest (vaplv@free.fr)
 *
 * The RSys library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * The RSys library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the RSys library. If not, see <http://www.gnu.org/licenses/>. */

#ifndef IMAGE_H
#define IMAGE_H

#include "rsys.h"

struct mem_allocator;

enum image_format {
  IMAGE_RGB8,
  IMAGE_RGB16
};

struct image {
  size_t width;
  size_t height;
  size_t pitch;
  enum image_format format;
  char* pixels;

  /* Internal data */
  struct mem_allocator* allocator;
};

static FINLINE size_t
sizeof_image_format(const enum image_format fmt)
{
  switch(fmt) {
    case IMAGE_RGB8: return sizeof(uint8_t[3]);
    case IMAGE_RGB16: return sizeof(uint16_t[3]);
    default: FATAL("Unreachable code.\n"); break;
  }
}

BEGIN_DECLS

RSYS_API res_T
image_init
  (struct mem_allocator* allocator, /* May be NULL */
   struct image* img);

RSYS_API res_T
image_release
  (struct image* img);

RSYS_API res_T
image_setup
  (struct image* image,
   const size_t width,
   const size_t height,
   const size_t pitch,
   const enum image_format format,
   const char* pixels); /* May be NULL */

RSYS_API res_T
image_read_ppm
  (struct image* image,
   const char* filename);

RSYS_API res_T
image_read_ppm_stream
  (struct image* image,
   FILE* stream);

RSYS_API res_T
image_write_ppm
  (const struct image* image,
   const int binary,
   const char* filename);

RSYS_API res_T
image_write_ppm_stream
  (const struct image* image,
   const int binary,
   FILE* stream);

END_DECLS

#endif /* IMAGE_H */
