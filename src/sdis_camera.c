/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#include "sdis.h"
#include "sdis_camera.h"
#include "sdis_device_c.h"

#include <rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
camera_release(ref_T* ref)
{
  struct sdis_camera* cam = CONTAINER_OF(ref, struct sdis_camera, ref);
  struct sdis_device* dev;
  ASSERT(ref);
  dev = cam->dev;
  MEM_RM(dev->allocator, cam);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_camera_create(struct sdis_device* dev, struct sdis_camera** out_cam)
{
  const double pos[3] = {0, 0, 0};
  const double tgt[3] = {0, 0,-1};
  const double up[3] = {0, 1, 0};
  struct sdis_camera* cam = NULL;
  res_T res = RES_OK;

  if(!dev || !out_cam) {
    res = RES_BAD_ARG;
    goto error;
  }

  cam = MEM_CALLOC(dev->allocator, 1, sizeof(struct sdis_camera));
  if(!cam) {
    res = RES_MEM_ERR;
    goto error;
  }
  SDIS(device_ref_get(dev));
  cam->dev = dev;
  ref_init(&cam->ref);
  cam->rcp_proj_ratio = 1.0;
  cam->fov_x = PI/2.0;
  SDIS(camera_look_at(cam, pos, tgt, up));

exit:
  if(out_cam) *out_cam = cam;
  return res;
error:
  if(cam) {
    SDIS(camera_ref_put(cam));
    cam = NULL;
  }
  goto exit;
}

res_T
sdis_camera_ref_get(struct sdis_camera* cam)
{
  if(!cam) return RES_BAD_ARG;
  ref_get(&cam->ref);
  return RES_OK;
}

res_T
sdis_camera_ref_put(struct sdis_camera* cam)
{
  if(!cam) return RES_BAD_ARG;
  ref_put(&cam->ref, camera_release);
  return RES_OK;
}

res_T
sdis_camera_set_proj_ratio(struct sdis_camera* cam, const double ratio)
{
  double y[3] = {0};
  if(!cam || ratio <= 0) return RES_BAD_ARG;
  if(d3_normalize(y, cam->axis_y) <= 0) return RES_BAD_ARG;
  cam->rcp_proj_ratio = 1.0 / ratio;
  d3_muld(cam->axis_y, y, cam->rcp_proj_ratio);
  return RES_OK;
}

res_T
sdis_camera_set_fov(struct sdis_camera* cam, const double fov_x)
{
  double z[3] = {0};
  double img_plane_depth;
  if(!cam || (float)fov_x <= 0) return RES_BAD_ARG;
  if(d3_normalize(z, cam->axis_z) <= 0) return RES_BAD_ARG;
  img_plane_depth = 1.0/tan(fov_x*0.5);
  d3_muld(cam->axis_z, z, img_plane_depth);
  cam->fov_x = fov_x;
  return RES_OK;
}

res_T
sdis_camera_look_at
  (struct sdis_camera* cam,
   const double pos[3],
   const double tgt[3],
   const double up[3])
{
  double x[3], y[3], z[3];
  double img_plane_depth;
  if(!cam || !pos || !tgt || !up) return RES_BAD_ARG;

  if(d3_normalize(z, d3_sub(z, tgt, pos)) <= 0) return RES_BAD_ARG;
  if(d3_normalize(x, d3_cross(x, z, up)) <= 0) return RES_BAD_ARG;
  if(d3_normalize(y, d3_cross(y, z, x)) <= 0) return RES_BAD_ARG;
  img_plane_depth = 1.0/tan(cam->fov_x*0.5);

  d3_set(cam->axis_x, x);
  d3_muld(cam->axis_y, y, cam->rcp_proj_ratio);
  d3_muld(cam->axis_z, z, img_plane_depth);
  d3_set(cam->position, pos);
  return RES_OK;
}

