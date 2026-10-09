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

#include "scam.h"
#include "test_scam_utils.h"

#include <rsys/double3.h>
#include<rsys/double33.h>
#include <rsys/logger.h>

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
  struct scam_orthographic_args args = SCAM_ORTHOGRAPHIC_ARGS_DEFAULT;
  struct scam* cam = NULL;
  const size_t nsamps = 10000;
  double screen2world[9];
  double world2screen[9];
  double axis_x[3];
  double axis_y[3];
  double axis_z[3];
  double solid_angle;
  size_t i;
  enum scam_type type = SCAM_NONE;
  (void)argc, (void)argv;

  CHK(scam_create_orthographic(NULL, NULL, 0, NULL, &cam) == RES_BAD_ARG);
  CHK(scam_create_orthographic(NULL, NULL, 0, &args, NULL) == RES_BAD_ARG);
  CHK(scam_create_orthographic(NULL, NULL, 0, &args, &cam) == RES_OK);

  CHK(scam_get_type(cam, &type) == RES_OK);
  CHK(type == SCAM_ORTHOGRAPHIC);
  CHK(scam_ref_put(cam) == RES_OK);

  CHK(logger_init(&mem_default_allocator, &logger) == RES_OK);
  logger_set_stream(&logger, LOG_OUTPUT, log_stream, NULL);
  logger_set_stream(&logger, LOG_ERROR, log_stream, NULL);
  logger_set_stream(&logger, LOG_WARNING, log_stream, NULL);

  CHK(scam_create_orthographic(&logger, NULL, 0, &args, &cam) == RES_OK);
  CHK(scam_ref_put(cam) == RES_OK);

  CHK(scam_create_orthographic(NULL, &mem_default_allocator, 0, &args, &cam) == RES_OK);
  CHK(scam_ref_put(cam) == RES_OK);

  d3_set(args.target, args.position);
  CHK(scam_create_orthographic(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);

  d3(args.position, 0, 0, 0);
  d3(args.target, 0, 1, 0);
  d3(args.up, 0, 1, 0);
  CHK(scam_create_orthographic(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);

  args = SCAM_ORTHOGRAPHIC_ARGS_DEFAULT;
  args.aspect_ratio = 0;
  CHK(scam_create_orthographic(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);

  args.aspect_ratio = 1;
  args.height = 0;
  CHK(scam_create_orthographic(NULL, NULL, 1, &args, &cam) == RES_BAD_ARG);

  args.position[0] = rand_canonical();
  args.position[1] = rand_canonical();
  args.position[2] = rand_canonical();
  args.target[0] = rand_canonical();
  args.target[1] = rand_canonical();
  args.target[2] = rand_canonical();
  args.up[0] = rand_canonical();
  args.up[1] = rand_canonical();
  args.up[2] = rand_canonical();
  args.height = 10;
  args.aspect_ratio = 16.0/9.0;

  CHK(scam_create_orthographic(NULL, NULL, 1, &args, &cam) == RES_OK);
  CHK(scam_perspective_get_solid_angle(cam, &solid_angle) == RES_BAD_ARG);

  /* Precompute camera parameters */
  d3_normalize(axis_z, d3_sub(axis_z, args.target, args.position));
  d3_normalize(axis_x, d3_cross(axis_x, axis_z, args.up));
  d3_normalize(axis_y, d3_cross(axis_y, axis_z, axis_x));

  d3_muld(screen2world+0, axis_x, 0.5*args.height*args.aspect_ratio);
  d3_muld(screen2world+3, axis_y, 0.5*args.height);
  d3_set (screen2world+6, axis_z);
  d33_inverse(world2screen, screen2world);

  FOR_EACH(i, 0, nsamps) {
    double pos[3];
    struct scam_sample sample = SCAM_SAMPLE_NULL;
    struct scam_ray ray = SCAM_RAY_NULL;

    sample.film[0] = rand_canonical();
    sample.film[1] = rand_canonical();
    CHK(scam_generate_ray(cam, &sample, &ray) == RES_OK);

    /* Check the ray direction */
    CHK(d3_eq_eps(ray.dir, axis_z, 1.e-6));

    /* Transform the ray origin in camera space */
    d3_sub(pos, ray.org, args.position);
    d33_muld3(pos, world2screen, pos);

    /* Check that the ray origin lies on the image plane */
    CHK(-1 <= pos[0] && pos[0] <= 1);
    CHK(-1 <= pos[1] && pos[1] <= 1);
    CHK(eq_eps(pos[2], 0, 1.e-6));
  }

  CHK(scam_ref_put(cam) == RES_OK);

  logger_release(&logger);
  CHK(mem_allocated_size() == 0);
  return 0;
}
