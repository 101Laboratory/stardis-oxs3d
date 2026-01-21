/* Copyright (C) 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#define MAP_WIDTH 480
#define MAP_HEIGHT 32

#include "scmap.h"

#include <rsys/image.h>
#include<rsys/mem_allocator.h>
#include <rsys/str.h>
#include <rsys/stretchy_array.h>

int
main(int argc, char** argv)
{
  struct str filename;
  FILE* fp = NULL;
  struct scmap* scmap = NULL;
  const struct scmap_palette* palette = NULL;
  double* colors = NULL;
  struct image img;
  size_t x, y;
  size_t i;
  (void)argc, (void)argv;

  if(argc < 2) {
    fprintf(stderr, "Usage: %s PALETTE\n", argv[0]);
    return 1;
  }

  CHK(scmap_get_builtin_palette("bad_name") == NULL);
  CHK((palette = scmap_get_builtin_palette(argv[1])) != NULL);
  CHK(scmap_create(NULL, NULL, 1, palette, &scmap) == RES_OK);
  CHK((colors = sa_add(colors, MAP_WIDTH*3)) != NULL);

  FOR_EACH(i, 0, MAP_WIDTH) {
    const double u = (double)i / (double)(MAP_WIDTH-1);
    CHK(scmap_fetch_color(scmap, u, SCMAP_FILTER_LINEAR, colors+i*3) == RES_OK);
  }

  CHK(image_init(&mem_default_allocator, &img) == RES_OK);
  image_setup(&img, MAP_WIDTH, MAP_HEIGHT,
    sizeof_image_format(IMAGE_RGB8)*MAP_WIDTH, IMAGE_RGB8, NULL);

  FOR_EACH(y, 0, MAP_HEIGHT) {
    char* row = img.pixels + img.pitch * y;
    FOR_EACH(x, 0, MAP_WIDTH) {
      uint8_t* pix = (uint8_t*)(row + x*sizeof_image_format(img.format));
      pix[0] = (uint8_t)(colors[x*3+0] * 255 + 0.5/*round*/);
      pix[1] = (uint8_t)(colors[x*3+1] * 255 + 0.5/*round*/);
      pix[2] = (uint8_t)(colors[x*3+2] * 255 + 0.5/*round*/);
    }
  }
  
  str_init(&mem_default_allocator, &filename);
  CHK(str_set(&filename, argv[1]) == RES_OK);
  CHK(str_append(&filename, ".ppm") == RES_OK);
  CHK((fp = fopen(str_cget(&filename), "w")) != NULL);
  CHK(image_write_ppm_stream(&img, 0, fp) == RES_OK); 
  CHK(fclose(fp) == 0);
  str_release(&filename);

  sa_release(colors);
  CHK(image_release(&img) == RES_OK);
  CHK(scmap_ref_put(scmap) == RES_OK);
  CHK(mem_allocated_size() == 0);
  return 0;
}
