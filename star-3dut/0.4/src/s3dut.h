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

#ifndef S3DUT_H
#define S3DUT_H

#include <rsys/rsys.h>

/* Library symbol management */
#if defined(S3DUT_SHARED_BUILD) /* Build shared library */
  #define S3DUT_API extern EXPORT_SYM
#elif defined(S3DUT_STATIC_BUILD) /* Use/build static library */
  #define S3DUT_API extern LOCAL_SYM
#else /* Use shared library */
  #define S3DUT_API extern IMPORT_SYM
#endif

/* Helper macro that asserts if the invocation of the s3dut function `Func'
 * returns an error. */
#ifndef NDEBUG
  #define S3DUT(Func) ASSERT(s3dut_##Func == RES_OK)
#else
  #define S3DUT(Func) s3dut_##Func
#endif

struct mem_allocator;
struct s3dut_mesh;

struct s3dut_mesh_data {
  const double* positions;
  const size_t* indices;
  size_t nvertices; /* # vertices */
  size_t nprimitives; /* # primitives */
};

/* Defines the radius 'r' with respect the an angle 'a':
 * r(a) = ( |cos(M*a/4)/A)|^N1 + |sin(M*a/4)/B|^N2 )^{-1/N0} */
struct s3dut_super_formula {
  double A;
  double B;
  double M;
  double N0;
  double N1;
  double N2;
};
#define S3DUT_SUPER_FORMULA_NULL__ {0, 0, 0, 0, 0, 0}
static const struct s3dut_super_formula S3DUT_SUPER_FORMULA_NULL =
  S3DUT_SUPER_FORMULA_NULL__;

enum s3dut_cap_flag {
  S3DUT_CAP_NEG_Z = BIT(0),
  S3DUT_CAP_POS_Z = BIT(1)
};

BEGIN_DECLS

/*******************************************************************************
 * Stard-3DUT API
 ******************************************************************************/
S3DUT_API res_T
s3dut_mesh_ref_get
  (struct s3dut_mesh* mesh);

S3DUT_API res_T
s3dut_mesh_ref_put
  (struct s3dut_mesh* mesh);

S3DUT_API res_T
s3dut_mesh_get_data
  (const struct s3dut_mesh* mesh,
   struct s3dut_mesh_data* data);

/* Create a triangulated UV sphere centered in 0 discretized in `nslices'
 * around the Z axis and `nstacks' along the Z axis. Face vertices are CCW
 * ordered with respect to the sphere center, i.e. they are CW ordered from the
 * outside point of view. */
S3DUT_API res_T
s3dut_create_sphere
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const double radius, /* In ]0, INF) */
   const unsigned nslices, /* # subdivisions around Z axis in [3, INF) */
   const unsigned nstacks, /* # subdivisions along Z axis in [2, INF) */
   struct s3dut_mesh** sphere);

/* Create a triangulated cylinder centered in 0 discretized in `nslices' around
 * the Z axis and `nstacks' along the Z axis. The top and the bottom ends of
 * the cylinder are closed with triangle fans whose center is on the Z axis.
 * Face vertices are CCW ordered with respect to the cylinder center, i.e. they
 * are CW ordered from the outside point of view. */
S3DUT_API res_T
s3dut_create_cylinder
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const double radius, /* In ]0, INF) */
   const double height, /* In ]0, INF) */
   const unsigned nslices, /* # subdivisions around Z axis in [3, INF) */
   const unsigned nstacks, /* # subdivision along Z axis in [1, INF) */
   struct s3dut_mesh** cylinder);

/* Create a triangulated cylinder centered in 0 discretized in `nslices' around
 * the Z axis and `nstacks' along the Z axis. The top and the bottom ends of
 * the cylinder can be closed or not with a triangle fan whose center is on the
 * Z axis, according to the `cap_mask' argument. Face vertices are CCW ordered
 * with respect to the cylinder center, i.e. they are CW ordered from the
 * outside point of view.  Similar to s3dut_create_cylinder with the ability to
 * close ends or not. */
S3DUT_API res_T
s3dut_create_thin_cylinder
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const double radius, /* In ]0, INF) */
   const double height, /* In ]0, INF) */
   const unsigned nslices, /* # subdivisions around Z axis in [3, INF) */
   const unsigned nstacks, /* # subdivision along Z axis in [1, INF) */
   const int cap_mask, /* Combination of s3dut_cap_flag */
   struct s3dut_mesh** cylinder);

/* Create a triangulated thick cylinder centered in 0 discretized in `nslices'
 * around the Z axis and `nstacks' along the Z axis, with walls of thickness
 * `thickness'. The top and the bottom of the cylinder can be closed or not,
 * according to the `cap_mask' argument. Closed ends are closed by a wall of
 * thickness `thickness' made of 2 triangle fans centered on the Z axis. Face
 * vertices are CCW ordered with respect to the walls' inside, i.e.  they are
 * CW ordered from any point of view outside of the walls. */
S3DUT_API res_T
s3dut_create_thick_cylinder
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const double radius, /* In ]thickness, INF); exterior radius */
   const double height, /* In ]min_height, INF); min_height = 0,
                           tickness or 2*thickness according to cap_mask */
   const double thickness, /* In ]0, INF) */
   const unsigned nslices, /* # subdivisions around Z axis in [3, INF) */
   const unsigned nstacks, /* # subdivision along Z axis in [1, INF) */
   const int cap_mask, /* Combination of s3dut_cap_flag */
   struct s3dut_mesh** cylinder);

/* Create a triangulated cuboid centered in 0. Face vertices are CCW ordered
 * with respect to the cylinder center, i.e. they are CW ordered from the
 * outside point of view. */
S3DUT_API res_T
s3dut_create_cuboid
  (struct mem_allocator* allocator,
   const double width,
   const double height,
   const double depth,
   struct s3dut_mesh** cuboid);

/* Create a triangulated UV hemisphere oriented wrt Z axis. It discretized in
 * `nslices' around the Z axis and `nstacks' along the Z axis. The back of the
 * hemisphere is not triangulated. Face vertices are CCW ordered with respect
 * to the hemisphere center, i.e. they are CW ordered from the outside point of
 * view. */
S3DUT_API res_T
s3dut_create_hemisphere
  (struct mem_allocator* allocator,
   const double radius,
   const unsigned nslices, /* # subdivisions around Z axis in [3, INF) */
   const unsigned nstacks, /* # subdivisions along Z axis int [2, INF) */
   struct s3dut_mesh** hemisphere);

/* Create a triangulated UV sphere centered in 0 discretized in `nslices'
 * around the Z axis and `nstacks' along the Z axis. The top and the bottom of
 * the sphere can be truncated at some specified z, according to the `z_range'
 * parameter. If truncated, the top and the bottom of the sphere can be closed
 * with a triangle fan whose center is on the Z axis or not, according to the
 * `cap_mask' argument. Face vertices are CCW ordered with respect to the
 * sphere center, i.e. they are CW ordered from the outside point of view. */
S3DUT_API res_T
s3dut_create_truncated_sphere
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const double radius, /* In ]0, INF) */
   const unsigned nslices, /* # subdivisions around Z axis in [3, INF) */
   const unsigned nstacks, /* # subdivisions along Z axis in [2, INF) */
   const double z_range[2], /* Clamp the sphere to z_range. NULL <=> no clamp */
   const int cap_mask, /* Combination of s3dut_cap_flag. Ignored if no clamp */
   struct s3dut_mesh** sphere);

/* Create a triangulated thick UV sphere centered in 0 discretized in `nslices'
 * around the Z axis and `nstacks' along the Z axis, with walls of thickness
 * `thickness'. The top and the bottom of the sphere can be truncated at some
 * specified z, according to the `z_range' parameter. If truncated, the top and
 * the bottom of the sphere can be closed or not, according to the `cap_mask'
 * argument. Closed ends are closed by a wall of thickness `thickness'
 * made of 2 triangle fans centered on the Z axis. Face vertices are CCW
 * ordered with respect to the walls' inside, i.e. they are CW ordered from any
 * point of view outside of the walls. */
S3DUT_API res_T
s3dut_create_thick_truncated_sphere
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const double radius, /* In ]thickness, INF); exterior radius */
   const double thickness, /* In ]0, INF) */
   const unsigned nslices, /* # subdivisions around Z axis in [3, INF) */
   const unsigned nstacks, /* # subdivisions along Z axis in [2, INF) */
   const double z_range[2], /* Clamp the sphere to z_range. NULL <=> no clamp */
   const int cap_mask, /* Combination of s3dut_cap_flag. Ignored if no clamp */
   struct s3dut_mesh** sphere);

/* Create a triangulated super shape centered in 0 and discretized in `nslices'
 * around the Z axis and `nstacks' along the Z axis. Face vertices are CCW
 * ordered with respect to the center of the super shape.
 *
 * Assuming a point with the spherical coordinates {r, theta, phi} - with r the
 * radius of the sphere, theta in [-PI,2PI] and phi in [-PI/2, PI/2] - the
 * corresponding 3D coordinates onto the super shape is obtained by evaluating
 * the following relations:
 *    x = r0(theta)*cos(theta) * r1(phi)*cos(phi)
 *    y = r0(theta)*sin(theta) * r1(phi)*cos(phi)
 *    z = r1(phi)*sin(phi)
 * with r0 and r1 refering to the two super formulas. */
S3DUT_API res_T
s3dut_create_super_shape
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const struct s3dut_super_formula* formula0,
   const struct s3dut_super_formula* formula1,
   const double radius, /* In [0, INF) */
   const unsigned nslices, /* # subdivisions around Z axis in [3, INF) */
   const unsigned nstacks, /* # subdivisions along Z axis in [2, INF) */
   struct s3dut_mesh** super_shape);

/* Create a triangulated super shape centered in 0 and discretized in `nslices'
 * around the Z axis and `nstacks' along the Z axis, with walls of thickness
 * `thickness'. Refer to the comments of the s3dut_create_super_shape function
 * for informations on super shape geometry. The top and the bottom of the
 * super shape can be truncated at some specified z, according to the `z_range'
 * parameter. If truncated, the top and the bottom of the super shape can be
 * closed with a triangle fan whose center is on the Z axis or not, according
 * to the `cap_mask' argument. Face vertices are CCW ordered with from the
 * inside point of view. */
S3DUT_API res_T
s3dut_create_thick_truncated_super_shape
  (struct mem_allocator* allocator, /* May be NULL <=> use default allocator */
   const struct s3dut_super_formula* formula0,
   const struct s3dut_super_formula* formula1,
   const double radius, /* In [0, INF) */
   const double thickness, /* In ]0, INF) */
   const unsigned nslices, /* # subdivisions around Z axis in [3, INF) */
   const unsigned nstacks, /* # subdivisions along Z axis in [2, INF) */
   const double z_range[2], /* Clamp the sphere to z_range. NULL <=> no clamp */
   const int cap_mask, /* Combination of s3dut_cap_flag. Ignored if no clamp */
   struct s3dut_mesh** super_shape);

END_DECLS

#endif /* S3DUT_H */
