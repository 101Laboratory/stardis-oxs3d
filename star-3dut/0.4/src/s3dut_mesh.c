/* Copyright (C) 2016, 2017, 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#include "s3dut.h"
#include "s3dut_mesh.h"

#include<rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
release_mesh(ref_T* ref)
{
  struct s3dut_mesh* mesh;
  ASSERT(ref);
  mesh = CONTAINER_OF(ref, struct s3dut_mesh, ref);
  darray_double_release(&mesh->coords);
  darray_size_t_release(&mesh->ids);
  MEM_RM(mesh->allocator, mesh);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s3dut_mesh_ref_get(struct s3dut_mesh* mesh)
{
  if(!mesh) return RES_BAD_ARG;
  ref_get(&mesh->ref);
  return RES_OK;
}

res_T
s3dut_mesh_ref_put(struct s3dut_mesh* mesh)
{
  if(!mesh) return RES_BAD_ARG;
  ref_put(&mesh->ref, release_mesh);
  return RES_OK;
}

res_T
s3dut_mesh_get_data(const struct s3dut_mesh* mesh, struct s3dut_mesh_data* data)
{
  size_t ncoords, nids;
  if(!mesh || !data) return RES_BAD_ARG;

  data->positions = darray_double_cdata_get(&mesh->coords);
  data->indices = darray_size_t_cdata_get(&mesh->ids);

  ncoords = darray_double_size_get(&mesh->coords);
  nids = darray_size_t_size_get(&mesh->ids);
  ASSERT(ncoords % 3 == 0); /* Only 3D coordinates are supported */
  ASSERT(nids % 3 == 0); /* Only triangular primitives are supported */

  data->nvertices = ncoords / 3;
  data->nprimitives = nids / 3;

  return RES_OK;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
res_T
mesh_create
  (struct mem_allocator* allocator,
   const enum s3dut_mesh_type type,
   const size_t nvertices,
   const size_t ntriangles,
   struct s3dut_mesh** out_mesh)
{
  struct s3dut_mesh* mesh = NULL;
  struct mem_allocator* mem_allocator;
  res_T res = RES_OK;

  if(!out_mesh) {
    res = RES_BAD_ARG;
    goto error;
  }

  mem_allocator = allocator ? allocator : &mem_default_allocator;
  mesh = MEM_CALLOC(mem_allocator, 1, sizeof(struct s3dut_mesh));
  if(!mesh) {
    res = RES_MEM_ERR;
    goto error;
  }
  mesh->allocator = mem_allocator;
  mesh->type = type;
  ref_init(&mesh->ref);
  darray_double_init(mem_allocator, &mesh->coords);
  darray_size_t_init(mem_allocator, &mesh->ids);

  res = darray_double_resize(&mesh->coords, nvertices*3/*#coords*/);
  if(res != RES_OK) goto error;
  res = darray_size_t_resize(&mesh->ids, ntriangles*3/*#ids per triangle*/);
  if(res != RES_OK) goto error;

exit:
  if(out_mesh) *out_mesh = mesh;
  return res;
error:
  if(mesh) {
    S3DUT(mesh_ref_put(mesh));
    mesh = NULL;
  }
  goto exit;
}

