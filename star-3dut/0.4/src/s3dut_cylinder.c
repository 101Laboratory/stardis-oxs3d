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

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static double*
setup_cylinder_coords
  (double* coords,
   const double radius,
   const double z_bottom,
   const double z_top,
   const unsigned nslices,
   const unsigned nstacks,
   const int close_bottom,
   const int close_top)
{
  double step_theta;
  double step_z;
  const double height = z_top - z_bottom;
  size_t itheta, istack;
  size_t i = 0;
  ASSERT(coords && radius > 0 && height > 0 && nslices >= 3 && nstacks >= 1);

  /* Contour vertices */
  step_theta = 2*PI / (double)nslices;
  step_z = height / (double)nstacks;
  FOR_EACH(itheta, 0, nslices) {
    const double theta = (double)itheta * step_theta;
    const double x = cos(theta);
    const double y = sin(theta);
    double z = z_bottom;

    FOR_EACH(istack, 0, nstacks+1) {
      coords[i++] = x*radius;
      coords[i++] = y*radius;
      coords[i++] = z;
      z = (istack==nstacks) ? z_top : z+step_z; /* No rounding error! */
    }
  }

  /* Bottom polar vertex */
  if(close_bottom) {
    coords[i++] = 0;
    coords[i++] = 0;
    coords[i++] = z_bottom;
  }

  /* Top polar vertex */
  if(close_top) {
    coords[i++] = 0;
    coords[i++] = 0;
    coords[i++] = z_top;
  }
  return coords + i;
}

static size_t*
setup_cylinder_indices
  (size_t* ids,
   const size_t offset,
   const unsigned nslices,
   const unsigned nstacks,
   const int close_bottom,
   const int close_top,
   const int cw_out)
{
  size_t islice;
  size_t istack;
  size_t ibottom;
  size_t itop;
  size_t i = 0;
  ASSERT(ids && nslices && nstacks);

  FOR_EACH(islice, 0, nslices) {
    const size_t islice0 = offset + islice * (nstacks+1);
    const size_t islice1 = offset + ((islice+1)%nslices) * (nstacks+1);
    FOR_EACH(istack, 0, nstacks) {
      const size_t istack0 = istack + 0;
      const size_t istack1 = istack + 1;

      ids[i] = islice0 + istack0;
      ids[cw_out?i+1:i+2] = islice0 + istack1;
      ids[cw_out?i+2:i+1] = islice1 + istack0;
      i += 3;

      ids[i] = islice1 + istack0;
      ids[cw_out?i+1:i+2] = islice0 + istack1;
      ids[cw_out?i+2:i+1] = islice1 + istack1;
      i += 3;
    }
  }

  if(close_bottom) {
    ibottom = nslices * (nstacks+1);
    FOR_EACH(islice, 0, nslices) {
      ids[i] = offset + ibottom;
      ids[cw_out?i+1:i+2] = offset + islice * (nstacks+1);
      ids[cw_out?i+2:i+1] = offset + ((islice+1)%nslices) * (nstacks+1);
      i += 3;
    }
  }

  if(close_top) {
    itop = (close_bottom) ? nslices * (nstacks+1) + 1 : nslices * (nstacks+1);
    FOR_EACH(islice, 0, nslices) {
      ids[i] = offset + itop;
      ids[cw_out?i+1:i+2] = offset + ((islice+1)%nslices) * (nstacks+1) + nstacks;
      ids[cw_out?i+2:i+1] = offset + islice * (nstacks+1) + nstacks;
      i += 3;
    }
  }
  return ids + i;
}

static size_t*
close_wall
  (size_t* ids,
   const size_t fst_id_out,
   const size_t fst_id_in,
   const unsigned nslices,
   const unsigned nstacks,
   const int bottom)
{
  size_t islice;
  size_t i = 0;
  ASSERT(ids && nslices >= 3 && nstacks >= 1);

  FOR_EACH(islice, 0, nslices) {
    ids[i] = fst_id_out + islice * (nstacks+1);
    ids[bottom?i+1:i+2] = fst_id_in + ((islice+1) % nslices) * (nstacks+1);
    ids[bottom?i+2:i+1] = fst_id_in + islice * (nstacks+1);
    i += 3;

    ids[i] = fst_id_out + islice * (nstacks+1);
    ids[bottom?i+1:i+2] = fst_id_out + ((islice+1) % nslices) * (nstacks+1);
    ids[bottom?i+2:i+1] = fst_id_in + ((islice+1) % nslices) * (nstacks+1);
    i += 3;
  }
  return ids + i;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s3dut_create_cylinder
  (struct mem_allocator* allocator,
   const double radius,
   const double height,
   const unsigned nslices,
   const unsigned nstacks,
   struct s3dut_mesh** mesh)
{
  return s3dut_create_thin_cylinder(allocator, radius, height, nslices,
    nstacks, S3DUT_CAP_POS_Z|S3DUT_CAP_NEG_Z, mesh);
}

res_T
s3dut_create_thin_cylinder
  (struct mem_allocator* allocator,
   const double radius,
   const double height,
   const unsigned nslices,
   const unsigned nstacks,
   const int close_ends,
   struct s3dut_mesh** mesh)
{
  struct s3dut_mesh* cylinder = NULL;
  double* coords = NULL;
  size_t* ids = NULL;
  size_t nverts;
  size_t ntris;
  const int close_bottom = close_ends & S3DUT_CAP_NEG_Z;
  const int close_top = close_ends & S3DUT_CAP_POS_Z;
  const unsigned nb_closed_ends = (close_bottom ? 1u : 0) + (close_top ? 1u : 0);
  res_T res = RES_OK;

  if(radius <= 0 || height <= 0 || nslices < 3 || nstacks < 1 || !mesh) {
    res = RES_BAD_ARG;
    goto error;
  }

  nverts = nslices * (nstacks+1)/*#contour*/ + nb_closed_ends/*#polar*/;
  ntris = 2*nslices*nstacks/*#contour*/ + nb_closed_ends * nslices/*#polar*/;

  res = mesh_create(allocator, S3DUT_MESH_CYLINDER, nverts, ntris, &cylinder);
  if(res != RES_OK) goto error;

  coords = darray_double_data_get(&cylinder->coords);
  ids = darray_size_t_data_get(&cylinder->ids);
  setup_cylinder_coords(coords, radius, -0.5*height, +0.5*height,
    nslices, nstacks, close_bottom, close_top);
  setup_cylinder_indices(ids, 0, nslices, nstacks, close_bottom, close_top, 1);

exit:
  if(mesh) *mesh = cylinder;
  return res;
error:
  if(cylinder) {
    S3DUT(mesh_ref_put(cylinder));
    cylinder = NULL;
  }
  goto exit;
}

res_T
s3dut_create_thick_cylinder
  (struct mem_allocator* allocator,
   const double radius,
   const double height,
   const double thickness,
   const unsigned nslices,
   const unsigned nstacks,
   const int close_ends,
   struct s3dut_mesh** mesh)
{
  struct s3dut_mesh* cylinder = NULL;
  double* coords_out = NULL;
  double* coords_in = NULL;
  size_t* ids_out = NULL;
  size_t* ids_in = NULL;
  size_t* ids_walls = NULL;
  size_t nverts;
  size_t ntris;
  size_t id_offset;
  const int close_bottom = close_ends & S3DUT_CAP_NEG_Z;
  const int close_top = close_ends & S3DUT_CAP_POS_Z;
  const unsigned nb_closed_ends = (close_bottom ? 1u : 0) + (close_top ? 1u : 0);
  res_T res = RES_OK;

  if(radius <= thickness || height <= 0 || thickness <= 0
    || height <= (double)nb_closed_ends * thickness
    || nslices < 3 || nstacks < 1 || !mesh) {
    res = RES_BAD_ARG;
    goto error;
  }

  nverts = 2 * (nslices*(nstacks+1)/*#contour*/ + nb_closed_ends/*#polar*/);
  ntris = 2 *
    ( 2 * nslices*nstacks /*#contour*/
    + 2 * nslices/*#trg fans tris, regardless of closedness*/);

  res = mesh_create
    (allocator, S3DUT_MESH_THICK_CYLINDER, nverts, ntris, &cylinder);
  if(res != RES_OK) goto error;

  coords_out = darray_double_data_get(&cylinder->coords);
  ids_out = darray_size_t_data_get(&cylinder->ids);
  /* External cylinder */
  coords_in = setup_cylinder_coords(coords_out, radius, -0.5*height,
    +0.5*height, nslices, nstacks, close_bottom, close_top);
  ids_in = setup_cylinder_indices
    (ids_out, 0, nslices, nstacks, close_bottom, close_top, 1);
  /* Internal cylinder */
  id_offset = (size_t)(coords_in - coords_out);
  ASSERT(id_offset % 3 == 0);
  id_offset /= 3;
  setup_cylinder_coords(coords_in,
    radius - thickness,
    close_bottom  ? -0.5*height + thickness : -0.5*height,
    close_top  ? +0.5*height -thickness : +0.5*height,
    nslices, nstacks, close_bottom, close_top);
  ids_walls = setup_cylinder_indices
    (ids_in, id_offset, nslices, nstacks, close_bottom, close_top, 0);
  /* Close walls where the cylinder is open */
  if(!close_bottom) {
    ids_walls = close_wall(ids_walls, 0, id_offset, nslices, nstacks, 1);
  }
  if(!close_top) {
    close_wall(ids_walls, nstacks, id_offset + nstacks, nslices, nstacks, 0);
  }

exit:
  if(mesh) *mesh = cylinder;
  return res;
error:
  if(cylinder) {
    S3DUT(mesh_ref_put(cylinder));
    cylinder = NULL;
  }
  goto exit;
}
