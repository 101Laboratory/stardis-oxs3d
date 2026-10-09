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
#include "s3d_device_c.h"
#include "s3d_scene_c.h"
#include "s3d_scene_view_c.h"
#include "s3d_shape_c.h"

#include <rsys/list.h>
#include <rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
scene_release(ref_T* ref)
{
  struct s3d_scene* scn;
  struct s3d_device* dev;
  struct list_node* node;
  struct list_node* tmp;

  ASSERT(ref);
  scn = CONTAINER_OF(ref, struct s3d_scene, ref);
  dev = scn->dev;
  LIST_FOR_EACH_SAFE(node, tmp, &scn->scnviews) {
    scene_view_destroy(CONTAINER_OF(node, struct s3d_scene_view, node));
  }
  S3D(scene_clear(scn));
  htable_shape_release(&scn->shapes);
  MEM_RM(dev->allocator, scn);
  S3D(device_ref_put(dev));
}

/*******************************************************************************
 * Exported s3d_scene functions
 ******************************************************************************/
res_T
s3d_scene_create(struct s3d_device* dev, struct s3d_scene** out_scn)
{
  struct s3d_scene* scn = NULL;
  res_T res = RES_OK;

  if(!dev || !out_scn) {
    res = RES_BAD_ARG;
    goto error;
  }
  scn = (struct s3d_scene*)MEM_CALLOC
    (dev->allocator, 1, sizeof(struct s3d_scene));
  if(!scn) {
    res = RES_MEM_ERR;
    goto error;
  }
  htable_shape_init(dev->allocator, &scn->shapes);
  SIG_INIT(&scn->sig_shape_detach);
  list_init(&scn->scnviews);
  ref_init(&scn->ref);
  S3D(device_ref_get(dev));
  scn->dev = dev;

exit:
  if(out_scn) *out_scn = scn;
  return res;
error:
  if(scn) {
    S3D(scene_ref_put(scn));
    scn = NULL;
  }
  goto exit;
}

res_T
s3d_scene_ref_get(struct s3d_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_get(&scn->ref);
  return RES_OK;
}

res_T
s3d_scene_ref_put(struct s3d_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_put(&scn->ref, scene_release);
  return RES_OK;
}

res_T
s3d_scene_instantiate(struct s3d_scene* scn, struct s3d_shape** out_shape)
{
  struct s3d_shape* shape = NULL;
  res_T res = RES_OK;

  if(!scn || !out_shape) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = shape_create(scn->dev, &shape);
  if(res != RES_OK)
    goto error;

  shape->type = GEOM_INSTANCE;
  res = instance_create(scn, &shape->data.instance);
  if(res != RES_OK)
    goto error;

exit:
  if(out_shape)
    *out_shape = shape;
  return res;
error:
  if(shape) {
    S3D(shape_ref_put(shape));
    shape = NULL;
  }
  goto exit;
}

res_T
s3d_scene_attach_shape(struct s3d_scene* scn, struct s3d_shape* shape)
{
  unsigned shape_id;
  res_T res = RES_OK;

  if(!scn || !shape)
    return RES_BAD_ARG;
  if(shape->type == GEOM_INSTANCE && shape->data.instance->scene == scn) {
    log_error(scn->dev,
      "%s: the instantiated scene cannot be attached to itself.\n", FUNC_NAME);
    return RES_BAD_ARG;
  }

  S3D(shape_get_id(shape, &shape_id));
  if(htable_shape_find(&scn->shapes, &shape_id) != NULL) {
    log_warning(scn->dev,
      "%s: the shape is already attached to the scene.\n", FUNC_NAME);
    return RES_OK;
  }

  res = htable_shape_set(&scn->shapes, &shape_id, &shape);
  if(res != RES_OK) {
    log_error(scn->dev,
      "%s: cannot attach the shape to the scene.\n", FUNC_NAME);
    return RES_OK;
  }
  S3D(shape_ref_get(shape));
  scn->instances_count += shape->type == GEOM_INSTANCE;
  return RES_OK;
}

res_T
s3d_scene_detach_shape(struct s3d_scene* scn, struct s3d_shape* shape)
{
  size_t n;
  unsigned shape_id;

  if(!scn || !shape) return RES_BAD_ARG;

  S3D(shape_get_id(shape, &shape_id));
  if(htable_shape_find(&scn->shapes, &shape_id) == NULL) {
    log_error(scn->dev,
      "%s: the shape is not attached to the scene.\n", FUNC_NAME);
    return RES_BAD_ARG;
  }

  n = htable_shape_erase(&scn->shapes, &shape_id);
  ASSERT(n == 1); (void)n;

  SIG_BROADCAST(&scn->sig_shape_detach, scene_shape_cb_T, ARG2(scn, shape));

  S3D(shape_ref_put(shape));
  return RES_OK;
}

res_T
s3d_scene_clear(struct s3d_scene* scn)
{
  struct htable_shape_iterator it, end;

  if(!scn) return RES_BAD_ARG;

  htable_shape_begin(&scn->shapes, &it);
  htable_shape_end(&scn->shapes, &end);
  while(!htable_shape_iterator_eq(&it, &end)) {
    struct s3d_shape** pshape = htable_shape_iterator_data_get(&it);
    struct s3d_shape* shape = *pshape;
    SIG_BROADCAST(&scn->sig_shape_detach, scene_shape_cb_T, ARG2(scn, shape));
    S3D(shape_ref_put(shape));
    htable_shape_iterator_next(&it);
  }
  htable_shape_clear(&scn->shapes);

  return RES_OK;
}

res_T
s3d_scene_get_device(struct s3d_scene* scn, struct s3d_device** dev)
{
  if(!scn || !dev) return RES_BAD_ARG;
  *dev = scn->dev;
  return RES_OK;
}

res_T
s3d_scene_get_shapes_count(struct s3d_scene* scn, size_t* nshapes)
{
  if(!scn || !nshapes) return RES_BAD_ARG;
  *nshapes = htable_shape_size_get(&scn->shapes);
  return RES_OK;
}
