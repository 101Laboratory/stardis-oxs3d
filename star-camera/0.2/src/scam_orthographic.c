/* Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)
 *
 * Tyhis program is free software: you can redistribute it and/or modify
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

#include "scam_c.h"
#include "scam_log.h"

#include <rsys/double3.h>
#include<rsys/double33.h>
#include <rsys/math.h>

#include <rsys_math.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
setup_orthographic(struct scam* cam, const struct scam_orthographic_args* args)
{
  double x[3], y[3], z[3];
  res_T res = RES_OK;
  ASSERT(cam && args && cam->type == SCAM_ORTHOGRAPHIC);

  cam->param.ortho = ORTHOGRAPHIC_DEFAULT;

  if(args->aspect_ratio <= 0) {
    log_err(cam,
      "orthographic camera: invalid aspect ratio: %g\n",
      args->aspect_ratio);
    res = RES_BAD_ARG;
    goto error;
  }

  if(args->height <= 0) {
    log_err(cam,
      "orthographic camera: invalid height: %g\n",
      args->height);
    res = RES_BAD_ARG;
    goto error;
  }

  if(d3_normalize(z, d3_sub(z, args->target, args->position)) <= 0
  || d3_normalize(x, d3_cross(x, z, args->up)) <= 0
  || d3_normalize(y, d3_cross(y, z, x)) <= 0) {
    log_err(cam,
      "orthographic camera: invalid point of view:\n"
      "  position = %g %g %g\n"
      "  target   = %g %g %g\n"
      "  up       = %g %g %g\n",
      SPLIT3(args->position), SPLIT3(args->target), SPLIT3(args->up));
    res = RES_BAD_ARG;
    goto error;
  }

  cam->param.ortho.height = args->height;
  cam->param.ortho.aspect_ratio = args->aspect_ratio;

  d3_set(cam->param.ortho.position, args->position);
  
  d3_muld(cam->param.ortho.screen2world+0, x, 0.5*args->height*args->aspect_ratio);
  d3_muld(cam->param.ortho.screen2world+3, y, 0.5*args->height);
  d3_set (cam->param.ortho.screen2world+6, z);

  d3_set(cam->param.ortho.camera2world+0, x);
  d3_set(cam->param.ortho.camera2world+3, y);
  d3_set(cam->param.ortho.camera2world+6, z);

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Exported function
 ******************************************************************************/
SCAM_API res_T
scam_create_orthographic
  (struct logger* logger, /* NULL <=> use builtin logger */
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   struct scam_orthographic_args* args,
   struct scam** out_cam)
{
  struct scam* cam = NULL;
  res_T res = RES_OK;

  if(!args || !out_cam) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = camera_create(logger, allocator, verbose, SCAM_ORTHOGRAPHIC, &cam);
  if(res != RES_OK) goto error;
  res = setup_orthographic(cam, args);
  if(res != RES_OK) goto error;

exit:
  if(out_cam) *out_cam = cam;
  return res;
error:
  if(cam) {
    SCAM(ref_put(cam));
    cam = NULL;
  }
  goto exit;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
void
orthographic_generate_ray
  (const struct scam* cam,
   const struct scam_sample* sample,
   struct scam_ray* ray)
{
  double x[3], y[3];
  double pos[3];

  ASSERT(cam && sample && ray);
  ASSERT(cam->type == SCAM_ORTHOGRAPHIC);
  ASSERT(0 <= sample->film[0] && sample->film[0] < 1);
  ASSERT(0 <= sample->film[1] && sample->film[1] < 1);

  /* Transform the sampled position in screen space */
  pos[0] = sample->film[0]*2-1;
  pos[1] = sample->film[1]*2-1;
  pos[2] = 0;

  /* Transform the sampled position in world space */
  d3_muld(x, cam->param.ortho.screen2world+0, pos[0]);
  d3_muld(y, cam->param.ortho.screen2world+3, pos[1]);
  d3_add(ray->org, x, y);
  d3_add(ray->org, ray->org, cam->param.ortho.position);

  /* Setup the ray direction to the image plane normal, i.e. the z axis of the
   * camera */
  d3_set(ray->dir, cam->param.ortho.camera2world+6);
}
