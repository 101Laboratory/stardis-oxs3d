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
check_perspective_args
  (struct scam* cam,
   const struct scam_perspective_args* args)
{
  if(!args) return RES_BAD_ARG;

  /* Invalid aspect ratio */
  if(args->aspect_ratio <= 0) {
    log_err(cam,"perspective camera: invalid aspect ratio: %g\n",
      args->aspect_ratio);
    return RES_BAD_ARG;
  }

  /* Invalid lens radius */
  if(args->lens_radius < 0) {
    log_err(cam,"perspective camera: invalid negative lens radius: %g\n",
      args->lens_radius);
    return RES_BAD_ARG;
  }

  /* Invalid focal distance */
  if(args->lens_radius > 0 && args->focal_distance < 0) {
    log_err(cam, "perspective camera: invalid negative focal distance: %g\n",
      args->focal_distance);
    return RES_BAD_ARG;
  }

  /* Invalid field of view */
  if(args->field_of_view <= 0 || args->field_of_view >= PI) {
    log_err(cam, "perspective camera: invalid vertical field of view: %g\n",
      args->field_of_view);
    return RES_BAD_ARG;
  }

  return RES_OK;
}

static res_T
setup_perspective(struct scam* cam, const struct scam_perspective_args* args)
{
  double hfov; /* Horizotal field of view */
  double half_vfov; /* (Vertical vield of view) / 2 */
  double tan_half_vfov;
  double x[3], y[3], z[3];
  res_T res = RES_OK;
  ASSERT(cam && args && cam->type == SCAM_PERSPECTIVE);

  cam->param.persp = PERSPECTIVE_DEFAULT;

  if(d3_normalize(z, d3_sub(z, args->target, args->position)) <= 0
  || d3_normalize(x, d3_cross(x, z, args->up)) <= 0
  || d3_normalize(y, d3_cross(y, z, x)) <= 0) {
    log_err(cam,
      "perspective camera: invalid point of view:\n"
      "  position = %g %g %g\n"
      "  target   = %g %g %g\n"
      "  up       = %g %g %g\n",
      SPLIT3(args->position), SPLIT3(args->target), SPLIT3(args->up));
    res = RES_BAD_ARG;
    goto error;
  }

  half_vfov = args->field_of_view*0.5;
  tan_half_vfov = tan(half_vfov);

  cam->param.persp.rcp_tan_half_fov = 1.0/tan_half_vfov;
  cam->param.persp.aspect_ratio = args->aspect_ratio;
  cam->param.persp.lens_radius = args->lens_radius;
  cam->param.persp.focal_distance = args->focal_distance;

  d3_set(cam->param.persp.position, args->position);

  d3_muld(cam->param.persp.screen2world+0, x, args->aspect_ratio);
  d3_set (cam->param.persp.screen2world+3, y);
  d3_muld(cam->param.persp.screen2world+6, z, cam->param.persp.rcp_tan_half_fov);

  d3_set(cam->param.persp.camera2world+0, x);
  d3_set(cam->param.persp.camera2world+3, y);
  d3_set(cam->param.persp.camera2world+6, z);

  /* Compute the solid angle of the camera */
  hfov = 2*atan(args->aspect_ratio * tan_half_vfov);
  cam->param.persp.solid_angle = hfov * 2*sin(half_vfov);

exit:
  return res;
error:
  goto exit;
}

static INLINE void
pinhole_generate_ray
  (const struct scam* cam,
   const struct scam_sample* sample,
   struct scam_ray* ray)
{
  double x[3], y[3], z[3], len;
  double pos[3];
  (void)len;

  ASSERT(cam && sample && ray);
  ASSERT(cam->param.persp.lens_radius == 0);
  ASSERT(cam->type == SCAM_PERSPECTIVE);
  ASSERT(0 <= sample->film[0] && sample->film[0] < 1);
  ASSERT(0 <= sample->film[1] && sample->film[1] < 1);

  /* Transform the sampled position in screen space */
  pos[0] = sample->film[0]*2-1;
  pos[1] = sample->film[1]*2-1;
  pos[2] = 1;

  /* Transform the sampled position in world space. Note that no translation is
   * performed to directly obtain the (un-normalized) ray direction. */
  d3_muld(x, cam->param.persp.screen2world+0, pos[0]);
  d3_muld(y, cam->param.persp.screen2world+3, pos[1]);
  d3_set (z, cam->param.persp.screen2world+6);
  d3_add(ray->dir, x, y);
  d3_add(ray->dir, ray->dir, z);
  len = d3_normalize(ray->dir, ray->dir);
  ASSERT(len >= 1.e-6);

  /* Setup the ray origin */
  d3_set(ray->org, cam->param.persp.position);
}

static INLINE void
thin_lens_generate_ray
  (const struct scam* cam,
   const struct scam_sample* sample,
   struct scam_ray* ray)
{
  double focus_pt[3];
  double dir[3];
  double theta;
  double len;
  double t;
  double r;
  (void)len;

  ASSERT(cam && sample && ray);
  ASSERT(cam->param.persp.lens_radius > 0);
  ASSERT(cam->type == SCAM_PERSPECTIVE);
  ASSERT(0 <= sample->film[0] && sample->film[0] < 1);
  ASSERT(0 <= sample->film[1] && sample->film[1] < 1);
  ASSERT(0 <= sample->lens[0] && sample->lens[0] < 1);
  ASSERT(0 <= sample->lens[1] && sample->lens[1] < 1);

  /* Transform the sampled position in screen space and use it as the
   * (un-normalized) direction starting from the lens center and intersecting
   * the sample */
  dir[0] = sample->film[0]*2-1;
  dir[1] = sample->film[1]*2-1;
  dir[2] = 1;

  /* Transform the sampled direction in camera space */
  dir[0] = dir[0] * cam->param.persp.aspect_ratio;
  dir[1] = dir[1];
  dir[2] = dir[2] * cam->param.persp.rcp_tan_half_fov;
  len = d3_normalize(dir, dir);
  ASSERT(len >= 1.e-6);

  /* find the focus point by intersecting dir with the focus plane */
  t = cam->param.persp.focal_distance / dir[2];
  focus_pt[0] = /* null ray origin + */ t*dir[0];
  focus_pt[1] = /* null ray origin + */ t*dir[1];
  focus_pt[2] = /* null ray origin + */ t*dir[2];

  /* Uniformly sample a position onto the lens in camera space */
  theta = 2 * PI * sample->lens[0];
  r = cam->param.persp.lens_radius * sqrt(sample->lens[1]);
  ray->org[0] = r * cos(theta);
  ray->org[1] = r * sin(theta);
  ray->org[2] = 0;

  /* Compute the ray direction in camera space */
  d3_sub(ray->dir, focus_pt, ray->org);
  len = d3_normalize(ray->dir, ray->dir);
  ASSERT(len >= 1.e-6);

  /* Transform the ray from camera space to world space */
  d33_muld3(ray->dir, cam->param.persp.camera2world, ray->dir);
  d33_muld3(ray->org, cam->param.persp.camera2world, ray->org);
  d3_add(ray->org, cam->param.persp.position, ray->org);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
SCAM_API res_T
scam_create_perspective
  (struct logger* logger, /* NULL <=> use builtin logger */
   struct mem_allocator* allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   struct scam_perspective_args* args,
   struct scam** out_cam)
{
  struct scam* cam = NULL;
  res_T res = RES_OK;

  if(!args || !out_cam) {
    res = RES_BAD_ARG;
    goto error;
  }
  res = camera_create(logger, allocator, verbose, SCAM_PERSPECTIVE, &cam);
  if(res != RES_OK) goto error;
  res = check_perspective_args(cam, args);
  if(res != RES_OK) goto error;
  res = setup_perspective(cam, args);
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

res_T
scam_perspective_get_solid_angle(const struct scam* camera, double* solid_angle)
{
  if(!camera || camera->type != SCAM_PERSPECTIVE || !solid_angle)
    return RES_BAD_ARG;

  *solid_angle = camera->param.persp.solid_angle;
  return RES_OK;
}

res_T
scam_focal_length_to_field_of_view
  (const double lens_radius,
   const double focal_length,
   double* field_of_view)
{
  if(lens_radius < 0 || focal_length <= 0 || !field_of_view)
    return RES_BAD_ARG;
  *field_of_view = 2 * atan(lens_radius /focal_length);
  return RES_OK;
}

res_T
scam_field_of_view_to_focal_length
  (const double lens_radius,
   const double field_of_view,
   double* focal_length)
{
  if(lens_radius < 0 || field_of_view <= 0 || field_of_view >= PI || !focal_length)
    return RES_BAD_ARG;
  *focal_length = lens_radius / tan(field_of_view*0.5);
  return RES_OK;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
void
perspective_generate_ray
  (const struct scam* cam,
   const struct scam_sample* sample,
   struct scam_ray* ray)
{
  ASSERT(cam && cam->type == SCAM_PERSPECTIVE);
  if(cam->param.persp.lens_radius == 0) {
    pinhole_generate_ray(cam, sample, ray);
  } else {
    thin_lens_generate_ray(cam, sample, ray);
  }
}
