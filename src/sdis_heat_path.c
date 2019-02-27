/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

/* Generate the radiative paths */
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_radiative_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_radiative_Xd.h"

/* Generate the convective paths */
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_convective_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_convective_Xd.h"

/* Generate the conductive paths */
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_conductive_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_conductive_Xd.h"

/* Generate the boundary paths */
#define SDIS_XD_DIMENSION 2
#include "sdis_heat_path_boundary_Xd.h"
#define SDIS_XD_DIMENSION 3
#include "sdis_heat_path_boundary_Xd.h"

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_heat_path_get_vertices_count
  (const struct sdis_heat_path* path, size_t* nvertices)
{
  if(!path || !nvertices) return RES_BAD_ARG;
  *nvertices = darray_heat_vertex_size_get(&path->vertices);
  return RES_OK;
}

res_T
sdis_heat_path_get_status
  (const struct sdis_heat_path* path, enum sdis_heat_path_flag* status)
{
  if(!path || !status) return RES_BAD_ARG;
  *status = path->status;
  return RES_OK;
}

res_T
sdis_heat_path_get_vertex
  (const struct sdis_heat_path* path,
   const size_t ivertex,
   struct sdis_heat_vertex* vertex)
{
  if(!path || !vertex
  || ivertex >= darray_heat_vertex_size_get(&path->vertices)) {
    return RES_BAD_ARG;
  }

  *vertex = darray_heat_vertex_cdata_get(&path->vertices)[ivertex];
  return RES_OK;
}

res_T
sdis_heat_path_for_each_vertex
  (const struct sdis_heat_path* path,
   sdis_process_heat_vertex_T func,
   void* context)
{
  const struct sdis_heat_vertex* vertices;
  size_t i, n;
  res_T res = RES_OK;

  if(!path || !func) {
    res = RES_BAD_ARG;
    goto error;
  }

  SDIS(heat_path_get_vertices_count(path, &n));
  vertices = darray_heat_vertex_cdata_get(&path->vertices);
  FOR_EACH(i, 0, n) {
    res = func(vertices+i, context);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

