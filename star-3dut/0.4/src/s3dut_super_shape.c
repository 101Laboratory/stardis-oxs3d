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

#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys_math.h>

struct spherical {
  double r;
  double theta;
  double phi;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static FINLINE void
cartesian_to_spherical(const double* xyz, struct spherical* spherical)
{
  ASSERT(spherical && xyz);
  spherical->r = d3_len(xyz);
  spherical->phi = asin(xyz[2]/spherical->r);
  /* Map the atan results in [-PI, PI] to ensure that theta lies in [-PI,2PI]
   * rather than [-PI/2, 3PI/2] that would violate the super formula
   * constraints */
  spherical->theta = xyz[0] == 0 ? PI/2 : atan(xyz[1]/xyz[0]) - PI/2;
  if(xyz[0] < 0) spherical->theta += PI;
}

static FINLINE double
super_formula_eval(const struct s3dut_super_formula* form, const double angle)
{
  double m, k, g;
  ASSERT(form);
  m = fabs(cos(form->M * angle / 4.0)) / form->A;
  k = fabs(sin(form->M * angle / 4.0)) / form->B;
  g = pow(m, form->N1) + pow(k, form->N2);
  return pow(g, (-1.0/form->N0));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s3dut_create_super_shape
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const struct s3dut_super_formula* formula0,
   const struct s3dut_super_formula* formula1,
   const double radius, /* In [0, INF) */
   const unsigned nslices, /* # subdivisions around Z axis in [3, INF) */
   const unsigned nstacks, /* # subdivisions along Z axis in [2, INF) */
   struct s3dut_mesh** super_shape)
{
  return s3dut_create_thick_truncated_super_shape(allocator, formula0,
    formula1, radius, 0, nslices, nstacks, NULL, 0, super_shape);
}

res_T
s3dut_create_thick_truncated_super_shape
  (struct mem_allocator* allocator,
   const struct s3dut_super_formula* formula0,
   const struct s3dut_super_formula* formula1,
   const double radius,
   const double thickness,
   const unsigned nslices,
   const unsigned nstacks,
   const double z_range[2],
   const int cap_mask,
   struct s3dut_mesh** mesh)
{
  struct s3dut_mesh* sshape = NULL;
  size_t nverts;
  size_t ivert;
  res_T res = RES_OK;

  if(!formula0 || !formula1 || !mesh) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(thickness == 0) {
    res = s3dut_create_truncated_sphere(allocator, radius, nslices, nstacks,
      z_range, cap_mask, &sshape);
  } else {
    res = s3dut_create_thick_truncated_sphere(allocator, radius, thickness,
      nslices, nstacks, z_range, cap_mask, &sshape);
  }
  if(res != RES_OK) goto error;

  /* Positioned the sphere vertices wrt to the super formulas */
  nverts = darray_double_size_get(&sshape->coords) / 3u;
  FOR_EACH(ivert, 0, nverts) {
    double* pos = darray_double_data_get(&sshape->coords) + ivert*3;
    struct spherical spherical;
    double uv[2];
    double cos_theta, cos_phi;
    double sin_theta, sin_phi;

    cartesian_to_spherical(pos, &spherical);

    uv[0] = super_formula_eval(formula0, spherical.theta);
    uv[1] = super_formula_eval(formula1, spherical.phi);

    cos_theta = cos(spherical.theta);
    sin_theta = sin(spherical.theta);
    cos_phi = cos(spherical.phi);
    sin_phi = sin(spherical.phi);

    pos[0] = uv[0] * cos_theta * uv[1] * cos_phi * spherical.r;
    pos[1] = uv[0] * sin_theta * uv[1] * cos_phi * spherical.r;
    pos[2] = uv[1] * sin_phi * spherical.r;
  }

  sshape->type = S3DUT_MESH_SUPER_SHAPE;

exit:
  if(mesh) *mesh = sshape;
  return res;
error:
  if(sshape) { S3DUT(mesh_ref_put(sshape)); sshape = NULL; }
  goto exit;
}

