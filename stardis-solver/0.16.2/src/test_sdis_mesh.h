/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#ifndef TEST_SDIS_MESH_H
#define TEST_SDIS_MESH_H

#include <rsys/rsys.h>
#include <rsys/stretchy_array.h>

struct mesh {
  double* positions; /* List of double 3 */
  size_t* indices; /* List of size_t 3 */
};
#define MESH_NULL__ {NULL, NULL}
static const struct mesh MESH_NULL = MESH_NULL__;

static INLINE void
mesh_init(struct mesh* mesh)
{
  CHK(mesh);
  *mesh = MESH_NULL;
}

static INLINE void
mesh_release(struct mesh* mesh)
{
  CHK(mesh);
  sa_release(mesh->positions);
  sa_release(mesh->indices);
}

/* Number of vertices */
static INLINE size_t
mesh_nvertices(const struct mesh* mesh)
{
  CHK(mesh);
  return sa_size(mesh->positions) / 3/* #coords per vertex */;
}

static INLINE size_t
mesh_2d_nvertices(const struct mesh* mesh)
{
  CHK(mesh);
  return sa_size(mesh->positions) / 2/* #coords per vertex */;
}

/* Number of triangles */
static INLINE size_t
mesh_ntriangles(const struct mesh* mesh)
{
  CHK(mesh);
  return sa_size(mesh->indices) / 3/* #indices per triangle */;
}

static INLINE size_t
mesh_2d_nsegments(const struct mesh* mesh)
{
  CHK(mesh);
  return sa_size(mesh->indices) / 2/* #indices per segment */;
}

static INLINE void
mesh_append
  (struct mesh* mesh,
   const double* in_positions,
   const size_t in_nvertices,
   const size_t* in_indices,
   const size_t in_ntriangles,
   const double in_translate[3]) /* May be NULL */
{
  double translate[3] = {0, 0, 0};
  double* positions = NULL;
  size_t* indices = NULL;
  size_t ivert = 0;
  size_t i = 0;
  CHK(mesh != NULL);

  ivert = mesh_nvertices(mesh);
  positions = sa_add(mesh->positions, in_nvertices*3);
  indices = sa_add(mesh->indices, in_ntriangles*3);

  if(in_translate) {
    translate[0] = in_translate[0];
    translate[1] = in_translate[1];
    translate[2] = in_translate[2];
  }

  FOR_EACH(i, 0, in_nvertices) {
    positions[i*3 + 0] = in_positions[i*3 + 0] + translate[0];
    positions[i*3 + 1] = in_positions[i*3 + 1] + translate[1];
    positions[i*3 + 2] = in_positions[i*3 + 2] + translate[2];
  }

  FOR_EACH(i, 0, in_ntriangles) {
    indices[i*3 + 0] = in_indices[i*3 + 0] + ivert;
    indices[i*3 + 1] = in_indices[i*3 + 1] + ivert;
    indices[i*3 + 2] = in_indices[i*3 + 2] + ivert;
  }
}

static INLINE void
mesh_2d_append
  (struct mesh* mesh,
   const double* in_positions,
   const size_t in_nvertices,
   const size_t* in_indices,
   const size_t in_nsegments,
   const double in_translate[2]) /* May be NULL */
{
  double translate[2] = {0, 0};
  double* positions = NULL;
  size_t* indices = NULL;
  size_t ivert = 0;
  size_t i = 0;
  CHK(mesh != NULL);

  ivert = mesh_2d_nvertices(mesh);
  positions = sa_add(mesh->positions, in_nvertices*2);
  indices = sa_add(mesh->indices, in_nsegments*2);

  if(in_translate) {
    translate[0] = in_translate[0];
    translate[1] = in_translate[1];
  }

  FOR_EACH(i, 0, in_nvertices) {
    positions[i*2 + 0] = in_positions[i*2 + 0] + translate[0];
    positions[i*2 + 1] = in_positions[i*2 + 1] + translate[1];
  }

  FOR_EACH(i, 0, in_nsegments) {
    indices[i*2 + 0] = in_indices[i*2 + 0] + ivert;
    indices[i*2 + 1] = in_indices[i*2 + 1] + ivert;
  }
}

static INLINE void
mesh_dump(const struct mesh* mesh, FILE* stream)
{
  size_t i, n;
  CHK(mesh != NULL);

  n = mesh_nvertices(mesh);
  FOR_EACH(i, 0, n) {
    fprintf(stream, "v %g %g %g\n", SPLIT3(mesh->positions+i*3));
  }

  n = mesh_ntriangles(mesh);
  FOR_EACH(i, 0, n) {
    fprintf(stream, "f %lu %lu %lu\n",
      (unsigned long)(mesh->indices[i*3+0] + 1),
      (unsigned long)(mesh->indices[i*3+1] + 1),
      (unsigned long)(mesh->indices[i*3+2] + 1));
  }
  fflush(stream);
}

static INLINE void
mesh_2d_dump(const struct mesh* mesh, FILE* stream)
{
  size_t i, n;
  CHK(mesh != NULL);

  n = mesh_2d_nvertices(mesh);
  FOR_EACH(i, 0, n) {
    fprintf(stream, "v %g %g\n", SPLIT2(mesh->positions+i*2));
  }

  n = mesh_2d_nsegments(mesh);
  FOR_EACH(i, 0, n) {
    fprintf(stream, "l %lu %lu\n",
      (unsigned long)(mesh->indices[i*2+0] + 1),
      (unsigned long)(mesh->indices[i*2+1] + 1));
  }
  fflush(stream);
}

#endif /* TEST_SDIS_MESH_H */
