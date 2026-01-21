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

#define _POSIX_C_SOURCE 200112L /* nextafter support */

#include "scmap.h"

#include <rsys/double3.h>
#include<rsys/mem_allocator.h>

#include <rsys/rsys_math.h>

static const double colors[] = {
  0.1, 0.1, 0.1,
  0.2, 0.2, 0.2,
  0.3, 0.3, 0.3,
  0.4, 0.4, 0.4,
  0.5, 0.5, 0.5
};
static const size_t ncolors = sizeof(colors)/(sizeof(double)*3);

static void
get_color(const size_t i, double col[3], void* context)
{
  (void)context;
  CHK(col);
  CHK(i < ncolors);
  col[0] = colors[i*3+0];
  col[1] = colors[i*3+1];
  col[2] = colors[i*3+2];
}

int
main(int argc, char** argv)
{
  struct scmap* scmap = NULL;
  struct scmap_palette palette = SCMAP_PALETTE_NULL;
  double col[3];
  (void)argc, (void)argv;

  palette.get_color = get_color;
  palette.ncolors = ncolors;
  CHK(scmap_create(NULL, NULL, 1, &palette, &scmap) == RES_OK);

  CHK(scmap_fetch_color(NULL, 0, SCMAP_FILTER_NEAREST, col) == RES_BAD_ARG);
  CHK(scmap_fetch_color(scmap, -1, SCMAP_FILTER_NEAREST, col) == RES_BAD_ARG);
  CHK(scmap_fetch_color(scmap, 0, -1, col) == RES_BAD_ARG);
  CHK(scmap_fetch_color(scmap, 0, SCMAP_FILTER_NEAREST, NULL) == RES_BAD_ARG);

  CHK(scmap_fetch_color(scmap, 0, SCMAP_FILTER_NEAREST, col) == RES_OK);
  CHK(col[0] == col[1] && col[1] == col[2] && col[2] == 0.1);

  CHK(scmap_fetch_color(scmap, 0.124, SCMAP_FILTER_NEAREST, col) == RES_OK);
  CHK(col[0] == col[1] && col[1] == col[2] && col[2] == 0.1);
  CHK(scmap_fetch_color(scmap, 0.126, SCMAP_FILTER_NEAREST, col) == RES_OK);
  CHK(col[0] == col[1] && col[1] == col[2] && col[2] == 0.2);
  CHK(scmap_fetch_color(scmap, 0.25, SCMAP_FILTER_LINEAR, col) == RES_OK);
  CHK(col[0] == col[1] && col[1] == col[2] && col[2] == 0.2);
  CHK(scmap_fetch_color(scmap, 0.5, SCMAP_FILTER_LINEAR, col) == RES_OK);
  CHK(col[0] == col[1] && col[1] == col[2] && col[2] == 0.3);
  CHK(scmap_fetch_color(scmap, 0.75, SCMAP_FILTER_LINEAR, col) == RES_OK);
  CHK(col[0] == col[1] && col[1] == col[2] && col[2] == 0.4);
  CHK(scmap_fetch_color(scmap, 1, SCMAP_FILTER_LINEAR, col) == RES_OK);
  CHK(col[0] == col[1] && col[1] == col[2] && col[2] == 0.5);

  CHK(scmap_fetch_color(scmap, nextafter(1, 2), SCMAP_FILTER_LINEAR, col)
    == RES_BAD_ARG);

  CHK(scmap_fetch_color(scmap, 0.125, SCMAP_FILTER_LINEAR, col) == RES_OK);
  CHK(col[0] == col[1] && col[1] == col[2] && col[2] == (0.1+0.2)*0.5);
  CHK(scmap_fetch_color(scmap, 0.7, SCMAP_FILTER_LINEAR, col) == RES_OK);
  CHK(col[0] == col[1] && col[1] == col[2] && col[2]);
  CHK(eq_eps(col[0], 0.8 * (0.4 - 0.3) + 0.3, 1.e-8));
  CHK(scmap_fetch_color(scmap, 0.85, SCMAP_FILTER_LINEAR, col) == RES_OK);
  CHK(col[0] == col[1] && col[1] == col[2] && col[2]);
  CHK(eq_eps(col[0], 0.4 * (0.5 - 0.4) + 0.4, 1.e-8));

  CHK(scmap_ref_put(scmap) == RES_OK);

  CHK(mem_allocated_size() == 0);
  return 0;
}

