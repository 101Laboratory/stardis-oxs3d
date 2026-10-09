/* Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)
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

#include "s3d_device_c.h"
#include "s3d_sphere.h"

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
sphere_release(ref_T* ref)
{
  struct sphere* sphre;
  struct s3d_device* dev;
  ASSERT(ref);

  sphre = CONTAINER_OF(ref, struct sphere, ref);
  dev = sphre->dev;
  MEM_RM(dev->allocator, sphre);
  S3D(device_ref_put(dev));
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
sphere_create(struct s3d_device* dev, struct sphere** out_sphere)
{
  struct sphere* sphere = NULL;
  res_T res = RES_OK;
  ASSERT(dev && out_sphere);

  sphere = (struct sphere*)MEM_CALLOC(dev->allocator, 1, sizeof(struct sphere));
  if(!sphere) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&sphere->ref);
  S3D(device_ref_get(dev));
  sphere->dev = dev;
  sphere->radius = -1;

exit:
  *out_sphere = sphere;
  return res;
error:
  if(sphere) {
    sphere_ref_put(sphere);
    sphere = NULL;
  }
  goto exit;
}

void
sphere_ref_get(struct sphere* sphere)
{
  ASSERT(sphere);
  ref_get(&sphere->ref);
}

void
sphere_ref_put(struct sphere* sphere)
{
  ASSERT(sphere);
  ref_put(&sphere->ref, sphere_release);
}

