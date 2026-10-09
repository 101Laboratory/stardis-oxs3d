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
#include "s3d_c.h"
#include "s3d_device_c.h"
#include "s3d_scene_c.h"
#include "s3d_shape_c.h"

#include <rsys/float33.h>
#include <rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
shape_release(ref_T* ref)
{
  struct s3d_shape* shape;
  struct s3d_device* dev;
  ASSERT(ref);
  shape = CONTAINER_OF(ref, struct s3d_shape, ref);
  dev = shape->dev;

  /* The shape should not be attached */
  if(shape->type != GEOM_NONE) {
    switch(shape->type) {
      case GEOM_MESH:
        if(shape->data.mesh) mesh_ref_put(shape->data.mesh);
        break;
      case GEOM_INSTANCE:
        if(shape->data.instance) instance_ref_put(shape->data.instance);
        break;
      case GEOM_SPHERE:
        if(shape->data.sphere) sphere_ref_put(shape->data.sphere);
        break;
      default: FATAL("Unreachable code \n"); break;
    }
  }
  flist_name_del(&dev->names, shape->id);
  MEM_RM(dev->allocator, shape);
  S3D(device_ref_put(dev));
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
shape_create(struct s3d_device* dev, struct s3d_shape** out_shape)
{
  struct s3d_shape* shape = NULL;
  res_T res = RES_OK;

  if(!dev || !out_shape) {
    res = RES_BAD_ARG;
    goto error;
  }
  shape = (struct s3d_shape*)MEM_CALLOC
    (dev->allocator, 1, sizeof(struct s3d_shape));
  if(!shape) {
    res = RES_MEM_ERR;
    goto error;
  }
  S3D(device_ref_get(dev));
  shape->dev = dev;
  ref_init(&shape->ref);
  shape->id = flist_name_add(&dev->names);
  shape->type = GEOM_NONE;
  shape->is_enabled = 1;
  shape->flip_surface = 0;

exit:
  if(out_shape) *out_shape = shape;
  return res;
error:
  if(shape) {
    S3D(shape_ref_put(shape));
    shape = NULL;
  }
  goto exit;
}

/*******************************************************************************
 * Exported s3d_shape functions
 ******************************************************************************/
res_T
s3d_shape_create_mesh
  (struct s3d_device* dev, struct s3d_shape** out_shape)
{
  struct s3d_shape* shape = NULL;
  res_T res = RES_OK;

  if(!dev || !out_shape) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = shape_create(dev, &shape);
  if(res != RES_OK) goto error;

  shape->type = GEOM_MESH;
  res = mesh_create(dev, &shape->data.mesh);
  if(res != RES_OK) goto error;

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
s3d_shape_create_sphere
  (struct s3d_device* dev, struct s3d_shape** out_shape)
{
  struct s3d_shape* shape = NULL;
  res_T res = RES_OK;

  if(!dev || !out_shape) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = shape_create(dev, &shape);
  if(res != RES_OK) goto error;

  shape->type = GEOM_SPHERE;
  res = sphere_create(dev, &shape->data.sphere);
  if(res != RES_OK) goto error;

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
s3d_shape_ref_get(struct s3d_shape* shape)
{
  if(!shape) return RES_BAD_ARG;
  ref_get(&shape->ref);
  return RES_OK;
}

res_T
s3d_shape_ref_put(struct s3d_shape* shape)
{
  if(!shape) return RES_BAD_ARG;
  ref_put(&shape->ref, shape_release);
  return RES_OK;
}

res_T
s3d_shape_get_id(const struct s3d_shape* shape, unsigned* id)
{
  if(!shape || !id) return RES_BAD_ARG;
  *id = shape->id.index;
  return RES_OK;
}

res_T
s3d_shape_enable(struct s3d_shape* shape, const char enable)
{
  if(!shape) return RES_BAD_ARG;
  shape->is_enabled = enable;
  return RES_OK;
}

res_T
s3d_shape_is_enabled(const struct s3d_shape* shape, char* is_enabled)
{
  if(!shape || !is_enabled) return RES_BAD_ARG;
  *is_enabled = shape->is_enabled;
  return RES_OK;
}

res_T
s3d_shape_flip_surface(struct s3d_shape* shape)
{
  if(!shape) return RES_BAD_ARG;
  shape->flip_surface ^= 1;
  return RES_OK;
}

res_T
s3d_instance_set_position
  (struct s3d_shape* shape, const float position[3])
{
  float axis[3];
  if(!shape || shape->type != GEOM_INSTANCE || !position)
    return RES_BAD_ARG;
  shape->data.instance->transform[9]  =
    f3_dot(f33_row(axis, shape->data.instance->transform, 0), position);
  shape->data.instance->transform[10] =
    f3_dot(f33_row(axis, shape->data.instance->transform, 1), position);
  shape->data.instance->transform[11] =
    f3_dot(f33_row(axis, shape->data.instance->transform, 2), position);
  return RES_OK;
}

res_T
s3d_instance_translate
  (struct s3d_shape* shape,
   const enum s3d_transform_space space,
   const float translation[3])
{
  if(!shape || shape->type != GEOM_INSTANCE || !translation)
    return RES_BAD_ARG;
  if(space == S3D_LOCAL_TRANSFORM) {
    float vec[3];
    f33_mulf3(vec, shape->data.instance->transform, translation);
    f3_add
      (shape->data.instance->transform + 9,
       shape->data.instance->transform + 9,
       vec);
  } else if(space == S3D_WORLD_TRANSFORM) {
    f3_add
      (shape->data.instance->transform + 9,
       shape->data.instance->transform + 9,
       translation);
  } else {
    return RES_BAD_ARG;
  }
  return RES_OK;
}

res_T
s3d_instance_set_transform
  (struct s3d_shape* shape,
   const float transform[12])
{
  if(!shape || shape->type != GEOM_INSTANCE || !transform)
    return RES_BAD_ARG;
  memcpy(shape->data.instance->transform, transform, 12 * sizeof(float));
  return RES_OK;
}

res_T
s3d_instance_transform
  (struct s3d_shape* shape,
   const enum s3d_transform_space space,
   const float transform[12])
{
  if(!shape || shape->type != GEOM_INSTANCE || !transform)
    return RES_BAD_ARG;
  if(space == S3D_LOCAL_TRANSFORM) {
    f33_mulf33(shape->data.instance->transform,
      transform, shape->data.instance->transform);
  } else if(space == S3D_WORLD_TRANSFORM) {
    f33_mulf33(shape->data.instance->transform,
      shape->data.instance->transform, transform);
  } else {
    return RES_BAD_ARG;
  }
  return RES_OK;
}

res_T
s3d_sphere_setup
  (struct s3d_shape* shape,
   const float position[3],
   const float radius)
{
  if(!shape || !position || radius <= 0)
    return RES_BAD_ARG;
  f3_set(shape->data.sphere->pos, position);
  shape->data.sphere->radius = radius;
  return RES_OK;
}

res_T
s3d_sphere_set_hit_filter_function
  (struct s3d_shape* shape,
   s3d_hit_filter_function_T func,
   void* data)
{
  if(!shape || shape->type != GEOM_SPHERE) return RES_BAD_ARG;
  shape->data.sphere->filter.func = func;
  shape->data.sphere->filter.data = data;
  return RES_OK;
}

res_T
s3d_sphere_get_hit_filter_data(struct s3d_shape* shape, void** data)
{
  if(!shape || !data || shape->type != GEOM_SPHERE) return RES_BAD_ARG;
  *data = shape->data.sphere->filter.data;
  return RES_OK;
}

res_T
s3d_mesh_setup_indexed_vertices
  (struct s3d_shape* shape,
   const unsigned ntris,
   void (*get_indices)(const unsigned itri, unsigned ids[3], void* ctx),
   const unsigned nverts,
   struct s3d_vertex_data attribs[],
   const unsigned nattribs,
   void* data)
{
  if(!shape || shape->type != GEOM_MESH)
    return RES_BAD_ARG;
  return mesh_setup_indexed_vertices
    (shape->data.mesh, ntris, get_indices, nverts, attribs, nattribs, data);
}

res_T
s3d_mesh_copy
  (const struct s3d_shape* src,
   struct s3d_shape* dst)
{
  if(!src || !dst || src->type != GEOM_MESH || dst->type != GEOM_MESH)
    return RES_BAD_ARG;
  if(src == dst) return RES_OK;

  dst->flip_surface = src->flip_surface;
  dst->is_enabled = src->is_enabled;
  mesh_copy_indexed_vertices(src->data.mesh, dst->data.mesh);
  return RES_OK;
}

res_T
s3d_mesh_get_vertices_count(const struct s3d_shape* shape, unsigned* nverts)
{
  if(!shape || !nverts || shape->type != GEOM_MESH) return RES_BAD_ARG;
  *nverts = (unsigned)mesh_get_nverts(shape->data.mesh);
  return RES_OK;
}

res_T
s3d_mesh_get_vertex_attrib
  (const struct s3d_shape* shape,
   const unsigned ivert,
   const enum s3d_attrib_usage usage,
   struct s3d_attrib* attrib)
{
  const float* data;
  unsigned i, dim;
  if(!shape
  || shape->type != GEOM_MESH
  || (unsigned)usage >= S3D_ATTRIBS_COUNT__
  || !shape->data.mesh->attribs[usage]
  || !attrib
  || ivert >= (unsigned)mesh_get_nverts(shape->data.mesh))
    return RES_BAD_ARG;

  attrib->usage = usage;
  attrib->type = shape->data.mesh->attribs_type[usage];

  dim = s3d_type_get_dimension(attrib->type);
  data = mesh_get_attr(shape->data.mesh, usage) + ivert * dim;
  FOR_EACH(i, 0, dim) attrib->value[i] = data[i];
  return RES_OK;
}

res_T
s3d_mesh_get_triangles_count(const struct s3d_shape* shape, unsigned* ntris)
{
  if(!shape || !ntris || shape->type != GEOM_MESH) return RES_BAD_ARG;
  *ntris = (unsigned)mesh_get_ntris(shape->data.mesh);
  return RES_OK;
}

res_T
s3d_mesh_get_triangle_indices
  (const struct s3d_shape* shape,
   const unsigned itri,
   unsigned ids[3])
{
  const unsigned* data;
  if(!shape
  || shape->type != GEOM_MESH
  || !ids
  || itri >= (unsigned)mesh_get_ntris(shape->data.mesh))
    return RES_BAD_ARG;

  data = mesh_get_ids(shape->data.mesh) + itri * 3;
  ids[0] = data[0];
  ids[1] = data[1];
  ids[2] = data[2];
  return RES_OK;
}

res_T
s3d_mesh_set_hit_filter_function
  (struct s3d_shape* shape,
   s3d_hit_filter_function_T func,
   void* data)
{
  if(!shape || shape->type != GEOM_MESH) return RES_BAD_ARG;
  shape->data.mesh->filter.func = func;
  shape->data.mesh->filter.data = data;
  return RES_OK;
}

res_T
s3d_mesh_get_hit_filter_data(struct s3d_shape* shape, void** data)
{
  if(!shape || !data || shape->type != GEOM_MESH) return RES_BAD_ARG;
  *data = shape->data.mesh->filter.data;
  return RES_OK;
}

