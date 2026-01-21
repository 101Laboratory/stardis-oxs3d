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

#include "s3d_c.h"
#include "s3d_device_c.h"
#include "s3d_instance.h"
#include "s3d_mesh.h"
#include "s3d_scene_c.h"
#include "s3d_sphere.h"

#include <rsys/float33.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
mesh_get_primitive_attrib
  (const struct geometry* geom,
   const float* transform, /* Can be NULL => no transform */
   const char flip_surface,
   const struct s3d_primitive* prim,
   const enum s3d_attrib_usage usage,
   const float uv[2],
   struct s3d_attrib* attrib)
{
  const uint32_t* ids;
  float w;
  res_T res = RES_OK;
  ASSERT(geom && geom->type == GEOM_MESH && prim && prim->shape__ == geom);
  ASSERT(uv && attrib);

  /* Unormalized barycentric coordinates */
  w = CLAMP(1.f - uv[0] - uv[1], 0.f, 1.f);
  if(uv[0] < 0.f || uv[1] < 0.f || uv[0] > 1.f || uv[1] > 1.f
  || !eq_epsf(w + uv[0] + uv[1], 1.f, 1.e-3f)) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* The mesh haven't the required mesh attrib */
  if(usage != S3D_GEOMETRY_NORMAL && !geom->data.mesh->attribs[usage]) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Out of bound primitive index */
  if(prim->prim_id >= mesh_get_ntris(geom->data.mesh)) {
    res = RES_BAD_ARG;
    goto error;
  }
  ids = mesh_get_ids(geom->data.mesh) + prim->prim_id * 3/*#triangle ids*/;
  attrib->usage = usage;

  if(usage == S3D_POSITION || usage == S3D_GEOMETRY_NORMAL) {
    const float* v0, *v1, *v2;
    const float* pos;
    attrib->type = S3D_FLOAT3;
    /* Fetch data */
    pos = mesh_get_pos(geom->data.mesh);
    v0 = pos + ids[0] * 3;
    v1 = pos + ids[1] * 3;
    v2 = pos + ids[2] * 3;
    if(usage == S3D_GEOMETRY_NORMAL) { /* Compute the geometry normal */
      float e0[3], e1[3];
      /* Build the geometric normal with respect to surface orientation.
       * Default is Clock Wise */
      f3_sub(e0, v2, v0);
      f3_sub(e1, v1, v0);
      if(flip_surface) {
        f3_cross(attrib->value, e1, e0);
      } else {
        f3_cross(attrib->value, e0, e1);
      }
      if(transform) { /* Transform the normal from local to world space */
        float transform_invtrans[9];
        f33_invtrans(transform_invtrans, transform);
        f33_mulf3(attrib->value, transform_invtrans, attrib->value);
      }
    } else { /* Interpolate the vertex position */
      float tmp[3];
      f3_mulf(attrib->value, v0, uv[0]);
      f3_add(attrib->value, attrib->value, f3_mulf(tmp, v1, uv[1]));
      f3_add(attrib->value, attrib->value, f3_mulf(tmp, v2, w));
      if(transform) { /* Transform the position from local to world space */
        f33_mulf3(attrib->value, transform, attrib->value); /* Rotation */
        f3_add(attrib->value, attrib->value, transform + 9); /* Translation */
      }
    }
  } else {
    const float* attr;
    const float* v0, *v1, *v2;
    unsigned i, dim;
    attrib->type = geom->data.mesh->attribs_type[usage];
    /* Fetch attrib data */
    dim = s3d_type_get_dimension(attrib->type);
    attr = mesh_get_attr(geom->data.mesh, usage);
    v0 = attr + ids[0] * dim;
    v1 = attr + ids[1] * dim;
    v2 = attr + ids[2] * dim;
    /* Interpolate the vertex attribs */
    ASSERT(dim <= 4);
    FOR_EACH(i, 0, dim) {
      attrib->value[i] = v0[i]*uv[0] + v1[i]*uv[1] + v2[i]*w;
    }
  }
exit:
  return res;
error:
  goto exit;
}

static res_T
sphere_get_attrib
  (const struct geometry* geom,
   const float* transform, /* Can be NULL => no transform */
   const char flip_surface,
   const enum s3d_attrib_usage usage,
   const float uv[2],
   struct s3d_attrib* attrib)
{
  res_T res = RES_OK;
  double phi, cos_theta, sin_theta;
  float P[3];
  float N[3];
  ASSERT(geom && geom->type == GEOM_SPHERE);
  ASSERT(uv && attrib);

  /* Only position and geometry normal are valid sphere attribs */
  if(usage != S3D_GEOMETRY_NORMAL && usage != S3D_POSITION) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Compute the sampled position on the unit sphere that is actually equal to
   * the normal at this position. */
  phi = uv[0] * 2*PI;
  cos_theta = 1 - 2 * uv[1];
  sin_theta = 2 * sqrtf(uv[1] * (1 - uv[1]));
  N[0] = (float)(cos(phi) * sin_theta);
  N[1] = (float)(sin(phi) * sin_theta);
  N[2] = (float)cos_theta;

  if(usage == S3D_GEOMETRY_NORMAL) {
    if(flip_surface) f3_minus(N, N);
    if(transform) { /* Transform the normal from local to world space */
      float invtrans[9];
      f33_invtrans(invtrans, transform);
      f33_mulf3(attrib->value, invtrans, N);
    }
    f3_set(attrib->value, N);
  } else {
    ASSERT(usage == S3D_POSITION);
    /* Compute the sampled position in local space */
    f3_mulf(P, N, geom->data.sphere->radius);
    f3_add(P, P, geom->data.sphere->pos);
    if(transform) { /* Transform the position from local to world space */
      f33_mulf3(P, transform, P); /* Affine */
      f3_add(P, P, transform + 9); /* Linear */
    }
    f3_set(attrib->value, P);
  }

exit:
  return res;
error:
  goto exit;
}

static int
check_primitive(const struct s3d_primitive* prim)
{
  return prim
      && prim->geom_id != S3D_INVALID_ID
      && prim->prim_id != S3D_INVALID_ID
      && prim->shape__ != NULL
      && (prim->inst_id != S3D_INVALID_ID || prim->inst__ == NULL);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s3d_primitive_get_attrib
  (const struct s3d_primitive* prim,
   const enum s3d_attrib_usage usage,
   const float uv[2],
   struct s3d_attrib* attrib)
{
  struct geometry* geom_shape = NULL;
  const float* transform = NULL;
  char flip_surface = 0;
  res_T res = RES_OK;

  if(!check_primitive(prim) || usage == S3D_ATTRIBS_COUNT__ || !uv || !attrib) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(prim->inst__ == NULL) {
    geom_shape = (struct geometry*)prim->shape__;
    flip_surface = geom_shape->flip_surface;
  } else {
    const struct geometry* geom_inst = (const struct geometry*)prim->inst__;
    ASSERT(geom_inst->type == GEOM_INSTANCE);
    ASSERT(prim->inst_id == geom_inst->name);
    geom_shape = (struct geometry*)prim->shape__;
    transform = geom_inst->data.instance->transform;
    ASSERT(geom_shape);
    flip_surface = geom_inst->flip_surface ^ geom_shape->flip_surface;
  }
  ASSERT(prim->geom_id == geom_shape->name);

  if(geom_shape->type == GEOM_SPHERE) {
    res = sphere_get_attrib
      (geom_shape, transform, flip_surface, usage, uv, attrib);
  } else {
    ASSERT(geom_shape->type == GEOM_MESH);
    res = mesh_get_primitive_attrib
      (geom_shape, transform, flip_surface, prim, usage, uv, attrib);
  }
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}

res_T
s3d_primitive_has_attrib
  (const struct s3d_primitive* prim,
   const enum s3d_attrib_usage attr,
   char* has_attrib)
{
  if(!check_primitive(prim) || !has_attrib
  || (attr != S3D_GEOMETRY_NORMAL && (unsigned)attr >= S3D_ATTRIBS_COUNT__))
    return RES_BAD_ARG;

  if(attr == S3D_GEOMETRY_NORMAL) {
    *has_attrib = 1;
  } else {
    struct geometry* geom_shape = (struct geometry*)prim->shape__;
    if(geom_shape->type == GEOM_MESH) {
      *has_attrib = geom_shape->data.mesh->attribs[attr] != NULL;
    } else {
      *has_attrib = 0;
    }
  }
  return RES_OK;
}

res_T
s3d_primitive_sample
  (const struct s3d_primitive *prim,
   const float u,
   const float v,
   float st[2])
{
  struct geometry* geom_shape;
  double sqrt_u;

  if(!check_primitive(prim) || !st)
    return RES_BAD_ARG;

  /* Expecting canonic numbers */
  if(u < 0.f || u >= 1.f || v < 0.f || v >= 1.f)
    return RES_BAD_ARG;

  geom_shape = (struct geometry*)prim->shape__;
  switch(geom_shape->type) {
    case GEOM_MESH:
      /* Triangular primitive */
      sqrt_u = sqrt(u);
      st[0] = (float)(1.0 - sqrt_u);
      st[1] = (float)(v * sqrt_u);
      break;
    case GEOM_SPHERE:
      st[0] = u;
      st[1] = v;
      break;
    default: FATAL("Unreachable code\n"); break;
  }
  return RES_OK;
}

res_T
s3d_primitive_compute_area(const struct s3d_primitive* prim, float* area)
{
  struct geometry* geom;

  if(!check_primitive(prim) || !area)
    return RES_BAD_ARG;

  geom = (struct geometry*)prim->shape__;
  if(geom->type == GEOM_SPHERE) {
    *area = sphere_compute_area(geom->data.sphere);
  } else if(geom->type == GEOM_MESH) {
    const uint32_t* ids;
    const float* pos;
    const float* v0, *v1, *v2;
    float E0[3], E1[3], N[3];

    pos = mesh_get_pos(geom->data.mesh);
    ids = mesh_get_ids(geom->data.mesh) + prim->prim_id * 3/* #triangle ids */;
    v0 = pos + ids[0] * 3/* #coords */;
    v1 = pos + ids[1] * 3/* #coords */;
    v2 = pos + ids[2] * 3/* #coords */;
    f3_sub(E0, v1, v0);
    f3_sub(E1, v2, v0);
    *area = f3_len(f3_cross(N, E0, E1)) * 0.5f;
  } else {
    FATAL("Unreachable code\n");
  }
  return RES_OK;
}

res_T
s3d_primitive_get_transform
  (const struct s3d_primitive* prim, float transform[12])
{
  if(!check_primitive(prim) || !transform)
    return RES_BAD_ARG;

  if(!prim->inst__) {
    f3(transform + 0, 1.f, 0.f, 0.f);
    f3(transform + 3, 0.f, 1.f, 0.f);
    f3(transform + 6, 0.f, 0.f, 1.f);
    f3(transform + 9, 0.f, 0.f, 0.f);
  } else {
    struct geometry* geom_inst = (struct geometry*)prim->inst__;
    ASSERT(geom_inst->type == GEOM_INSTANCE);
    f3_set(transform + 0, geom_inst->data.instance->transform + 0);
    f3_set(transform + 3, geom_inst->data.instance->transform + 3);
    f3_set(transform + 6, geom_inst->data.instance->transform + 6);
    f3_set(transform + 9, geom_inst->data.instance->transform + 9);
  }
  return RES_OK;
}

res_T
s3d_triangle_get_vertex_attrib
  (const struct s3d_primitive* prim,
   const size_t ivertex,
   const enum s3d_attrib_usage usage,
   struct s3d_attrib* attrib)
{
  struct geometry* geom_shape = NULL;
  const float* transform = NULL;
  const uint32_t* ids;

  if(!check_primitive(prim) || ivertex > 2
  || (unsigned)usage >=  S3D_ATTRIBS_COUNT__
  || !attrib) {
    return RES_BAD_ARG;
  }

  geom_shape = (struct geometry*)prim->shape__;
  ASSERT(prim->geom_id == geom_shape->name);

  if(geom_shape->type != GEOM_MESH)
    return RES_BAD_ARG;

  if(prim->inst__ != NULL) {
    const struct geometry* geom_inst = (const struct geometry*)prim->inst__;
    ASSERT(geom_inst->type == GEOM_INSTANCE);
    ASSERT(prim->inst_id == geom_inst->name);
    transform = geom_inst->data.instance->transform;
  }

  /* The mesh haven't the required mesh attrib */
  if(!geom_shape->data.mesh->attribs[usage]) {
    return RES_BAD_ARG;
  }

  /* Out of bound primitive index */
  if(prim->prim_id >= mesh_get_ntris(geom_shape->data.mesh)) {
    return RES_BAD_ARG;
  }
  ids = mesh_get_ids(geom_shape->data.mesh) + prim->prim_id * 3/*#triangle ids*/;
  attrib->usage = usage;

  if(usage != S3D_POSITION) {
    const float* attr;
    unsigned i, dim;
    attrib->type = geom_shape->data.mesh->attribs_type[usage];
    /* Fetch attrib data */
    dim = s3d_type_get_dimension(attrib->type);
    attr = mesh_get_attr(geom_shape->data.mesh, usage) + ids[ivertex] * dim;
    FOR_EACH(i, 0, dim) attrib->value[i] = attr[i];
  } else {
    const float* pos;
    attrib->type = S3D_FLOAT3;
    /* Fetch data */
    pos = mesh_get_pos(geom_shape->data.mesh) + ids[ivertex] * 3;
    f3_set(attrib->value, pos);
    if(transform) { /* Transform the position from local to world space */
      f33_mulf3(attrib->value, transform, attrib->value); /* Rotation */
      f3_add(attrib->value, attrib->value, transform + 9); /* Translation */
    }
  }
  return RES_OK;
}

