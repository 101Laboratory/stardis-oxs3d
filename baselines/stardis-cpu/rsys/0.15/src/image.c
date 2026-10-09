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

#define _POSIX_C_SOURCE 200112L /* snprintf support */
#include "cstr.h"
#include "image.h"
#include "mem_allocator.h"
#include <stdio.h>
#include <string.h>

enum ppm_id { P3, P6 };

struct parser {
  char buf[512];
  char* tk;
  FILE* stream;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
parser_init(struct parser* parser, FILE* stream)
{
  ASSERT(parser && stream);
  parser->tk = NULL;
  parser->stream = stream;
}

static char*
parser_next_token(struct parser* parser)
{
  do {
    if(parser->tk) parser->tk = strtok(NULL, " \t\n");

    if(!parser->tk) {
      char* line = fgets(parser->buf, (int)sizeof(parser->buf), parser->stream);
      if(line) parser->tk = strtok(line, " \t\n");
    }

    if(parser->tk && parser->tk[0] == '#')
      parser->tk = NULL;

  } while(!parser->tk && !feof(parser->stream));
  return parser->tk;
}

static INLINE res_T
parse_ppm_id(const char* str, enum ppm_id* id)
{
  ASSERT(id);
  if(!str) return RES_BAD_ARG;
  if(!strcmp(str, "P3")) {
    *id = P3;
  } else if(!strcmp(str, "P6")) {
    *id = P6;
  } else {
    return RES_BAD_ARG;
  }
  return RES_OK;
}

static res_T
parse_raw_pixels
  (struct parser* parser,
   const size_t width,
   const size_t height,
   const enum image_format fmt,
   char* buffer)
{
  size_t i, n;
  res_T res = RES_OK;
  ASSERT(parser && width > 0 && height > 0 && buffer);

  n = (size_t)(width * height * 3/*#channels*/);
  FOR_EACH(i, 0, n) {
    unsigned val;

    res = cstr_to_uint(parser_next_token(parser), &val);
    if(res != RES_OK) return res;

    switch(fmt) {
      case IMAGE_RGB8:
        if(val > UINT8_MAX) return RES_BAD_ARG;
        ((uint8_t*)buffer)[i] = (uint8_t)val;
        break;
      case IMAGE_RGB16:
        if(val > UINT16_MAX) return RES_BAD_ARG;
        ((uint16_t*)buffer)[i] = (uint16_t)val;
        break;
      default: FATAL("Unreachable code.\n"); break;
    }
  }
  return RES_OK;
}

static INLINE res_T
parse_bin_pixels
  (struct parser* parser,
   const size_t width,
   const size_t height,
   const enum image_format fmt,
   char* buffer)
{
  size_t n, size;
  ASSERT(parser && width > 0 && height > 0 && buffer);
  switch(fmt) {
    case IMAGE_RGB8: size = 1; break;
    case IMAGE_RGB16: size = 2; break;
    default: FATAL("Unreachable code.\n"); break;
  }
  n = (size_t)(width * height * 3/*#channels*/);
  return (n == fread(buffer, size, n, parser->stream)) ? RES_OK : RES_BAD_ARG;
}

static res_T
write_bin_ppm(const struct image* img, FILE* stream)
{
  size_t y;
  ASSERT(img && stream);

  FOR_EACH(y, 0, img->height) {
    const char* row = img->pixels + y * img->pitch;
    size_t n;
    n = fwrite(row, sizeof_image_format(img->format), img->width, stream);
    if(n < img->width) return RES_IO_ERR;
  }
  return RES_OK;
}

static res_T
write_raw_ppm(const struct image* img, FILE* stream)
{
  size_t x, y;
  ASSERT(img && stream);

  FOR_EACH(y, 0, img->height) {
    const char* row = img->pixels + y * img->pitch;
    FOR_EACH(x, 0, img->width) {
      const char* pixel = row + x * sizeof_image_format(img->format);
      switch(img->format) {
        case IMAGE_RGB8:
          fprintf(stream, "%u %u %u\n", SPLIT3((uint8_t*)pixel));
          break;
        case IMAGE_RGB16:
          fprintf(stream, "%u %u %u\n", SPLIT3(((uint16_t*)pixel)));
          break;
        default: FATAL("Unreachable code.\n"); break;
      }
    }
  }
  return RES_OK;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
image_init(struct mem_allocator* mem_allocator, struct image* img)
{
  struct mem_allocator* allocator;
  if(!img) return RES_BAD_ARG;
  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  memset(img, 0, sizeof(struct image));
  img->allocator = allocator;
  return RES_OK;
}

res_T
image_release(struct image* img)
{
  if(!img) return RES_BAD_ARG;
  if(img->pixels) MEM_RM(img->allocator, img->pixels);
  return RES_OK;
}

res_T
image_setup
  (struct image* img,
   const size_t width,
   const size_t height,
   const size_t pitch,
   const enum image_format format,
   const char* pixels)
{
  size_t size;
  char* buffer = NULL;
  res_T res = RES_OK;

  if(!img || !width || !height || !pitch || pitch < width) {
    res = RES_BAD_ARG;
    goto error;
  }

  size = height * pitch;
  if(size != img->height * img->pitch) {
    buffer = MEM_ALLOC(img->allocator, size);
    if(!buffer) {
      res = RES_MEM_ERR;
      goto error;
    }
    if(img->pixels) MEM_RM(img->allocator, img->pixels);
    img->pixels = buffer;
  }

  if(pixels) {
    memcpy(img->pixels, pixels, size);
  }

  img->width = width;
  img->height = height;
  img->pitch = pitch;
  img->format = format;

exit:
  return res;
error:
  if(buffer) MEM_RM(img->allocator, buffer);
  goto exit;
}

res_T
image_read_ppm(struct image* img, const char* filename)
{
  FILE* stream = NULL;
  res_T res = RES_OK;

  if(!img || !filename) {
    res = RES_BAD_ARG;
    goto error;
  }

  stream = fopen(filename, "r");
  if(!stream) {
    res = RES_IO_ERR;
    goto error;
  }

  res = image_read_ppm_stream(img, stream);
  if(res != RES_OK) goto error;

exit:
  if(stream) fclose(stream);
  return res;
error:
  goto exit;
}

res_T
image_read_ppm_stream(struct image* img, FILE* stream)
{
  struct parser parser;
  size_t pitch;
  unsigned long width=0, height=0, max_val=0;
  enum ppm_id id;
  enum image_format fmt;
  res_T res = RES_OK;

  if(!img || !stream) return RES_BAD_ARG;

  parser_init(&parser, stream);

  /* Read header */
  #define CALL(Func) { res = Func; if(res != RES_OK) goto error; } (void)0
  CALL(parse_ppm_id(parser_next_token(&parser), &id));
  CALL(cstr_to_ulong(parser_next_token(&parser), &width));
  CALL(cstr_to_ulong(parser_next_token(&parser), &height));
  CALL(cstr_to_ulong(parser_next_token(&parser), &max_val));
  #undef CALL

  /* Check header */
  if(!width || !height || !max_val || max_val > 65535) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Allocate the image buffer */
  fmt = max_val <= 255 ? IMAGE_RGB8 : IMAGE_RGB16;
  pitch = width * sizeof_image_format(fmt);
  res = image_setup(img, width, height, pitch, fmt, NULL);
  if(res != RES_OK) goto error;

  /* Read pixel data */
  switch(id) {
    case P3:
      res = parse_raw_pixels(&parser, width, height, fmt, img->pixels);
      break;
    case P6:
      res = parse_bin_pixels(&parser, width, height, fmt, img->pixels);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
image_write_ppm
  (const struct image* img,
   const int binary,
   const char* filename)
{
  FILE* stream = NULL;
  res_T res = RES_OK;

  if(!img || !filename) {
    res = RES_BAD_ARG;
    goto error;
  }

  stream = fopen(filename, "w");
  if(!stream) {
    res = RES_IO_ERR;
    goto error;
  }

  res = image_write_ppm_stream(img, binary, stream);
  if(res != RES_OK) goto error;

exit:
  if(stream) fclose(stream);
  return res;
error:
  goto exit;
}

res_T
image_write_ppm_stream
  (const struct image* img,
   const int bin,
   FILE* stream)
{
  res_T res = RES_OK;

  if(!img || !stream) {
    res = RES_BAD_ARG;
    goto error;
  }

  fprintf(stream, "%s %lu %lu\n", bin ? "P6" : "P3",
    (unsigned long)img->width,
    (unsigned long)img->height);
  switch(img->format) { /* Write Max val */
    case IMAGE_RGB8: fprintf(stream, "255\n"); break;
    case IMAGE_RGB16: fprintf(stream, "65535\n"); break;
    default: FATAL("Unreachable code.\n"); break;
  }

  if(bin) {
    res = write_bin_ppm(img, stream);
  } else {
    res = write_raw_ppm(img, stream);
  }
  if(res != RES_OK) goto error;


exit:
  return res;
error:
  goto exit;
}
