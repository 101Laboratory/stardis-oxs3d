/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#include "s2d_device_c.h"
#include "s2d_geometry.h"
#include "s2d_line_segments.h"

#include<rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
geometry_release(ref_T* ref)
{
  struct geometry* geom;
  struct s2d_device* dev;

  geom = CONTAINER_OF(ref, struct geometry, ref);
  dev = geom->dev;
  if(geom->lines) line_segments_ref_put(geom->lines);
  MEM_RM(dev->allocator, geom);
  S2D(device_ref_put(dev));
}

/*******************************************************************************
 * Non exported functions
 ******************************************************************************/
res_T
geometry_create(struct s2d_device* dev, struct geometry** out_geom)
{
  struct geometry* geom = NULL;
  res_T res = RES_OK;
  ASSERT(dev && out_geom);

  geom = (struct geometry*)MEM_CALLOC
    (dev->allocator, 1, sizeof(struct geometry));
  if(!geom) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&geom->ref);
  S2D(device_ref_get(dev));
  geom->dev = dev;
  geom->name = S2D_INVALID_ID;
  geom->rtc = NULL;
  geom->rtc_id = RTC_INVALID_GEOMETRY_ID;
  geom->embree_outdated_mask = 0;
  geom->flip_contour = 0;
  geom->is_enabled = 1;
  geom->lines = NULL;

exit:
  *out_geom = geom;
  return res;
error:
  if(geom) {
    geometry_ref_put(geom);
    geom = NULL;
  }
  goto exit;
}

void
geometry_ref_get(struct geometry* geom)
{
  ASSERT(geom);
  ref_get(&geom->ref);
}

void
geometry_ref_put(struct geometry* geom)
{
  ASSERT(geom);
  ref_put(&geom->ref, geometry_release);
}

