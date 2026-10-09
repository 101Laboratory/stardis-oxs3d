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
#include "s3d_instance.h"
#include "s3d_geometry.h"
#include "s3d_mesh.h"
#include "s3d_sphere.h"
#include "s3d_scene_view_c.h"

#include <rsys/float33.h>
#include <limits.h>

struct intersect_context {
  struct RTCRayQueryContext rtc;
  struct s3d_scene_view* scnview;
  void* data; /* Per ray user defined data */
  float ws_org[3]; /* World space ray origin */
  float ws_dir[3]; /* World space ray direction */
  float ws_range[3]; /* World space ray range */
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE void
hit_setup
  (struct s3d_scene_view* scnview,
   const struct RTCRayHit* ray_hit,
   struct s3d_hit* hit)
{
  float w;
  char flip_surface = 0;

  ASSERT(scnview && hit && ray_hit);

  if(ray_hit->hit.geomID == RTC_INVALID_GEOMETRY_ID) { /* No hit */
    *hit = S3D_HIT_NULL;
    return;
  }

  hit->normal[0] = ray_hit->hit.Ng_x;
  hit->normal[1] = ray_hit->hit.Ng_y;
  hit->normal[2] = ray_hit->hit.Ng_z;
  hit->distance = ray_hit->ray.tfar;

  if(ray_hit->hit.instID[0] == RTC_INVALID_GEOMETRY_ID) {
    struct geometry* geom_shape;
    geom_shape = scene_view_geometry_from_embree_id(scnview, ray_hit->hit.geomID);
    hit->prim.shape__ = geom_shape;
    hit->prim.inst__ = NULL;
    hit->prim.prim_id = ray_hit->hit.primID;
    hit->prim.geom_id = geom_shape->name;
    hit->prim.inst_id = S3D_INVALID_ID;
    hit->prim.scene_prim_id = /* Compute the "scene space" primitive id */
      hit->prim.prim_id /* Mesh space */
    + geom_shape->scene_prim_id_offset; /* Scene space */

  } else { /* The hit shape is instantiated */
    /* Retrieve the hit instance */
    struct geometry* geom_inst;
    struct geometry* geom_shape;
    float transform[9];
    geom_inst = scene_view_geometry_from_embree_id
      (scnview, ray_hit->hit.instID[0]);
    geom_shape = scene_view_geometry_from_embree_id
      (geom_inst->data.instance->scnview, ray_hit->hit.geomID);
    hit->prim.shape__ = geom_shape;
    hit->prim.inst__ = geom_inst;
    hit->prim.prim_id = ray_hit->hit.primID;
    hit->prim.geom_id = geom_shape->name;
    hit->prim.inst_id = geom_inst->name;
    hit->prim.scene_prim_id = /* Compute the "scene space" primitive id */
      hit->prim.prim_id /* Shape space */
      + geom_shape->scene_prim_id_offset /* Inst space */
      + geom_inst->scene_prim_id_offset; /* Scene space */

    flip_surface = geom_inst->flip_surface;
    ASSERT(hit->prim.inst__);
    ASSERT(((struct geometry*)hit->prim.inst__)->type == GEOM_INSTANCE);

    /* Transform the normal in world space */
    f33_invtrans(transform, geom_inst->data.instance->transform);
    f33_mulf3(hit->normal, transform, hit->normal);
  }
  ASSERT(hit->prim.shape__);
  ASSERT(((struct geometry*)hit->prim.shape__)->type == GEOM_MESH
       ||((struct geometry*)hit->prim.shape__)->type == GEOM_SPHERE);

  /* Handle Embree returning uv out of range */
  hit->uv[0] = CLAMP(ray_hit->hit.u, 0, 1);
  hit->uv[1] = CLAMP(ray_hit->hit.v, 0, 1);

  if(((struct geometry*)hit->prim.shape__)->type == GEOM_MESH) {
    w = 1.f - hit->uv[0] - hit->uv[1];
    if(w < 0.f) { /* Handle precision error */
      if(hit->uv[0] > hit->uv[1]) hit->uv[0] += w;
      else hit->uv[1] += w;
      w = 0.f;
    }

    /* Embree stores on the u and v ray parameters the barycentric coordinates of
     * the hit with respect to the second and third triangle vertices,
     * respectively. The following code computes the barycentric coordinates of
     * the hit for the first and second triangle vertices */
    hit->uv[1] = hit->uv[0];
    hit->uv[0] = w;

    /* In Embree3 the normal orientation is flipped wrt to Star-3D convention */
    #if RTC_VERSION_MAJOR >= 3
    f3_minus(hit->normal, hit->normal);
    #endif
  }

  /* Flip geometric normal with respect to the flip surface flag */
  flip_surface ^= ((struct geometry*)hit->prim.shape__)->flip_surface;
  if(flip_surface) f3_minus(hit->normal, hit->normal);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s3d_scene_view_trace_ray
  (struct s3d_scene_view* scnview,
   const float org[3],
   const float dir[3],
   const float range[2],
   void* ray_data,
   struct s3d_hit* hit)
{
  struct RTCRayHit ray_hit;
  struct RTCIntersectArguments intersect_args;
  struct intersect_context intersect_ctx;
  size_t i;

  if(!scnview || !org || !dir || !range || !hit)
    return RES_BAD_ARG;
  if(!f3_is_normalized(dir)) {
    log_error(scnview->scn->dev,
      "%s: unnormalized ray direction {%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(dir));
    return RES_BAD_ARG;
  }
  if(range[0] < 0) {
    log_error(scnview->scn->dev,
      "%s: invalid ray range [%g, %g] - it must be in [0, INF).\n",
      FUNC_NAME, range[0], range[1]);
    return RES_BAD_ARG;
  }
  if((scnview->mask & S3D_TRACE) == 0) {
    log_error(scnview->scn->dev,
      "%s: the S3D_TRACE flag is not active onto the submitted scene view.\n",
      FUNC_NAME);
    return RES_BAD_OP;
  }
  if(range[0] > range[1]) { /* Degenerated range <=> disabled ray */
    *hit = S3D_HIT_NULL;
    return RES_OK;
  }

  /* Initialise the ray */
  ray_hit.ray.org_x = org[0];
  ray_hit.ray.org_y = org[1];
  ray_hit.ray.org_z = org[2];
  ray_hit.ray.dir_x = dir[0];
  ray_hit.ray.dir_y = dir[1];
  ray_hit.ray.dir_z = dir[2];
  ray_hit.ray.tnear = range[0];
  ray_hit.ray.tfar = range[1];
  ray_hit.ray.time = FLT_MAX; /* Invalid fields */
  ray_hit.ray.mask = UINT_MAX; /* Invalid fields */
  ray_hit.ray.id = UINT_MAX; /* Invalid fields */
  ray_hit.ray.flags = UINT_MAX; /* Invalid fields */

  /* Initialise the hit */
  ray_hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
  FOR_EACH(i, 0, RTC_MAX_INSTANCE_LEVEL_COUNT) {
    ray_hit.hit.instID[i] = RTC_INVALID_GEOMETRY_ID;
  }

  /* Initialise the intersect context */
  rtcInitIntersectArguments(&intersect_args);
  intersect_args.context = &intersect_ctx.rtc;
  rtcInitRayQueryContext(&intersect_ctx.rtc);
  intersect_ctx.ws_org[0] = org[0];
  intersect_ctx.ws_org[1] = org[1];
  intersect_ctx.ws_org[2] = org[2];
  intersect_ctx.ws_dir[0] = dir[0];
  intersect_ctx.ws_dir[1] = dir[1];
  intersect_ctx.ws_dir[2] = dir[2];
  intersect_ctx.ws_range[0] = range[0];
  intersect_ctx.ws_range[1] = range[1];
  intersect_ctx.scnview = scnview;
  intersect_ctx.data = ray_data;

  /* Here we go! */
  rtcIntersect1(scnview->rtc_scn, &ray_hit, &intersect_args);

  hit_setup(scnview, &ray_hit, hit);
  return RES_OK;
}

res_T
s3d_scene_view_trace_rays
  (struct s3d_scene_view* scnview,
   const size_t nrays,
   const int mask,
   const float* origins,
   const float* directions,
   const float* ranges,
   void* rays_data,
   const size_t sizeof_ray_data,
   struct s3d_hit* hits)
{
  size_t iray;
  size_t iorg, idir, irange, idata;
  size_t org_step, dir_step, range_step, data_step;
  res_T res = RES_OK;

  if(!scnview) return RES_BAD_ARG;
  if(!nrays) return RES_OK;

  org_step = mask & S3D_RAYS_SINGLE_ORIGIN ? 0 : 3;
  dir_step = mask & S3D_RAYS_SINGLE_DIRECTION ? 0 : 3;
  range_step = mask & S3D_RAYS_SINGLE_RANGE ? 0 : 2;
  data_step = (mask & S3D_RAYS_SINGLE_DATA) || !rays_data ? 0 : sizeof_ray_data;
  iorg = idir = irange = idata = 0;

  FOR_EACH(iray, 0, nrays) {
    res = s3d_scene_view_trace_ray(scnview, origins+iorg, directions+idir,
      ranges+irange, (char*)rays_data+idata, hits+iray);
    if(UNLIKELY(res != RES_OK)) break;
    iorg += org_step;
    idir += dir_step;
    irange += range_step;
    idata += data_step;
  }
  return res;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
/* Wrapper between an Embree and a Star-3D filter function */
void
rtc_hit_filter_wrapper(const struct RTCFilterFunctionNArguments* args)
{
  struct s3d_hit hit;
  struct RTCRayHit ray_hit;
  struct intersect_context* ctx;
  struct geometry* geom;
  struct hit_filter* filter;
  int is_hit_filtered = 0;
  ASSERT(args && args->N == 1 && args->context && args->valid[0] != 0);

  rtc_rayN_get_ray(args->ray, args->N, 0, &ray_hit.ray);
  rtc_hitN_get_hit(args->hit, args->N, 0, &ray_hit.hit);

  ctx = CONTAINER_OF(args->context, struct intersect_context, rtc);

  geom = args->geometryUserPtr;
  switch(geom->type) {
    case GEOM_MESH:
      filter = &geom->data.mesh->filter;
      break;
    case GEOM_SPHERE:
      filter = &geom->data.sphere->filter;
      break;
    default: FATAL("Unreachable code\n"); break;
  }
  ASSERT(filter->func);

  hit_setup(ctx->scnview, &ray_hit, &hit);
  is_hit_filtered = filter->func
    (&hit, ctx->ws_org, ctx->ws_dir, ctx->ws_range, ctx->data, filter->data);
  if(is_hit_filtered) {
    args->valid[0] = 0;
  }
}
