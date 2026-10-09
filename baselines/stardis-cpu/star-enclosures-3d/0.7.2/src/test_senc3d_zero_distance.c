/* Copyright (C) 2018-2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

/* This test has been created using the sg3_geometry_dump_as_C_code feature
 * of star-geometry. It uses output from test_sg3_cube_on_cube. */

#define _POSIX_C_SOURCE 200112L /* snprintf */

#include "senc3d.h"
#include "test_senc3d_utils.h"

#include <rsys/double3.h>

#include <stdio.h>

/* Tests created using -c option of stardis */

/* zero_1 geometry is made of a big distorded cube, that as a single highest
 * vertex V, and two internal shapes (highly distorded cubes) that joint at the
 * same V vertex. Distance between the 6 components (inside and outside of each
 * shape) is zero. */
#define zero_1_UNSPECIFIED_PROPERTY 4294967295

static const unsigned
zero_1_vertices_count = 22;

static const unsigned
zero_1_triangles_count = 36;

static const double
zero_1_vertices[22][3] = {
   { 10, 10, 0 },
   { 10, 0, 0 },
   { 0, 10, 0 },
   { 0, 0, 0 },
   { 0, 10, 10 },
   { 0, 0, 10 },
   { 10, 0, 10 },
   { 9, 9, 11 },
   { 8, 9, 1 },
   { 4, 7, 1 },
   { 1, 9, 1 },
   { 1, 2, 1 },
   { 1, 9, 9 },
   { 1, 2, 9 },
   { 4, 7, 9 },
   { 7, 5, 1 },
   { 9, 8, 1 },
   { 9, 1, 1 },
   { 2, 1, 1 },
   { 2, 1, 9 },
   { 9, 1, 9 },
   { 7, 4, 9 }
};

static const unsigned
zero_1_triangles[36][3] = {
   { 0, 1, 2 },
   { 1, 3, 2 },
   { 4, 5, 6 },
   { 7, 4, 6 },
   { 2, 3, 4 },
   { 3, 5, 4 },
   { 6, 1, 7 },
   { 1, 0, 7 },
   { 0, 2, 4 },
   { 7, 0, 4 },
   { 5, 3, 1 },
   { 6, 5, 1 },
   { 8, 9, 10 },
   { 9, 11, 10 },
   { 12, 13, 14 },
   { 7, 12, 14 },
   { 10, 11, 12 },
   { 11, 13, 12 },
   { 14, 9, 7 },
   { 9, 8, 7 },
   { 8, 10, 12 },
   { 7, 8, 12 },
   { 13, 11, 9 },
   { 14, 13, 9 },
   { 15, 16, 17 },
   { 18, 15, 17 },
   { 19, 20, 21 },
   { 20, 7, 21 },
   { 18, 17, 20 },
   { 19, 18, 20 },
   { 15, 21, 7 },
   { 16, 15, 7 },
   { 17, 16, 20 },
   { 16, 7, 20 },
   { 18, 19, 15 },
   { 19, 21, 15 }
};

static const unsigned
zero_1_properties[36][3] = {
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY }
};

/* zero_1d is similar as zero_1, with geometry duplicated in mirror along Z axis */
static const unsigned
zero_1d_vertices_count = 44;

static const unsigned
zero_1d_triangles_count = 72;

static const double
zero_1d_vertices[44][3] = {
   { 10, 10, -11 },
   { 10, 0, -11 },
   { 0, 10, -11 },
   { 0, 0, -11 },
   { 0, 10, -1 },
   { 0, 0, -1 },
   { 10, 0, -1 },
   { 9, 9, 0 },
   { 8, 9, -10 },
   { 4, 7, -10 },
   { 1, 9, -10 },
   { 1, 2, -10 },
   { 1, 9, -2 },
   { 1, 2, -2 },
   { 4, 7, -2 },
   { 7, 5, -10 },
   { 9, 8, -10 },
   { 9, 1, -10 },
   { 2, 1, -10 },
   { 2, 1, -2 },
   { 9, 1, -2 },
   { 7, 4, -2 }, 
   { 10, 10, 11 },
   { 10, 0, 11 },
   { 0, 10, 11 },
   { 0, 0, 11 },
   { 0, 10, 1 },
   { 0, 0, 1 },
   { 10, 0, 1 },
   { 9, 9, 0.1 }, /* Vertex #29 == vertex #7 => keep a dummy placeholder here and use #7 instead in triangles */
   { 8, 9, 10 },
   { 4, 7, 10 },
   { 1, 9, 10 },
   { 1, 2, 10 },
   { 1, 9, 2 },
   { 1, 2, 2 },
   { 4, 7, 2 },
   { 7, 5, 10 },
   { 9, 8, 10 },
   { 9, 1, 10 },
   { 2, 1, 10 },
   { 2, 1, 2 },
   { 9, 1, 2 },
   { 7, 4, 2 }
};

static const unsigned
zero_1d_triangles[72][3] = {
   { 0, 1, 2 },
   { 1, 3, 2 },
   { 4, 5, 6 },
   { 7, 4, 6 },
   { 2, 3, 4 },
   { 3, 5, 4 },
   { 6, 1, 7 },
   { 1, 0, 7 },
   { 0, 2, 4 },
   { 7, 0, 4 },
   { 5, 3, 1 },
   { 6, 5, 1 },
   { 8, 9, 10 },
   { 9, 11, 10 },
   { 12, 13, 14 },
   { 7, 12, 14 },
   { 10, 11, 12 },
   { 11, 13, 12 },
   { 14, 9, 7 },
   { 9, 8, 7 },
   { 8, 10, 12 },
   { 7, 8, 12 },
   { 13, 11, 9 },
   { 14, 13, 9 },
   { 15, 16, 17 },
   { 18, 15, 17 },
   { 19, 20, 21 },
   { 20, 7, 21 },
   { 18, 17, 20 },
   { 19, 18, 20 },
   { 15, 21, 7 },
   { 16, 15, 7 },
   { 17, 16, 20 },
   { 16, 7, 20 },
   { 18, 19, 15 },
   { 19, 21, 15 },
   { 22, 23, 24 },
   { 23, 25, 24 },
   { 26, 27, 28 },
   { 7, 26, 28 },
   { 24, 25, 26 },
   { 25, 27, 26 },
   { 28, 23, 7 },
   { 23, 22, 7 },
   { 22, 24, 26 },
   { 7, 22, 26 },
   { 27, 25, 23 },
   { 28, 27, 23 },
   { 30, 31, 32 },
   { 31, 33, 32 },
   { 34, 35, 36 },
   { 7, 34, 36 },
   { 32, 33, 34 },
   { 33, 35, 34 },
   { 36, 31, 7 },
   { 31, 30, 7 },
   { 30, 32, 34 },
   { 7, 30, 34 },
   { 35, 33, 31 },
   { 36, 35, 31 },
   { 37, 38, 39 },
   { 40, 37, 39 },
   { 41, 42, 43 },
   { 42, 7, 43 },
   { 40, 39, 42 },
   { 41, 40, 42 },
   { 37, 43, 7 },
   { 38, 37, 7 },
   { 39, 38, 42 },
   { 38, 7, 42 },
   { 40, 41, 37 },
   { 41, 43, 37 }
};

static const unsigned
zero_1d_properties[72][3] = {
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 0, zero_1_UNSPECIFIED_PROPERTY, 2 },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_1_UNSPECIFIED_PROPERTY },

   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { zero_1_UNSPECIFIED_PROPERTY, 0, 2 },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_1_UNSPECIFIED_PROPERTY }
};

/* zero_1D is similar as zero_1d, with a tiny distance between mirrored
 * geometries */
static const double
zero_1D_vertices[44][3] = {
   { 10, 10, -11 },
   { 10, 0, -11 },
   { 0, 10, -11 },
   { 0, 0, -11 },
   { 0, 10, -1 },
   { 0, 0, -1 },
   { 10, 0, -1 },
   { 9, 9, 0 },
   { 8, 9, -10 },
   { 4, 7, -10 },
   { 1, 9, -10 },
   { 1, 2, -10 },
   { 1, 9, -2 },
   { 1, 2, -2 },
   { 4, 7, -2 },
   { 7, 5, -10 },
   { 9, 8, -10 },
   { 9, 1, -10 },
   { 2, 1, -10 },
   { 2, 1, -2 },
   { 9, 1, -2 },
   { 7, 4, -2 },
   { 10, 10, 11 },
   { 10, 0, 11 },
   { 0, 10, 11 },
   { 0, 0, 11 },
   { 0, 10, 1 },
   { 0, 0, 1 },
   { 10, 0, 1 },
   { 9, 9, 1e-30 },
   { 8, 9, 10 },
   { 4, 7, 10 },
   { 1, 9, 10 },
   { 1, 2, 10 },
   { 1, 9, 2 },
   { 1, 2, 2 },
   { 4, 7, 2 },
   { 7, 5, 10 },
   { 9, 8, 10 },
   { 9, 1, 10 },
   { 2, 1, 10 },
   { 2, 1, 2 },
   { 9, 1, 2 },
   { 7, 4, 2 }
};

#define zero_1D_triangles zero_1d_triangles

#define zero_1D_properties zero_1d_properties
//static unsigned zero_1D_triangles_count = zero_1d_triangles_count;
//static unsigned zero_1D_vertices_count = zero_1d_vertices_count;
static unsigned zero_1D_triangles_count = 72;
static unsigned zero_1D_vertices_count = 44;

/* zero_2 geometry is made of 5 nested distorded cubes of decreasing sizes, that
 * all join at a single vertex V (the highest vertex in the model). Distance between
 * the 10 components (inside and outside of each shape) is zero. */
#define zero_2_UNSPECIFIED_PROPERTY 4294967295

static const unsigned
zero_2_vertices_count = 36;

static const unsigned
zero_2_triangles_count = 60;

static const double
zero_2_vertices[36][3] = {
   { 0, 10, 0 },
   { 0, 0, 0 },
   { 0, 10, 10 },
   { 0, 0, 10 },
   { 10, 0, 10 },
   { 10, 0, 0 },
   { 9, 9, 11 },
   { 10, 10, 0 },
   { 1, 9, 1 },
   { 1, 1, 1 },
   { 1, 9, 9 },
   { 1, 1, 9 },
   { 9, 1, 9 },
   { 9, 1, 1 },
   { 9, 9, 1 },
   { 2, 8, 2 },
   { 2, 2, 2 },
   { 2, 8, 8 },
   { 2, 2, 8 },
   { 8, 2, 8 },
   { 8, 2, 2 },
   { 8, 8, 2 },
   { 3, 7, 3 },
   { 3, 3, 3 },
   { 3, 7, 7 },
   { 3, 3, 7 },
   { 7, 3, 7 },
   { 7, 3, 3 },
   { 7, 7, 3 },
   { 4, 6, 4 },
   { 4, 4, 4 },
   { 4, 6, 6 },
   { 4, 4, 6 },
   { 6, 4, 6 },
   { 6, 4, 4 },
   { 6, 6, 4 }
};

static const unsigned
zero_2_triangles[60][3] = {
   { 0, 1, 2 },
   { 1, 3, 2 },
   { 4, 5, 6 },
   { 5, 7, 6 },
   { 2, 3, 4 },
   { 6, 2, 4 },
   { 5, 1, 0 },
   { 7, 5, 0 },
   { 3, 1, 5 },
   { 4, 3, 5 },
   { 7, 0, 2 },
   { 6, 7, 2 },
   { 8, 9, 10 },
   { 9, 11, 10 },
   { 12, 13, 6 },
   { 13, 14, 6 },
   { 10, 11, 12 },
   { 6, 10, 12 },
   { 13, 9, 8 },
   { 14, 13, 8 },
   { 11, 9, 13 },
   { 12, 11, 13 },
   { 14, 8, 10 },
   { 6, 14, 10 },
   { 15, 16, 17 },
   { 16, 18, 17 },
   { 19, 20, 6 },
   { 20, 21, 6 },
   { 17, 18, 19 },
   { 6, 17, 19 },
   { 20, 16, 15 },
   { 21, 20, 15 },
   { 18, 16, 20 },
   { 19, 18, 20 },
   { 21, 15, 17 },
   { 6, 21, 17 },
   { 22, 23, 24 },
   { 23, 25, 24 },
   { 26, 27, 6 },
   { 27, 28, 6 },
   { 24, 25, 26 },
   { 6, 24, 26 },
   { 27, 23, 22 },
   { 28, 27, 22 },
   { 25, 23, 27 },
   { 26, 25, 27 },
   { 28, 22, 24 },
   { 6, 28, 24 },
   { 29, 30, 31 },
   { 30, 32, 31 },
   { 33, 34, 6 },
   { 34, 35, 6 },
   { 31, 32, 33 },
   { 6, 31, 33 },
   { 34, 30, 29 },
   { 35, 34, 29 },
   { 32, 30, 34 },
   { 33, 32, 34 },
   { 35, 29, 31 },
   { 6, 35, 31 }
};

static const unsigned
zero_2_properties[60][3] = {
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY }
};

/* zero_2d is similar as zero_2, with geometry duplicated in mirror along Z axis */
static const unsigned
zero_2d_vertices_count = 72;

static const unsigned
zero_2d_triangles_count = 120;

static const double
zero_2d_vertices[72][3] = {
   { 0, 10, -11 },
   { 0, 0, -11 },
   { 0, 10, -1 },
   { 0, 0, -1 },
   { 10, 0, -1 },
   { 10, 0, -11 },
   { 9, 9, 0 },
   { 10, 10, -11 },
   { 1, 9, -10 },
   { 1, 1, -10 },
   { 1, 9, -2 },
   { 1, 1, -2 },
   { 9, 1, -2 },
   { 9, 1, -10 },
   { 9, 9, -10 },
   { 2, 8, -9 },
   { 2, 2, -9 },
   { 2, 8, -3 },
   { 2, 2, -3 },
   { 8, 2, -3 },
   { 8, 2, -9 },
   { 8, 8, -9 },
   { 3, 7, -8 },
   { 3, 3, -8 },
   { 3, 7, -4 },
   { 3, 3, -4 },
   { 7, 3, -4 },
   { 7, 3, -8 },
   { 7, 7, -8 },
   { 4, 6, -7 },
   { 4, 4, -7 },
   { 4, 6, -5 },
   { 4, 4, -5 },
   { 6, 4, -5 },
   { 6, 4, -7 },
   { 6, 6, -7 },
   { 0, 10, 11 },
   { 0, 0, 11 },
   { 0, 10, 1 },
   { 0, 0, 1 },
   { 10, 0, 1 },
   { 10, 0, 11 },
   { 9, 9, 0.1 }, /* Vertex #42 == vertex #6 => keep a dummy placeholder here and use #6 instead in triangles */
   { 10, 10, 11 },
   { 1, 9, 10 },
   { 1, 1, 10 },
   { 1, 9, 2 },
   { 1, 1, 2 },
   { 9, 1, 2 },
   { 9, 1, 10 },
   { 9, 9, 10 },
   { 2, 8, 9 },
   { 2, 2, 9 },
   { 2, 8, 3 },
   { 2, 2, 3 },
   { 8, 2, 3 },
   { 8, 2, 9 },
   { 8, 8, 9 },
   { 3, 7, 8 },
   { 3, 3, 8 },
   { 3, 7, 4 },
   { 3, 3, 4 },
   { 7, 3, 4 },
   { 7, 3, 8 },
   { 7, 7, 8 },
   { 4, 6, 7 },
   { 4, 4, 7 },
   { 4, 6, 5 },
   { 4, 4, 5 },
   { 6, 4, 5 },
   { 6, 4, 7 },
   { 6, 6, 7 }
};

static const unsigned
zero_2d_triangles[120][3] = {
   { 0, 1, 2 },
   { 1, 3, 2 },
   { 4, 5, 6 },
   { 5, 7, 6 },
   { 2, 3, 4 },
   { 6, 2, 4 },
   { 5, 1, 0 },
   { 7, 5, 0 },
   { 3, 1, 5 },
   { 4, 3, 5 },
   { 7, 0, 2 },
   { 6, 7, 2 },
   { 8, 9, 10 },
   { 9, 11, 10 },
   { 12, 13, 6 },
   { 13, 14, 6 },
   { 10, 11, 12 },
   { 6, 10, 12 },
   { 13, 9, 8 },
   { 14, 13, 8 },
   { 11, 9, 13 },
   { 12, 11, 13 },
   { 14, 8, 10 },
   { 6, 14, 10 },
   { 15, 16, 17 },
   { 16, 18, 17 },
   { 19, 20, 6 },
   { 20, 21, 6 },
   { 17, 18, 19 },
   { 6, 17, 19 },
   { 20, 16, 15 },
   { 21, 20, 15 },
   { 18, 16, 20 },
   { 19, 18, 20 },
   { 21, 15, 17 },
   { 6, 21, 17 },
   { 22, 23, 24 },
   { 23, 25, 24 },
   { 26, 27, 6 },
   { 27, 28, 6 },
   { 24, 25, 26 },
   { 6, 24, 26 },
   { 27, 23, 22 },
   { 28, 27, 22 },
   { 25, 23, 27 },
   { 26, 25, 27 },
   { 28, 22, 24 },
   { 6, 28, 24 },
   { 29, 30, 31 },
   { 30, 32, 31 },
   { 33, 34, 6 },
   { 34, 35, 6 },
   { 31, 32, 33 },
   { 6, 31, 33 },
   { 34, 30, 29 },
   { 35, 34, 29 },
   { 32, 30, 34 },
   { 33, 32, 34 },
   { 35, 29, 31 },
   { 6, 35, 31 },
   { 36, 37, 38 },
   { 37, 39, 38 },
   { 40, 41, 6 },
   { 41, 43, 6 },
   { 38, 39, 40 },
   { 6, 38, 40 },
   { 41, 37, 36 },
   { 43, 41, 36 },
   { 39, 37, 41 },
   { 40, 39, 41 },
   { 43, 36, 38 },
   { 6, 43, 38 },
   { 44, 45, 46 },
   { 45, 47, 46 },
   { 48, 49, 6 },
   { 49, 50, 6 },
   { 46, 47, 48 },
   { 6, 46, 48 },
   { 49, 45, 44 },
   { 50, 49, 44 },
   { 47, 45, 49 },
   { 48, 47, 49 },
   { 50, 44, 46 },
   { 6, 50, 46 },
   { 51, 52, 53 },
   { 52, 54, 53 },
   { 55, 56, 6 },
   { 56, 57, 6 },
   { 53, 54, 55 },
   { 6, 53, 55 },
   { 56, 52, 51 },
   { 57, 56, 51 },
   { 54, 52, 56 },
   { 55, 54, 56 },
   { 57, 51, 53 },
   { 6, 57, 53 },
   { 58, 59, 60 },
   { 59, 61, 60 },
   { 62, 63, 6 },
   { 63, 64, 6 },
   { 60, 61, 62 },
   { 6, 60, 62 },
   { 63, 59, 58 },
   { 64, 63, 58 },
   { 61, 59, 63 },
   { 62, 61, 63 },
   { 64, 58, 60 },
   { 6, 64, 60 },
   { 65, 66, 67 },
   { 66, 68, 67 },
   { 69, 70, 6 },
   { 70, 71, 6 },
   { 67, 68, 69 },
   { 6, 67, 69 },
   { 70, 66, 65 },
   { 71, 70, 65 },
   { 68, 66, 70 },
   { 69, 68, 70 },
   { 71, 65, 67 },
   { 6, 71, 67 }
};

static const unsigned
zero_2d_properties[120][3] = {
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 0, zero_2_UNSPECIFIED_PROPERTY, 5 },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 0, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 4, 3, zero_2_UNSPECIFIED_PROPERTY },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { zero_2_UNSPECIFIED_PROPERTY, 0, 5 },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 0, 1, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 1, 2, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 2, 3, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY },
   { 3, 4, zero_2_UNSPECIFIED_PROPERTY }
};

/* zero_2D is similar as zero_2d, with a tiny distance between mirrored
 * geometries */
static const double
zero_2D_vertices[72][3] = {
   { 0, 10, -11 },
   { 0, 0, -11 },
   { 0, 10, -1 },
   { 0, 0, -1 },
   { 10, 0, -1 },
   { 10, 0, -11 },
   { 9, 9, 0 },
   { 10, 10, -11 },
   { 1, 9, -10 },
   { 1, 1, -10 },
   { 1, 9, -2 },
   { 1, 1, -2 },
   { 9, 1, -2 },
   { 9, 1, -10 },
   { 9, 9, -10 },
   { 2, 8, -9 },
   { 2, 2, -9 },
   { 2, 8, -3 },
   { 2, 2, -3 },
   { 8, 2, -3 },
   { 8, 2, -9 },
   { 8, 8, -9 },
   { 3, 7, -8 },
   { 3, 3, -8 },
   { 3, 7, -4 },
   { 3, 3, -4 },
   { 7, 3, -4 },
   { 7, 3, -8 },
   { 7, 7, -8 },
   { 4, 6, -7 },
   { 4, 4, -7 },
   { 4, 6, -5 },
   { 4, 4, -5 },
   { 6, 4, -5 },
   { 6, 4, -7 },
   { 6, 6, -7 },
   { 0, 10, 11 },
   { 0, 0, 11 },
   { 0, 10, 1 },
   { 0, 0, 1 },
   { 10, 0, 1 },
   { 10, 0, 11 },
   { 9, 9, 1e-30 },
   { 10, 10, 11 },
   { 1, 9, 10 },
   { 1, 1, 10 },
   { 1, 9, 2 },
   { 1, 1, 2 },
   { 9, 1, 2 },
   { 9, 1, 10 },
   { 9, 9, 10 },
   { 2, 8, 9 },
   { 2, 2, 9 },
   { 2, 8, 3 },
   { 2, 2, 3 },
   { 8, 2, 3 },
   { 8, 2, 9 },
   { 8, 8, 9 },
   { 3, 7, 8 },
   { 3, 3, 8 },
   { 3, 7, 4 },
   { 3, 3, 4 },
   { 7, 3, 4 },
   { 7, 3, 8 },
   { 7, 7, 8 },
   { 4, 6, 7 },
   { 4, 4, 7 },
   { 4, 6, 5 },
   { 4, 4, 5 },
   { 6, 4, 5 },
   { 6, 4, 7 },
   { 6, 6, 7 }
};

#define zero_2D_triangles zero_2d_triangles

#define zero_2D_properties zero_2d_properties
//static unsigned zero_2D_triangles_count = zero_2d_triangles_count;
//static unsigned zero_2D_vertices_count = zero_2d_vertices_count;
static unsigned zero_2D_triangles_count = 120;
static unsigned zero_2D_vertices_count = 72;


/* Enclosures' order is consistent accross runs as they are sorted by triangle
 * sides, thus allowing to expect a given order */
struct expected {
  unsigned enclosure_count; /* max 16 */
  double volume[16]; /* the volume of the expected enclosures */
  unsigned medium[16]; /* the single medium of the expected enclosures */
};

static int
test
  (const double* positions,
   const unsigned* indices,
   const unsigned* properties,
   const unsigned triangles_count,
   const unsigned vertices_count,
   const struct expected* expected)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct context ctx = CONTEXT_NULL__;
  unsigned count, e, n;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  FOR_EACH(n, 0, 2) {
    /* Create a scene with 4 enclosures joining at a single vertex. */
    ctx.positions = positions;
    ctx.indices = indices;
    ctx.properties = properties;

    /* If n==1, same model with symmetry / origin
     * (need to reverse vertices' order to keep same inside/outside) */
    ctx.scale = (n == 1) ? -1 : +1;
    ctx.reverse_vrtx = (n == 1) ? 1 : 0;

    OK(senc3d_scene_create(dev,
      SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_OUTSIDE,
      triangles_count, get_indices, get_media_from_properties,
      vertices_count, get_position, &ctx, &scn));

    OK(senc3d_scene_get_vertices_count(scn, &count));
    CHK(count == vertices_count);

    OK(senc3d_scene_get_triangles_count(scn, &count));
    CHK(count == triangles_count);

    OK(senc3d_scene_get_enclosure_count(scn, &count));
    CHK(count == expected->enclosure_count);

    FOR_EACH(e, 0, count) {
      struct senc3d_enclosure* enclosure;
      struct senc3d_enclosure_header header;
      unsigned m;
      char name[128]; (void)name;
      OK(senc3d_scene_get_enclosure(scn, e, &enclosure));
      OK(senc3d_enclosure_get_header(enclosure, &header));
      CHK(header.enclosed_media_count == 1);
      OK(senc3d_enclosure_get_medium(enclosure, 0, &m));
      CHK(m == expected->medium[e]);
      CHK(eq_eps(header.volume, expected->volume[e], 0.01));
      OK(senc3d_enclosure_ref_put(enclosure));
#ifdef DUMP_ENCLOSURES
      snprintf(name, sizeof(name), "test_zero_distance_%u.obj", e);
      dump_enclosure(scn, e, name);
#endif
    }

    OK(senc3d_scene_ref_put(scn));
  }

  OK(senc3d_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

int
main(int argc, char** argv)
{
  struct expected expected;
  (void)argc, (void)argv;

  expected.enclosure_count = 4;
  expected.medium[0] = SENC3D_UNSPECIFIED_MEDIUM;
  expected.volume[0] = -966.67;
  expected.medium[1] = 0;
  expected.volume[1] = 647.33;
  expected.medium[2] = 1;
  expected.volume[2] = 150;
  expected.medium[3] = 1;
  expected.volume[3] = 169.33;
  test((double *)zero_1_vertices, (unsigned*)zero_1_triangles,
    (unsigned *)zero_1_properties, zero_1_triangles_count,
    zero_1_vertices_count, &expected);

  expected.enclosure_count = 7;
  expected.medium[0] = SENC3D_UNSPECIFIED_MEDIUM;
  expected.volume[0] = -1933.33;
  expected.medium[1] = 0;
  expected.volume[1] = 647.33;
  expected.medium[2] = 1;
  expected.volume[2] = 150;
  expected.medium[3] = 1;
  expected.volume[3] = 169.33;
  expected.medium[4] = 0;
  expected.volume[4] = 647.33;
  expected.medium[5] = 1;
  expected.volume[5] = 150;
  expected.medium[6] = 1;
  expected.volume[6] = 169.33;
  test((double *)zero_1d_vertices, (unsigned*)zero_1d_triangles,
    (unsigned *)zero_1d_properties, zero_1d_triangles_count,
    zero_1d_vertices_count, &expected);

  test((double *)zero_1D_vertices, (unsigned*)zero_1D_triangles,
    (unsigned *)zero_1D_properties, zero_1D_triangles_count,
    zero_1D_vertices_count, &expected);

  expected.enclosure_count = 6;
  expected.medium[0] = SENC3D_UNSPECIFIED_MEDIUM;
  expected.volume[0] = -966.67;
  expected.medium[1] = 0;
  expected.volume[1] = 433.33;
  expected.medium[2] = 1;
  expected.volume[2] = 281.33;
  expected.medium[3] = 2;
  expected.volume[3] = 161.33;
  expected.medium[4] = 3;
  expected.volume[4] = 73.33;
  expected.medium[5] = 4;
  expected.volume[5] = 17.33;
  test((double*)zero_2_vertices, (unsigned*)zero_2_triangles,
    (unsigned*)zero_2_properties, zero_2_triangles_count,
    zero_2_vertices_count, &expected);

  expected.enclosure_count = 11;
  expected.medium[0] = SENC3D_UNSPECIFIED_MEDIUM;
  expected.volume[0] = -1933.33;
  expected.medium[1] = 0;
  expected.volume[1] = 433.33;
  expected.medium[2] = 1;
  expected.volume[2] = 281.33;
  expected.medium[3] = 2;
  expected.volume[3] = 161.33;
  expected.medium[4] = 3;
  expected.volume[4] = 73.33;
  expected.medium[5] = 4;
  expected.volume[5] = 17.33;
  expected.medium[6] = 0;
  expected.volume[6] = 433.33;
  expected.medium[7] = 1;
  expected.volume[7] = 281.33;
  expected.medium[8] = 2;
  expected.volume[8] = 161.33;
  expected.medium[9] = 3;
  expected.volume[9] = 73.33;
  expected.medium[10] = 4;
  expected.volume[10] = 17.33;
  test((double*)zero_2d_vertices, (unsigned*)zero_2d_triangles,
    (unsigned*)zero_2d_properties, zero_2d_triangles_count,
    zero_2d_vertices_count, &expected);

  test((double*)zero_2D_vertices, (unsigned*)zero_2D_triangles,
    (unsigned*)zero_2D_properties, zero_2D_triangles_count,
    zero_2D_vertices_count, &expected);

  return 0;
}
