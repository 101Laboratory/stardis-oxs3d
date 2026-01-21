/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_SUVM_BOX_H
#define TEST_SUVM_BOX_H

static const double box_vertices[9/*#vertices*/*3/*#coords per vertex*/] = {
  0.0, 0.0, 0.0,
  1.0, 0.0, 0.0,
  0.0, 1.0, 0.0,
  1.0, 1.0, 0.0,
  0.0, 0.0, 1.0,
  1.0, 0.0, 1.0,
  0.0, 1.0, 1.0,
  1.0, 1.0, 1.0,
  0.5, 0.5, 0.5
};
static const size_t box_nverts = sizeof(box_vertices) / (sizeof(double)*3);

/* The following array lists the indices toward the 3D vertices of each
 * boundary triangle. The index 8 references the vertex at the center of the
 * box.
 *        ,2---,3           ,2----3
 *      ,' | ,'/|         ,'/| \  |        Y
 *    6----7' / |       6' / |  \ |        |
 *    |',  | / ,1       | / ,0---,1        o--X
 *    |  ',|/,'         |/,' | ,'         /
 *    4----5'           4----5'          Z */
static const size_t box_indices[12/*#tetras*/*4/*#indices per tetra*/] = {
  0, 1, 2, 8, 1, 3, 2, 8, /* -Z */
  0, 2, 4, 8, 2, 6, 4, 8, /* -X */
  4, 6, 5, 8, 6, 7, 5, 8, /* +Z */
  3, 5, 7, 8, 5, 3, 1, 8, /* +X */
  2, 7, 6, 8, 7, 2, 3, 8, /* +Y */
  0, 5, 1, 8, 5, 0, 4, 8  /* -Y */
};
static const size_t box_ntetras = sizeof(box_indices) / (sizeof(size_t)*4);

#endif /* TEST_SUVM_BOX_H */
