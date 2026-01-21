/* Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#define _POSIX_C_SOURCE 200112L /* nextafterf */

#include "scam.h"
#include "test_scam_utils.h"

#include <rsys/double2.h>
#include <rsys/double3.h>
#include<rsys/double33.h>
#include <rsys/math.h>
#include<rsys/mem_allocator.h>

int
main(int argc, char** argv)
{
  struct scam* cam = NULL;
  struct scam_perspective_args args = SCAM_PERSPECTIVE_ARGS_DEFAULT;
  struct scam_sample sample = SCAM_SAMPLE_NULL;
  struct scam_ray ray = SCAM_RAY_NULL;
  const size_t nsamps = 10000;
  size_t i;
  double transform[9];
  double axis_x[3];
  double axis_y[3];
  double axis_z[3];
  double hori_hfov; /* horizontal half field of view */
  double sin_hori_hfov; /* Sinus of the horizontal half field of view */
  double sin_vert_hfov; /* Sinos of the vertical half field of view */
  double depth;
  double fov;
  double length;
  double radius;
  double ref;
  (void)argc, (void)argv;

  args.lens_radius = -1;
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);
  args.lens_radius = 1;
  args.focal_distance = -1;
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);
  args.focal_distance = 1;
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_OK);
  CHK(scam_ref_put(cam) == RES_OK);

  length = 1.2;
  radius = 0.01;
  CHK(scam_focal_length_to_field_of_view(-1, length, &fov) == RES_BAD_ARG);
  CHK(scam_focal_length_to_field_of_view(radius, 0, &fov) == RES_BAD_ARG);
  CHK(scam_focal_length_to_field_of_view(radius, length, &fov) == RES_OK);

  ref = 2*atan(radius/length);
  CHK(eq_eps(fov, ref, ref*1.e-6));

  ref = length;
  CHK(scam_field_of_view_to_focal_length(-1, fov, &length) == RES_BAD_ARG);
  CHK(scam_field_of_view_to_focal_length(radius, 0, &length) == RES_BAD_ARG);
  CHK(scam_field_of_view_to_focal_length(radius, PI, &length) == RES_BAD_ARG);
  CHK(scam_field_of_view_to_focal_length(radius, fov, &length) == RES_OK);

  CHK(eq_eps(length, ref, ref*1.e-6));

  args.position[0] = rand_canonical();
  args.position[1] = rand_canonical();
  args.position[2] = rand_canonical();
  args.target[0] = rand_canonical();
  args.target[1] = rand_canonical();
  args.target[2] = rand_canonical();
  args.up[0] = rand_canonical();
  args.up[1] = rand_canonical();
  args.up[2] = rand_canonical();
  args.field_of_view = PI/2.0;
  args.aspect_ratio = 4.0/3.0;
  args.lens_radius = 0.01;
  args.focal_distance = 12.34;

  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_OK);

  /* Precompute some view frustum constants */
  d3_normalize(axis_z, d3_sub(axis_z, args.target, args.position));
  d3_normalize(axis_x, d3_cross(axis_x, axis_z, args.up));
  d3_normalize(axis_y, d3_cross(axis_y, axis_z, axis_x));
  depth = 1.0/tan(args.field_of_view*0.5);
  hori_hfov = atan(args.aspect_ratio / depth);
  sin_hori_hfov = sin(hori_hfov);
  sin_vert_hfov = sin(args.field_of_view*0.5);

  /* Compute the world to camera space transformation */
  d3_set(transform + 0, axis_x);
  d3_set(transform + 3, axis_y);
  d3_set(transform + 6, axis_z);
  d33_transpose(transform, transform);

  sample.lens[0] = nextafterf(0, -1);
  CHK(scam_generate_ray(cam, &sample, &ray) == RES_BAD_ARG);
  sample.lens[0] = 1;
  CHK(scam_generate_ray(cam, &sample, &ray) == RES_BAD_ARG);
  sample.lens[0] = 0;
  sample.lens[1] = nextafterf(0, -1);
  CHK(scam_generate_ray(cam, &sample, &ray) == RES_BAD_ARG);
  sample.lens[1] = 1;
  CHK(scam_generate_ray(cam, &sample, &ray) == RES_BAD_ARG);
  sample.film[0] = sample.lens[0] = 0;
  sample.film[1] = sample.lens[1] = nextafterf(1, 0);
  CHK(scam_generate_ray(cam, &sample, &ray) == RES_OK);

  FOR_EACH(i, 0, nsamps) {
    double cos_ray_axis_x;
    double cos_ray_axis_y;
    double cos_ray_axis_z;
    double dir[3];
    double pos[3];
    double dst;

    sample.film[0] = rand_canonical();
    sample.film[1] = rand_canonical();
    sample.lens[0] = rand_canonical();
    sample.lens[1] = rand_canonical();
    CHK(scam_generate_ray(cam, &sample, &ray) == RES_OK);

    /* Transform the position in camera space */
    d3_sub(pos, ray.org, args.position);
    d33_muld3(pos, transform, pos);

    /* Check that the sampled position lies onto the thin lens */
    dst = d2_len(pos);
    CHK(eq_eps(pos[2], 0, 1.e-6));
    CHK(dst < args.lens_radius
     || eq_eps(dst, args.lens_radius, args.lens_radius*1.e-6));

    /* Compute the ray starting from the lens center and intersecting the focal
     * plane at the same position of the generated ray */
    pos[0] = ray.org[0] + ray.dir[0]*args.focal_distance;
    pos[1] = ray.org[1] + ray.dir[1]*args.focal_distance;
    pos[2] = ray.org[2] + ray.dir[2]*args.focal_distance;
    CHK(d3_is_normalized(ray.dir));
    d3_add(pos, ray.org, ray.dir);
    d3_sub(dir, pos, args.position);
    d3_normalize(dir, dir);

    /* Check that the computed direction is in the view frustum */
    cos_ray_axis_x = d3_dot(dir, axis_x);
    cos_ray_axis_y = d3_dot(dir, axis_y);
    cos_ray_axis_z = d3_dot(dir, axis_z);
    CHK(cos_ray_axis_z >= 0);
    CHK(cos_ray_axis_y <= sin_vert_hfov);
    CHK(cos_ray_axis_x <= sin_hori_hfov);
  }

  CHK(scam_ref_put(cam) == RES_OK);
  CHK(mem_allocated_size() == 0);
  return 0;
}
