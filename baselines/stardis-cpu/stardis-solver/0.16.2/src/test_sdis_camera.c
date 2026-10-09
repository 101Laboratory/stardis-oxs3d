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
#include "test_sdis_utils.h"

#include <rsys_math.h>

int
main(int argc, char** argv)
{
  struct sdis_device* dev;
  struct sdis_camera* cam;
  double pos[3] = {0};
  double tgt[3] = {0};
  double up[3] = {0};
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  BA(sdis_camera_create(NULL, NULL));
  BA(sdis_camera_create(dev, NULL));
  BA(sdis_camera_create(NULL, &cam));
  OK(sdis_camera_create(dev, &cam));

  BA(sdis_camera_ref_get(NULL));
  OK(sdis_camera_ref_get(cam));
  BA(sdis_camera_ref_put(NULL));
  OK(sdis_camera_ref_put(cam));
  OK(sdis_camera_ref_put(cam));

  OK(sdis_camera_create(dev, &cam));
  BA(sdis_camera_set_proj_ratio(NULL, 0));
  BA(sdis_camera_set_proj_ratio(cam, 0));
  BA(sdis_camera_set_proj_ratio(NULL, 4.0 / 3.0));
  OK(sdis_camera_set_proj_ratio(cam, 4.0/3.0));
  BA(sdis_camera_set_proj_ratio(cam, -4.0/3.0));

  BA(sdis_camera_set_fov(NULL, 0));
  BA(sdis_camera_set_fov(cam, 0));
  BA(sdis_camera_set_fov(NULL, PI/4.0));
  OK(sdis_camera_set_fov(cam, PI/4.0));
  BA(sdis_camera_set_fov(cam, -PI/4.0));

  pos[0] = 0, pos[1] = 0, pos[2] = 0;
  tgt[0] = 0, tgt[1] = 0, tgt[2] = -1;
  up[0] = 0, up[1] = 1, up[2] = 0;
  BA(sdis_camera_look_at(NULL, NULL, NULL, NULL));
  BA(sdis_camera_look_at(cam, NULL, NULL, NULL));
  BA(sdis_camera_look_at(NULL, pos, NULL, NULL));
  BA(sdis_camera_look_at(cam, pos, NULL, NULL));
  BA(sdis_camera_look_at(NULL, NULL, tgt, NULL));
  BA(sdis_camera_look_at(cam, NULL, tgt, NULL));
  BA(sdis_camera_look_at(NULL, pos, tgt, NULL));
  BA(sdis_camera_look_at(cam, pos, tgt, NULL));
  BA(sdis_camera_look_at(NULL, NULL, NULL, up));
  BA(sdis_camera_look_at(cam, NULL, NULL, up));
  BA(sdis_camera_look_at(NULL, pos, NULL, up));
  BA(sdis_camera_look_at(cam, pos, NULL, up));
  BA(sdis_camera_look_at(NULL, NULL, tgt, up));
  BA(sdis_camera_look_at(cam, NULL, tgt, up));
  BA(sdis_camera_look_at(NULL, pos, tgt, up));
  OK(sdis_camera_look_at(cam, pos, tgt, up));
  tgt[0] = 0, tgt[1] = 0, tgt[2] = 0;
  BA(sdis_camera_look_at(cam, pos, tgt, up));
  tgt[0] = 0, tgt[1] = 0, tgt[2] = -1;
  up[0] = 0, up[1] = 0, up[2] = 0;
  BA(sdis_camera_look_at(cam, pos, tgt, up));

  OK(sdis_device_ref_put(dev));
  OK(sdis_camera_ref_put(cam));

  CHK(mem_allocated_size() == 0);
  return 0;
}

