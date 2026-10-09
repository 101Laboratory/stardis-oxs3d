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

#include "s3d.h"
#include "s3d_backend.h"
#include "s3d_device_c.h"
#include "s3d_instance.h"
#include "s3d_shape_c.h"
#include "s3d_scene_c.h"

#include <rsys/float33.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
instance_release(ref_T* ref)
{
  struct instance* inst;
  struct s3d_scene* scn;
  ASSERT(ref);
  inst = CONTAINER_OF(ref, struct instance, ref);
  scn = inst->scene;
  MEM_RM(scn->dev->allocator, inst);
  S3D(scene_ref_put(scn));
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
instance_create
  (struct s3d_scene* scn,
   struct instance** out_inst)
{
  struct instance* inst = NULL;
  res_T res = RES_OK;
  ASSERT(scn && out_inst);

  inst = (struct instance*)
    MEM_CALLOC(scn->dev->allocator, 1, sizeof(struct instance));
  if(!inst) {
    res = RES_MEM_ERR;
    goto error;
  }
  f33_set_identity(inst->transform); /* rotation */
  f3_splat(inst->transform + 9, 0.f); /* Translation */
  ref_init(&inst->ref);
  S3D(scene_ref_get(scn));
  inst->scene = scn;
exit:
  *out_inst = inst;
  return res;
error:
  if(inst) {
    instance_ref_put(inst);
    inst = NULL;
  }
  goto exit;
}

void
instance_ref_get(struct instance* inst)
{
  ASSERT(inst);
  ref_get(&inst->ref);
}

void
instance_ref_put(struct instance* inst)
{
  ASSERT(inst);
  ref_put(&inst->ref, instance_release);
}

