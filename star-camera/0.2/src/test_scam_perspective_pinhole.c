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

#include <rsys/double3.h>
#include <rsys/logger.h>
#include<rsys/mem_allocator.h>

#include <rsys_math.h>

static void
log_stream(const char* msg, void* ctx)
{
  ASSERT(msg);
  (void)msg, (void)ctx;
  printf("%s", msg);
}

int
main(int argc, char** argv)
{
  struct logger logger;
  struct scam_perspective_args args = SCAM_PERSPECTIVE_ARGS_DEFAULT;
  struct scam_sample sample = SCAM_SAMPLE_NULL;
  struct scam_ray ray = SCAM_RAY_NULL;
  struct scam* cam = NULL;
  const size_t nsamps = 10000;
  double axis_x[3];
  double axis_y[3];
  double axis_z[3];
  double hori_hfov; /* horizontal half field of view */
  double sin_hori_hfov; /* Sinus of the horizontal half field of view */
  double sin_vert_hfov; /* Sinos of the vertical half field of view */
  double img_depth;
  double solid_angle;
  double ref;
  size_t i = 0;
  enum scam_type type = SCAM_NONE;
  (void)argc, (void)argv;

  CHK(scam_create_perspective(NULL, NULL, 0, NULL, &cam) == RES_BAD_ARG);
  CHK(scam_create_perspective(NULL, NULL, 0, &args, NULL) == RES_BAD_ARG);
  CHK(scam_create_perspective(NULL, NULL, 0, &args, &cam) == RES_OK);

  CHK(scam_get_type(NULL, &type) == RES_BAD_ARG);
  CHK(scam_get_type(cam, NULL) == RES_BAD_ARG);
  CHK(scam_get_type(cam, &type) == RES_OK);
  CHK(type == SCAM_PERSPECTIVE);

  CHK(scam_ref_get(NULL) == RES_BAD_ARG);
  CHK(scam_ref_get(cam) == RES_OK);
  CHK(scam_ref_put(NULL) == RES_BAD_ARG);
  CHK(scam_ref_put(cam) == RES_OK);
  CHK(scam_ref_put(cam) == RES_OK);

  CHK(logger_init(&mem_default_allocator, &logger) == RES_OK);
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  CHK(scam_create_perspective(&logger, NULL, 0, &args, &cam) == RES_OK);
  CHK(scam_ref_put(cam) == RES_OK);

  CHK(scam_create_perspective(NULL, &mem_default_allocator, 0, &args, &cam) == RES_OK);
  CHK(scam_ref_put(cam) == RES_OK);

  d3_set(args.target, args.position);
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);

  d3(args.position, 0, 0, 0);
  d3(args.target, 0, 1, 0);
  d3(args.up, 0, 1, 0);
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);

  args = SCAM_PERSPECTIVE_ARGS_DEFAULT;
  args.aspect_ratio = 0;
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);

  args = SCAM_PERSPECTIVE_ARGS_DEFAULT;
  args.field_of_view = 0;
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);
  args.field_of_view = PI;
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);
  args.field_of_view = nextafter(0, PI);
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_OK);
  CHK(scam_ref_put(cam) == RES_OK);
  args.field_of_view = nextafter(PI, 0);
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_OK);
  CHK(scam_ref_put(cam) == RES_OK);

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
  args.aspect_ratio = 1.0;

  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_OK);
  CHK(scam_perspective_get_solid_angle(NULL, &solid_angle) == RES_BAD_ARG);
  CHK(scam_perspective_get_solid_angle(cam, NULL) == RES_BAD_ARG);
  CHK(scam_perspective_get_solid_angle(cam, &solid_angle) == RES_OK);

  ref = args.field_of_view * 2*sin(args.field_of_view/2);
  CHK(eq_eps(solid_angle, ref, 1.e-6));
  CHK(scam_ref_put(cam) == RES_OK);

  args.aspect_ratio = 4.0/3.0;
  CHK(scam_create_perspective(NULL, NULL, 1, &args, &cam) == RES_OK);

  CHK(scam_perspective_get_solid_angle(cam, &solid_angle) == RES_OK);
  ref = 2.6227869472431911;
  CHK(eq_eps(solid_angle, ref, 1.e-6));

  /* Precompute some view frustum constants */
  d3_normalize(axis_z, d3_sub(axis_z, args.target, args.position));
  d3_normalize(axis_x, d3_cross(axis_x, axis_z, args.up));
  d3_normalize(axis_y, d3_cross(axis_y, axis_z, axis_x));
  img_depth = 1.0/tan(args.field_of_view*0.5);
  hori_hfov = atan(args.aspect_ratio / img_depth);
  sin_hori_hfov = sin(hori_hfov);
  sin_vert_hfov = sin(args.field_of_view*0.5);

  CHK(scam_generate_ray(NULL, &sample, &ray) == RES_BAD_ARG);
  CHK(scam_generate_ray(cam, NULL, &ray) == RES_BAD_ARG);
  CHK(scam_generate_ray(cam, &sample, NULL) == RES_BAD_ARG);

  sample.film[0] = nextafterf(0, -1);
  CHK(scam_generate_ray(cam, &sample, &ray) == RES_BAD_ARG);
  sample.film[0] = 1;
  CHK(scam_generate_ray(cam, &sample, &ray) == RES_BAD_ARG);
  sample.film[0] = 0;
  sample.film[1] = nextafterf(0, -1);
  CHK(scam_generate_ray(cam, &sample, &ray) == RES_BAD_ARG);
  sample.film[1] = 1;
  CHK(scam_generate_ray(cam, &sample, &ray) == RES_BAD_ARG);
  sample.film[0] = 0;
  sample.film[1] = nextafterf(1, 0);
  CHK(scam_generate_ray(cam, &sample, &ray) == RES_OK);

  FOR_EACH(i, 0, nsamps) {
    double cos_ray_axis_x;
    double cos_ray_axis_y;
    double cos_ray_axis_z;
    sample.film[0] = rand_canonical();
    sample.film[1] = rand_canonical();
    CHK(scam_generate_ray(cam, &sample, &ray) == RES_OK);

    /* Check the ray origin */
    CHK(d3_eq(ray.org, args.position));

    /* Check that the generated ray is in the view frustum */
    CHK(d3_is_normalized(ray.dir));
    cos_ray_axis_x = d3_dot(ray.dir, axis_x);
    cos_ray_axis_y = d3_dot(ray.dir, axis_y);
    cos_ray_axis_z = d3_dot(ray.dir, axis_z);
    CHK(cos_ray_axis_z >= 0);
    CHK(cos_ray_axis_y <= sin_vert_hfov);
    CHK(cos_ray_axis_x <= sin_hori_hfov);
  }
  CHK(scam_ref_put(cam) == RES_OK);

  logger_release(&logger);
  CHK(mem_allocated_size() == 0);
  return 0;
}
