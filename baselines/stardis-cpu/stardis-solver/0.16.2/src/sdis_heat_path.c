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

#include "sdis_heat_path.h"

/* Generate the radiative path routines */
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_radiative_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_radiative_Xd.h"

/* Generate the convective path routines */
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_convective_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_convective_Xd.h"

/*******************************************************************************
 * Local functions
 ******************************************************************************/
/* Return the offset into the list of vertices toward the first vertex of the
 * line strip */
static INLINE size_t
line_strip_vertex_offset(const struct sdis_heat_path* path, const size_t istrip)
{
  ASSERT(path);
#ifndef NDEBUG
  {
    size_t nstrips;
    SDIS(heat_path_get_line_strips_count(path, &nstrips));
    ASSERT(istrip < nstrips);
  }
#endif
  return istrip == 0 ? 0 : darray_size_t_cdata_get(&path->breaks)[istrip-1] + 1;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_heat_path_get_status
  (const struct sdis_heat_path* path, enum sdis_heat_path_flag* status)
{
  if(!path || !status) return RES_BAD_ARG;
  *status = path->status;
  return RES_OK;
}

res_T
sdis_heat_path_get_line_strips_count
  (const struct sdis_heat_path* path,
   size_t* nstrips)
{
  if(!path || !nstrips) return RES_BAD_ARG;
  /* #strips == #breaks + 1 */
  *nstrips = darray_size_t_size_get(&path->breaks) + 1;
  return RES_OK;
}

res_T
sdis_heat_path_line_strip_get_vertices_count
  (const struct sdis_heat_path* path,
   const size_t istrip,
   size_t* out_nvertices)
{
  size_t nstrips = 0;
  size_t ivert_begin = 0;
  size_t ivert_end = 0;
  res_T res = RES_OK;

  if(!path || !out_nvertices) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = sdis_heat_path_get_line_strips_count(path, &nstrips);
  if(res != RES_OK) goto error;

  /* Check the indices of the strip */
  if(istrip >= nstrips) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(istrip == 0) { /* First strip */
    ivert_begin = 0;
  } else {
    ivert_begin = line_strip_vertex_offset(path, istrip);
  }

  if(istrip == nstrips-1) { /* Last strip */
    ivert_end = darray_heat_vertex_size_get(&path->vertices);
  } else {
    ivert_end = line_strip_vertex_offset(path, istrip+1);
  }

  ASSERT(ivert_begin <= ivert_end);
  *out_nvertices = ivert_end - ivert_begin;

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_heat_path_line_strip_get_vertex
  (const struct sdis_heat_path* path,
   const size_t istrip,
   const size_t ivert,
   struct sdis_heat_vertex* vertex)
{
  size_t nverts = 0;
  size_t ivert_adjusted = 0;
  res_T res = RES_OK;

  if(!path || !vertex) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* By retrieving the number of vertices, we also check the validity of
   * istrip: the function will return an error if istrip is invalid */
  res = sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts);
  if(res != RES_OK) goto error;

  if(ivert >= nverts) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Compute the index into the overall list of vertices */
  ivert_adjusted = ivert + line_strip_vertex_offset(path, istrip);
  ASSERT(ivert_adjusted < darray_heat_vertex_size_get(&path->vertices));

  /* Fetch the vertex */
  *vertex = darray_heat_vertex_cdata_get(&path->vertices)[ivert_adjusted];

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_heat_path_line_strip_for_each_vertex
  (const struct sdis_heat_path* path,
   const size_t istrip,
   sdis_process_heat_vertex_T func,
   void* context)
{
  const struct sdis_heat_vertex* vertices = NULL;
  size_t ivert = 0;
  size_t offset = 0;
  size_t nverts = 0;
  res_T res = RES_OK;

  if(!path || !func) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = sdis_heat_path_line_strip_get_vertices_count(path, istrip, &nverts);
  if(res != RES_OK) goto error;

  offset = line_strip_vertex_offset(path, istrip);

  vertices = darray_heat_vertex_cdata_get(&path->vertices);
  FOR_EACH(ivert, 0, nverts) {
    res = func(vertices+ivert+offset, context);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}
