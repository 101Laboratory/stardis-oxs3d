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

#include "image.h"
#include "mem_allocator.h"
#include "test_utils.h"

#define WIDTH 64
#define HEIGHT 32

static void
check_image_eq
  (const struct image* img,
   const char* ref_pixels,
   const size_t ref_width,
   const size_t ref_height,
   const enum image_format ref_fmt)
{
  size_t x, y;
  size_t i;

  CHK(img->format == ref_fmt);
  CHK(img->height == ref_height);
  CHK(img->width == ref_width);
  CHK(img->pitch >= img->width);

  i = 0;
  FOR_EACH(y, 0, img->height) {
    const char* row = img->pixels + img->pitch * (size_t)y;
    FOR_EACH(x, 0, img->width) {
      const char* pixel = row + (size_t)x*sizeof_image_format(img->format);
      switch(img->format) {
        case IMAGE_RGB8:
          CHK(((uint8_t*)pixel)[0] == ((uint8_t*)ref_pixels)[i]), ++i;
          CHK(((uint8_t*)pixel)[1] == ((uint8_t*)ref_pixels)[i]), ++i;
          CHK(((uint8_t*)pixel)[2] == ((uint8_t*)ref_pixels)[i]), ++i;
          break;
        case IMAGE_RGB16:
          CHK(((uint16_t*)pixel)[0] == ((uint16_t*)ref_pixels)[i]), ++i;
          CHK(((uint16_t*)pixel)[1] == ((uint16_t*)ref_pixels)[i]), ++i;
          CHK(((uint16_t*)pixel)[2] == ((uint16_t*)ref_pixels)[i]), ++i;
          break;
        default: FATAL("Unreachable code.\n"); break;
      }
    }
  }
}

static void
check_image_read
  (FILE* fp,
   const size_t width,
   const size_t height,
   const enum image_format fmt,
   struct mem_allocator* allocator)
{
  struct image img;
  size_t max_val;
  int is_bin;
  char id[2];

  CHK(image_init(allocator, &img) == RES_OK);

  CHK(fseek(fp, 0, SEEK_SET) == 0);
  CHK(image_read_ppm_stream(&img, fp) == RES_OK);

  CHK(fseek(fp, 0, SEEK_SET) == 0);
  CHK(fread(id, 1, 2, fp) == 2);
  CHK(id[0] == 'P');
  CHK(id[1] == '3' || id[1] == '6');
  is_bin = id[1] == '6';

  switch(fmt) {
    case IMAGE_RGB8: max_val = 255; break;
    case IMAGE_RGB16: max_val = 65535; break;
    default: FATAL("Unreachable code.\n"); break;
  }

  CHK(fseek(fp, 3, SEEK_SET) == 0);
  fprintf(fp, "%lu %lu\n", (unsigned long)width+1, (unsigned long)height);
  CHK(fseek(fp, 0, SEEK_SET) == 0);
  CHK(image_read_ppm_stream(&img, fp) == RES_BAD_ARG);
  CHK(fseek(fp, 3, SEEK_SET) == 0);
  fprintf(fp, "%lu %lu\n", (unsigned long)width, (unsigned long)height+1);
  CHK(fseek(fp, 0, SEEK_SET) == 0);
  CHK(image_read_ppm_stream(&img, fp) == RES_BAD_ARG);
  CHK(fseek(fp, 3, SEEK_SET) == 0);
  fprintf(fp, "%lu %lu\n", (unsigned long)width, (unsigned long)height);
  fprintf(fp, "%lu\n", (unsigned long)max_val+1);
  CHK(fseek(fp, 0, SEEK_SET) == 0);

  if(is_bin) {
    CHK(image_read_ppm_stream(&img, fp) == RES_BAD_ARG);
  } else {
    switch(fmt) {
      case IMAGE_RGB8:
        CHK(image_read_ppm_stream(&img, fp) == RES_OK);
        break;
      case IMAGE_RGB16:
        CHK(image_read_ppm_stream(&img, fp) == RES_BAD_ARG);
        break;
      default: FATAL("Unreachable code.\n"); break;
    }
  }

  CHK(fseek(fp, 3, SEEK_SET) == 0);
  fprintf(fp, "%lu %lu\n", (unsigned long)width, (unsigned long)height);
  fprintf(fp, "%lu\n", (unsigned long)max_val);
  CHK(fseek(fp, 0, SEEK_SET) == 0);
  CHK(image_read_ppm_stream(&img, fp) == RES_OK);

  CHK(image_release(&img) == RES_OK);
}

static void
check_image
  (const char* pixels,
   const size_t width,
   const size_t height,
   const enum image_format fmt,
   struct mem_allocator* allocator)
{
  struct image img;
  size_t pitch;
  FILE* fp;

  CHK(image_init(NULL, NULL) == RES_BAD_ARG);
  CHK(image_init(allocator, NULL) == RES_BAD_ARG);
  CHK(image_init(NULL, &img) == RES_OK);
  CHK(image_release(NULL) == RES_BAD_ARG);
  CHK(image_release(&img) == RES_OK);
  CHK(image_init(allocator, &img) == RES_OK);

  pitch = width * sizeof_image_format(fmt);

  CHK(image_setup(NULL, 0, 0, 0, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(&img, 0, 0, 0, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(NULL, width, 0, 0, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(&img, width, 0, 0, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(NULL, 0, height, 0, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(&img, 0, height, 0, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(NULL, width, height, 0, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(&img, width, height, 0, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(NULL, 0, 0, pitch, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(&img, 0, 0, pitch, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(NULL, width, 0, pitch, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(&img, width, 0, pitch, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(NULL, 0, height, pitch, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(&img, 0, height, pitch, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(NULL, width, height, pitch, fmt, NULL) == RES_BAD_ARG);
  CHK(image_setup(&img, width, height, pitch, fmt, NULL) == RES_OK);
  CHK(image_setup(NULL, 0, 0, 0, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(&img, 0, 0, 0, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(NULL, width, 0, 0, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(&img, width, 0, 0, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(NULL, 0, height, 0, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(&img, 0, height, 0, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(NULL, width, height, 0, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(&img, width, height, 0, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(NULL, 0, 0, pitch, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(&img, 0, 0, pitch, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(NULL, width, 0, pitch, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(&img, width, 0, pitch, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(NULL, 0, height, pitch, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(&img, 0, height, pitch, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(NULL, width, height, pitch, fmt, pixels) == RES_BAD_ARG);
  CHK(image_setup(&img, width, height, pitch, fmt, pixels) == RES_OK);

  fp = tmpfile();
  CHK(fp != NULL);

  CHK(image_write_ppm_stream(NULL, 0, NULL) == RES_BAD_ARG);
  CHK(image_write_ppm_stream(&img, 0, NULL) == RES_BAD_ARG);
  CHK(image_write_ppm_stream(NULL, 0, fp) == RES_BAD_ARG);
  CHK(image_write_ppm_stream(&img, 0, fp) == RES_OK);

  CHK(image_read_ppm_stream(NULL, NULL) == RES_BAD_ARG);
  CHK(image_read_ppm_stream(&img, NULL) == RES_BAD_ARG);
  CHK(image_read_ppm_stream(NULL, fp) == RES_BAD_ARG);
  CHK(image_read_ppm_stream(&img, fp) == RES_BAD_ARG);
  rewind(fp);
  CHK(image_read_ppm_stream(&img, fp) == RES_OK);
  check_image_eq(&img, pixels, width, height, fmt);

  rewind(fp);
  CHK(image_write_ppm_stream(&img, 1, fp) == RES_OK);
  rewind(fp);
  CHK(image_read_ppm_stream(&img, fp) == RES_OK);
  check_image_eq(&img, pixels, width, height, fmt);

  CHK(image_write_ppm(NULL, 0, NULL) == RES_BAD_ARG);
  CHK(image_write_ppm(&img, 0, NULL) == RES_BAD_ARG);
  CHK(image_write_ppm(NULL, 0, "test.ppm") == RES_BAD_ARG);
  CHK(image_write_ppm(&img, 0, "test.ppm") == RES_OK);

  CHK(image_read_ppm(NULL, NULL) == RES_BAD_ARG);
  CHK(image_read_ppm(&img, NULL) == RES_BAD_ARG);
  CHK(image_read_ppm(NULL, "test_bad.ppm") == RES_BAD_ARG);
  CHK(image_read_ppm(&img, "test_bad.ppm") == RES_IO_ERR);
  CHK(image_read_ppm(&img, "test.ppm") == RES_OK);

  check_image_eq(&img, pixels, width, height, fmt);

  CHK(image_write_ppm(&img, 1, "test.ppm") == RES_OK);
  CHK(image_read_ppm(&img, "test.ppm") == RES_OK);
  check_image_eq(&img, pixels, width, height, fmt);

  CHK(image_write_ppm_stream(&img, 1, stdout) == RES_OK);

  fclose(fp);

  fp = tmpfile();
  CHK(fp != NULL);
  CHK(image_write_ppm_stream(&img, 0, fp) == RES_OK);
  check_image_read(fp, width, height, fmt, allocator);
  fclose(fp);

  fp = tmpfile();
  CHK(fp != NULL);
  CHK(image_write_ppm_stream(&img, 1, fp) == RES_OK);
  check_image_read(fp, width, height, fmt, allocator);
  fclose(fp);

  CHK(image_release(&img) == RES_OK);
}

int
main(int argc, char** argv)
{
  uint8_t lut8[4][4][3] = {
    {{0xFF,0xFF,0x00}, {0xFF,0x00,0x00}, {0xFF,0x00,0x00}, {0xFF,0xFF,0x00}},
    {{0xFF,0xFF,0xFF}, {0x00,0x00,0x00}, {0x00,0x00,0x00}, {0xFF,0xFF,0xFF}},
    {{0x00,0xFF,0x00}, {0x00,0xFF,0xFF}, {0x00,0xFF,0xFF}, {0x00,0xFF,0x00}},
    {{0x00,0x00,0xFF}, {0xFF,0x00,0xFF}, {0xFF,0x00,0xFF}, {0x00,0x00,0xFF}}
  };
  uint16_t lut16[4][4][3] = {
    { {0x0000,0x0000,0xFFFF}, {0xFFFF,0x0000,0xFFFF},
      {0xFFFF,0x0000,0xFFFF}, {0x0000,0x0000,0xFFFF} },
    { {0x0000,0xFFFF,0x0000}, {0x0000,0xFFFF,0xFFFF},
      {0x0000,0xFFFF,0xFFFF}, {0x0000,0xFFFF,0x0000} },
    { {0xFFFF,0xFFFF,0xFFFF}, {0x0000,0x0000,0x0000},
      {0x0000,0x0000,0x0000}, {0xFFFF,0xFFFF,0xFFFF} },
    { {0xFFFF,0xFFFF,0x0000}, {0xFFFF,0x0000,0x0000},
      {0xFFFF,0x0000,0x0000}, {0xFFFF,0xFFFF,0x0000} }
  };
  struct mem_allocator allocator;
  char* pixels;
  int x, y, i = 0;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);

  pixels = MEM_ALLOC(&allocator, WIDTH*sizeof_image_format(IMAGE_RGB8)*HEIGHT);
  CHK(pixels != NULL);

  i = 0;
  FOR_EACH(y, 0, HEIGHT) {
  FOR_EACH(x, 0, WIDTH) {
    int j = ((x/32) & 1) + ((y/16)&1) * 2;
    int k = ((x/8) & 1) + ((y/8)&1) * 2;

    ((uint8_t*)pixels)[i++] = lut8[j][k][0];
    ((uint8_t*)pixels)[i++] = lut8[j][k][1];
    ((uint8_t*)pixels)[i++] = lut8[j][k][2];
  }}
  check_image(pixels, WIDTH, HEIGHT, IMAGE_RGB8, &allocator);
  MEM_RM(&allocator, pixels);

  pixels = MEM_ALLOC(&allocator, WIDTH*sizeof_image_format(IMAGE_RGB16)*HEIGHT);
  CHK(pixels != NULL);

  i = 0;
  FOR_EACH(y, 0, HEIGHT) {
  FOR_EACH(x, 0, WIDTH) {
    int j = ((x/32) & 1) + ((y/16)&1) * 2;
    int k = ((x/8) & 1) + ((y/8)&1) * 2;
    ((uint16_t*)pixels)[i++] = lut16[j][k][0];
    ((uint16_t*)pixels)[i++] = lut16[j][k][1];
    ((uint16_t*)pixels)[i++] = lut16[j][k][2];
  }}
  check_image(pixels, WIDTH, HEIGHT, IMAGE_RGB16, &allocator);
  MEM_RM(&allocator, pixels);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
