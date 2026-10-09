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

#include "scmap.h"
#include <rsys/logger.h>

static void
get_white(const size_t icol, double col[3], void* context)
{
  (void)icol, (void)context;
  CHK(col);
  CHK((intptr_t)context == 0xDECAFBAD);
  col[0] = col[1] = col[2] = 1;
}

static void
get_color(const size_t icol, double col[3], void* context)
{
  const double* color = context;
  (void)icol, (void)context;
  CHK(col);
  col[0] = color[0];
  col[1] = color[1];
  col[2] = color[2];
}

static void
log_stream(const char* msg, void* ctx)
{
  ASSERT(msg);
  (void)msg, (void)ctx;
  printf("%s", msg);
}

int
main(int argc, char** argv)
{
  struct logger logger;
  struct scmap* scmap = NULL;
  struct scmap_palette palette = SCMAP_PALETTE_NULL;
  double color[3];
  (void)argc, (void)argv;

  CHK(scmap_create(NULL, NULL, 0, NULL, &scmap) == RES_BAD_ARG);
  CHK(scmap_create(NULL, NULL, 0, &scmap_palette_accent, NULL) == RES_BAD_ARG);
  CHK(scmap_create(NULL, NULL, 0, &scmap_palette_accent, &scmap) == RES_OK);

  CHK(scmap_ref_get(NULL) == RES_BAD_ARG);
  CHK(scmap_ref_get(scmap) == RES_OK);
  CHK(scmap_ref_put(NULL) == RES_BAD_ARG);
  CHK(scmap_ref_put(scmap) == RES_OK);
  CHK(scmap_ref_put(scmap) == RES_OK);

  CHK(scmap_create(NULL, &mem_default_allocator, 1, &scmap_palette_accent,
    &scmap) == RES_OK);
  CHK(scmap_ref_put(scmap) == RES_OK);

  CHK(logger_init(&mem_default_allocator, &logger) == RES_OK);
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  CHK(scmap_create(&logger, &mem_default_allocator, 0, &scmap_palette_accent,
    &scmap) == RES_OK);
  CHK(scmap_ref_put(scmap) == RES_OK);

  palette.get_color = get_white;
  palette.ncolors = 1 ;
  palette.context = (void*)(intptr_t)0xDECAFBAD;

  CHK(scmap_create(NULL, NULL, 1, &palette, &scmap) == RES_OK);
  CHK(scmap_ref_put(scmap) == RES_OK);

  color[0] = -1;
  color[1] = -1;
  color[2] = -1;
  palette.ncolors = 0;
  CHK(scmap_create(NULL, NULL, 1, &palette, &scmap) == RES_BAD_ARG);
  palette.ncolors = 1;
  palette.get_color = NULL;
  CHK(scmap_create(NULL, NULL, 1, &palette, &scmap) == RES_BAD_ARG);
  palette.get_color = get_color;
  palette.context = color;
  CHK(scmap_create(NULL, NULL, 1, &palette, &scmap) == RES_BAD_ARG);
  color[0] = 0;
  color[1] = 0;
  color[2] = 1.1;
  CHK(scmap_create(NULL, NULL, 1, &palette, &scmap) == RES_BAD_ARG);
  color[2] = 1;
  CHK(scmap_create(NULL, NULL, 1, &palette, &scmap) == RES_OK);
  CHK(scmap_ref_put(scmap) == RES_OK);

  logger_release(&logger);
  CHK(mem_allocated_size() == 0);
  return 0;
}
