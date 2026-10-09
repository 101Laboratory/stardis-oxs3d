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
static res_T
setup_sphere_coords
  (struct mem_allocator* allocator,
   double** coords_ptr,
   const double radius,
   const double z_range[2],
   const unsigned nslices, /* # subdivisions around the Z axis */
   const unsigned nstacks, /* # subdivisions along the Z axis */
   const int close_ends)
{
  enum { SIN, COS };
  double* coords;
  struct darray_double sincos_theta;
  struct darray_double sincos_phi;
  double step_theta;
  const int top_truncated = z_range && z_range[1] < +radius;
  const int bottom_truncated = z_range && z_range[0] > -radius;
  const unsigned nb_truncated = (unsigned)(top_truncated + bottom_truncated);
  const int close_top = top_truncated && (close_ends & S3DUT_CAP_POS_Z);
  const int close_bottom = bottom_truncated && (close_ends & S3DUT_CAP_NEG_Z);
  const unsigned nrings = nstacks - 1 + nb_truncated;
  const double phi_z_min
    = bottom_truncated ? asin(CLAMP(z_range[0] / radius, -1, 1)) : -PI / 2;
  const double phi_z_max
    = top_truncated ? asin(CLAMP(z_range[1] / radius, -1, 1)) : +PI / 2;
  double step_phi
    = (phi_z_max - phi_z_min) / (double)(nrings + 1 - nb_truncated);
  size_t itheta;
  size_t iphi;
  size_t i = 0;
  res_T res = RES_OK;
  ASSERT(coords_ptr && *coords_ptr && radius > 0);

  coords = *coords_ptr;
  darray_double_init(allocator, &sincos_theta);
  darray_double_init(allocator, &sincos_phi);

  res = darray_double_resize(&sincos_theta, nslices*2/*sin & cos*/);
  if(res != RES_OK) goto error;
  res = darray_double_resize(&sincos_phi, nrings*2/*sin & cos*/);
  if(res != RES_OK) goto error;

  /* Precompute the sinus/cosine of the theta/phi angles */
  step_theta = 2*PI / (double)nslices;
  FOR_EACH(itheta, 0, nslices) {
    const double theta = -PI + (double)itheta * step_theta;
    darray_double_data_get(&sincos_theta)[itheta*2 + SIN] = sin(theta);
    darray_double_data_get(&sincos_theta)[itheta*2 + COS] = cos(theta);
  }
  FOR_EACH(iphi, 0, nrings) {
    const double phi
      = phi_z_min + (double)(iphi + !bottom_truncated) * step_phi;
    darray_double_data_get(&sincos_phi)[iphi*2 + SIN] = sin(phi);
    darray_double_data_get(&sincos_phi)[iphi*2 + COS] = cos(phi);
  }

  /* Setup the contour vertices */
  i = 0;
  FOR_EACH(itheta, 0, nslices) {
    const double* theta = darray_double_cdata_get(&sincos_theta) + itheta*2;
    FOR_EACH(iphi, 0, nrings) {
      const double* phi = darray_double_cdata_get(&sincos_phi) + iphi*2;
      coords[i++] = radius * COS[theta] * COS[phi];
      coords[i++] = radius * SIN[theta] * COS[phi];
      coords[i++] = radius * SIN[phi];
    }
  }

  if(close_bottom || !bottom_truncated) {
    /* Setup the bottom polar vertex */
    coords[i++] = 0;
    coords[i++] = 0;
    coords[i++] = bottom_truncated ? z_range[0] : -radius;
  }

  if(close_top || !top_truncated) {
    /* Setup the top polar vertex */
    coords[i++] = 0;
    coords[i++] = 0;
    coords[i++] = top_truncated ? z_range[1] : +radius;
  }

exit:
  darray_double_release(&sincos_theta);
  darray_double_release(&sincos_phi);
  *coords_ptr = coords + i;
  return res;
error:
  goto exit;
}

static size_t*
setup_sphere_indices
  (size_t* ids,
   const size_t offset,
   const unsigned nslices, /* # subdivisions around the Z axis */
   const unsigned nstacks, /* # subdivisions along the Z axis */
   const double radius,
   const double z_range[2],
   const int close_ends,
   const int cw_out)
{
  const int top_truncated = z_range && z_range[1] < +radius;
  const int bottom_truncated = z_range && z_range[0] > -radius;
  const int close_top = top_truncated && (close_ends & S3DUT_CAP_POS_Z);
  const int close_bottom = bottom_truncated && (close_ends & S3DUT_CAP_NEG_Z);
  const unsigned nb_truncated = (unsigned)(top_truncated + bottom_truncated);
  const unsigned nrings = nstacks - 1 + nb_truncated;
  size_t ibottom;
  size_t itop;
  size_t i, itheta, iphi;
  ASSERT(ids && nslices && nstacks);

  /* Define the indices of the contour primitives */
  i = 0;
  FOR_EACH(itheta, 0, nslices) {
    const size_t itheta0 = offset + itheta * nrings;
    const size_t itheta1 = offset + ((itheta + 1) % nslices) * nrings;
    FOR_EACH(iphi, 0, nrings-1) {
      const size_t iphi0 = iphi + 0;
      const size_t iphi1 = iphi + 1;

      ids[i] = itheta0 + iphi0;
      ids[cw_out?i+1:i+2] = itheta0 + iphi1;
      ids[cw_out?i+2:i+1] = itheta1 + iphi0;
      i += 3;

      ids[i] = itheta1 + iphi0;
      ids[cw_out?i+1:i+2] = itheta0 + iphi1;
      ids[cw_out?i+2:i+1] = itheta1 + iphi1;
      i += 3;
    }
  }

  /* Define the indices of the polar primitives */
  FOR_EACH(itheta, 0, nslices) {
    const size_t itheta0 = offset + itheta * nrings;
    const size_t itheta1 = offset + ((itheta + 1) % nslices) * nrings;

    if(close_bottom || !bottom_truncated) {
      ibottom = nslices * nrings;
      ids[i] = offset + ibottom;
      ids[cw_out?i+1:i+2] = itheta0;
      ids[cw_out?i+2:i+1] = itheta1;
      i += 3;
    }

    if(close_top || !top_truncated) {
      itop = (close_bottom || !bottom_truncated)
        ? nslices * nrings + 1 : nslices * nrings;
      ids[i] = offset + itop;
      ids[cw_out?i+1:i+2] = itheta1 + (nrings - 1);
      ids[cw_out?i+2:i+1] = itheta0 + (nrings - 1);
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
   const unsigned external_nrings,
   const unsigned internal_nrings,
   const int bottom)
{
  size_t islice;
  size_t i = 0;
  ASSERT(ids && nslices >= 3 && external_nrings >= 1 && internal_nrings >= 1);

  FOR_EACH(islice, 0, nslices) {
    ids[i] = fst_id_out + islice * external_nrings;
    ids[bottom?i+1:i+2] = fst_id_in + ((islice+1) % nslices) * internal_nrings;
    ids[bottom?i+2:i+1] = fst_id_in + islice * internal_nrings;
    i += 3;

    ids[i] = fst_id_out + islice * external_nrings;
    ids[bottom?i+1:i+2] = fst_id_out + ((islice+1) % nslices) * external_nrings;
    ids[bottom?i+2:i+1] = fst_id_in + ((islice+1) % nslices) * internal_nrings;
    i += 3;
  }
  return ids + i;
}

static void
sphere_accum_counts
  (const double radius,
   const unsigned nslices,
   const unsigned nstacks,
   const double z_range[2],
   const int close_ends,
   unsigned* nrings,
   size_t* ntris,
   size_t* nverts)
{
  const int top_truncated = z_range && z_range[1] < +radius;
  const int bottom_truncated = z_range && z_range[0] > -radius;
  const int close_top = top_truncated && (close_ends & S3DUT_CAP_POS_Z);
  const int close_bottom = bottom_truncated && (close_ends & S3DUT_CAP_NEG_Z);
  const unsigned nb_truncated = (unsigned)(top_truncated + bottom_truncated);
  const unsigned nb_closed_ends = (close_top ? 1u : 0) + (close_bottom ? 1u : 0);
  const unsigned nb_pole_vrtx = nb_closed_ends + (2 - nb_truncated);
  ASSERT(nrings && ntris && nverts);
  *nrings = nstacks - 1 + nb_truncated;;
  *nverts += nslices*(*nrings) /* #contour verts*/ + nb_pole_vrtx; /*polar verts*/
  *ntris += 2 * nslices * (*nrings -1) /* #contour tris */
    + (2 - nb_truncated + nb_closed_ends) * nslices; /* #polar tris */
}

/*******************************************************************************
 * Exported function
 ******************************************************************************/
res_T
s3dut_create_sphere
  (struct mem_allocator* allocator,
   const double radius,
   const unsigned nslices,
   const unsigned nstacks,
   struct s3dut_mesh** mesh)
{
  return s3dut_create_truncated_sphere
    (allocator, radius, nslices, nstacks, NULL, 0, mesh);
}

res_T
s3dut_create_hemisphere
  (struct mem_allocator* allocator,
   const double radius,
   const unsigned nslices,
   const unsigned nstacks,
   struct s3dut_mesh** mesh)
{
  double z_range[2];
  z_range[0] = 0;
  z_range[1] = +radius;
  return s3dut_create_truncated_sphere
    (allocator, radius, nslices, nstacks, z_range, 0, mesh);
}

res_T
s3dut_create_truncated_sphere
  (struct mem_allocator* allocator,
   const double radius,
   const unsigned nslices,
   const unsigned nstacks,
   const double z_range[2],
   const int close_ends,
   struct s3dut_mesh** mesh)
{
  struct s3dut_mesh* sphere = NULL;
  const int top_truncated = z_range && z_range[1] < +radius;
  const int bottom_truncated = z_range && z_range[0] > -radius;
  const int close_top = top_truncated && (close_ends & S3DUT_CAP_POS_Z);
  const int close_bottom = bottom_truncated && (close_ends & S3DUT_CAP_NEG_Z);
  const unsigned nb_truncated = (unsigned)(top_truncated + bottom_truncated);
  const unsigned nb_closed_ends = (close_top ? 1u : 0) + (close_bottom ? 1u : 0);
  const unsigned nb_pole_vrtx = nb_closed_ends + (2 - nb_truncated);
  const unsigned nrings = nstacks - 1 + nb_truncated;
  const size_t nverts = nslices*nrings/* #contour verts*/
    + nb_pole_vrtx/*polar verts*/;
  const size_t ntris = 2 * nslices*(nrings - 1)/* #contour tris*/
    + nb_pole_vrtx*nslices/* #polar tris*/;
  double* coords;
  res_T res = RES_OK;
  ASSERT(nb_truncated <= 2);

  if(radius <= 0 || nslices < 3 || nstacks < 2 || !mesh
    || (z_range && z_range[0] >= z_range[1])) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = mesh_create(allocator, S3DUT_MESH_SPHERE, nverts, ntris, &sphere);
  if(res != RES_OK) goto error;

  coords = darray_double_data_get(&sphere->coords);
  res = setup_sphere_coords(allocator, &coords, radius, z_range, nslices,
    nstacks, close_ends);
  if(res != RES_OK) goto error;

  setup_sphere_indices(darray_size_t_data_get(&sphere->ids), 0,
    nslices, nstacks, radius, z_range, close_ends, 1);

exit:
  if(mesh) *mesh = sphere;
  return res;
error:
  if(sphere) {
    S3DUT(mesh_ref_put(sphere));
    sphere = NULL;
  }
  goto exit;
}

res_T
s3dut_create_thick_truncated_sphere
  (struct mem_allocator* allocator,
   const double radius,
   const double thickness,
   const unsigned nslices,
   const unsigned nstacks,
   const double z_range[2],
   const int c_e,
   struct s3dut_mesh** mesh)
{
  struct s3dut_mesh* sphere = NULL;
  int close_ends = c_e;
  double* coords = NULL;
  double* prev_coords;
  size_t* ids_out = NULL;
  size_t* ids_in = NULL;
  size_t* ids_walls = NULL;
  size_t id_offset;
  const double internal_radius = radius - thickness;
  const int top_truncated = z_range && (z_range[1] < +radius);
  const int bottom_truncated = z_range && (z_range[0] > -radius);
  const int close_top = top_truncated && (close_ends & S3DUT_CAP_POS_Z);
  const int close_bottom = bottom_truncated && (close_ends & S3DUT_CAP_NEG_Z);
  const int top_seam = z_range && (z_range[1] < internal_radius) && !close_top;
  const int bottom_seam
    = z_range && (z_range[0] > -internal_radius) && ! close_bottom;
  unsigned external_nrings;
  unsigned internal_nrings;
  const unsigned nb_seams = (unsigned)(top_seam + bottom_seam);
  size_t nverts = 0;
  size_t ntris = 0;
  double z_internal_range[2];
  res_T res = RES_OK;

  if(radius <= thickness || thickness <= 0 || nslices < 3 || nstacks < 2
  || !mesh || (z_range && z_range[0] >= z_range[1])) {
    return RES_BAD_ARG;
  }

  /* Special case when a single sphere is truncated */
  if(top_truncated && (z_range[1] >= internal_radius)) {
    close_ends |= S3DUT_CAP_POS_Z; /* close the external sphere's top end */
  }
  if(bottom_truncated && (z_range[0] <= -internal_radius)) {
    close_ends |= S3DUT_CAP_NEG_Z; /* close the external sphere's bottom end */
  }
  z_internal_range[0] = (bottom_truncated && !close_bottom)
    ? z_range[0] : (z_range ? z_range[0] : -radius) + thickness;
  z_internal_range[1] = (top_truncated && !close_top) ?
    z_range[1] : (z_range ? z_range[1] : +radius) - thickness;
  sphere_accum_counts(radius, nslices, nstacks, z_range, close_ends,
    &external_nrings, &ntris, &nverts);
  sphere_accum_counts(radius-thickness, nslices, nstacks, z_internal_range,
    close_ends, &internal_nrings, &ntris, &nverts);
  ntris += 2 * nb_seams * nslices; /* # seam tris */
  res = mesh_create(allocator, S3DUT_MESH_THICK_SPHERE, nverts, ntris, &sphere);
  if(res != RES_OK) goto error;

  coords = darray_double_data_get(&sphere->coords);
  ids_out = darray_size_t_data_get(&sphere->ids);
  /* External sphere */
  prev_coords = coords;
  setup_sphere_coords
    (allocator, &coords, radius, z_range, nslices, nstacks, close_ends);
  ids_in = setup_sphere_indices(ids_out, 0, nslices, nstacks, radius, z_range,
    close_ends, 1);
  /* Internal sphere */
  id_offset = (size_t)(coords - prev_coords);
  ASSERT(id_offset % 3 == 0);
  id_offset /= 3;
  setup_sphere_coords(allocator, &coords, radius - thickness, z_internal_range,
    nslices, nstacks, close_ends);
  ids_walls = setup_sphere_indices (ids_in, id_offset, nslices,
    nstacks, radius - thickness, z_internal_range, close_ends, 0);
  if(bottom_seam) {
    ids_walls = close_wall(ids_walls, 0, id_offset, nslices, external_nrings,
      internal_nrings, 1);
  }
  if(top_seam) {
    ids_walls = close_wall(ids_walls, nstacks, id_offset +  nstacks, nslices,
      external_nrings, internal_nrings, 0);
  }

exit:
  if(mesh) *mesh = sphere;
  return res;
error:
  if(sphere) {
    S3DUT(mesh_ref_put(sphere));
    sphere = NULL;
  }
  goto exit;
}
