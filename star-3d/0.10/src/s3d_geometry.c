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
#include "s3d_geometry.h"
#include "s3d_instance.h"
#include "s3d_mesh.h"
#include "s3d_scene_view_c.h"
#include "s3d_sphere.h"

#include <rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static FINLINE void
sphere_ray_hit_setup
  (const struct RTCIntersectFunctionNArguments* args, const float tfar)
{
  struct geometry* geom = args->geometryUserPtr;
  struct RTCRayN* rayN;
  struct RTCHitN* hitN;
  struct RTCHit hit;
  struct RTCRay ray;
  float Ng[3];
  float uv[2];
  size_t i;
  ASSERT(args && args->primID == 0 && args->N == 1 && args->valid[0] != 0);

  geom = args->geometryUserPtr;
  ASSERT(geom && geom->type == GEOM_SPHERE);

  rayN = RAYHITN_GET_RAYN(args->rayhit, args->N);
  hitN = RAYHITN_GET_HITN(args->rayhit, args->N);

  rtc_rayN_get_ray(rayN, args->N, 0, &ray);
  ray.tfar = tfar;

  Ng[0] = ray.dir_x*tfar + ray.org_x - geom->data.sphere->pos[0];
  Ng[1] = ray.dir_y*tfar + ray.org_y - geom->data.sphere->pos[1];
  Ng[2] = ray.dir_z*tfar + ray.org_z - geom->data.sphere->pos[2];

  f3_normalize(Ng, Ng);
  sphere_normal_to_uv(Ng, uv);

  hit.Ng_x = Ng[0];
  hit.Ng_y = Ng[1];
  hit.Ng_z = Ng[2];
  hit.u = uv[0];
  hit.v = uv[1];
  hit.primID = 0;
  hit.geomID = geom->rtc_id;
  FOR_EACH(i, 0, RTC_MAX_INSTANCE_LEVEL_COUNT) {
    hit.instID[i] = args->context->instID[i];
  }

  /* Filter the intersection if required */
  if(geom->data.sphere->filter.func) {
    struct RTCFilterFunctionNArguments filter_args;
    int valid = 1;

    filter_args.valid = &valid;
    filter_args.geometryUserPtr = args->geometryUserPtr;
    filter_args.context = args->context;
    filter_args.ray = (struct RTCRayN*)&ray;
    filter_args.hit = (struct RTCHitN*)&hit;
    filter_args.N = args->N;

    rtc_hit_filter_wrapper(&filter_args);
    if(!filter_args.valid[0]) return;
  }

  RAYN_GRAB(rayN, args->N, 0, float, tfar) = tfar;
  rtc_hitN_set_hit(hitN, args->N, 0, &hit);
}

static void
geometry_release(ref_T* ref)
{
  struct geometry* geom;
  struct s3d_device* dev;

  geom = CONTAINER_OF(ref, struct geometry, ref);
  dev = geom->dev;
  switch(geom->type) {
    case GEOM_MESH:
      if(geom->data.mesh) mesh_ref_put(geom->data.mesh);
      break;
    case GEOM_INSTANCE:
      if(geom->data.instance) instance_ref_put(geom->data.instance);
      break;
    case GEOM_SPHERE:
      if(geom->data.sphere) sphere_ref_put(geom->data.sphere);
      break;
    default: FATAL("Unreachable code\n"); break;
  }
  MEM_RM(dev->allocator, geom);
  S3D(device_ref_put(dev));
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
geometry_create
  (struct s3d_device* dev,
   struct geometry** out_geom)
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
  S3D(device_ref_get(dev));
  geom->dev = dev;
  geom->name = S3D_INVALID_ID;
  geom->flip_surface = 0;
  geom->is_enabled = 1;
  geom->type = GEOM_NONE;
  geom->data.mesh = NULL;
  geom->rtc = NULL;
  geom->rtc_id = RTC_INVALID_GEOMETRY_ID;
  geom->rtc_build_quality = RTC_BUILD_QUALITY_MEDIUM;

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

void
geometry_rtc_sphere_bounds(const struct RTCBoundsFunctionArguments* args)
{
  struct geometry* geom;
  struct sphere sphere;
  ASSERT(args && args->primID == 0 && args->timeStep == 0);

  geom = args->geometryUserPtr;
  ASSERT(geom && geom->type == GEOM_SPHERE);

  sphere = *geom->data.sphere;
  args->bounds_o->lower_x = sphere.pos[0] - sphere.radius;
  args->bounds_o->lower_y = sphere.pos[1] - sphere.radius;
  args->bounds_o->lower_z = sphere.pos[2] - sphere.radius;
  args->bounds_o->upper_x = sphere.pos[0] + sphere.radius;
  args->bounds_o->upper_y = sphere.pos[1] + sphere.radius;
  args->bounds_o->upper_z = sphere.pos[2] + sphere.radius;
}

void
geometry_rtc_sphere_intersect(const struct RTCIntersectFunctionNArguments* args)
{
  float v[3];
  float ray_org[3];
  float ray_dir[3];
  float A, B, C, D, Q, rcpA, t0, t1;
  struct geometry* geom;
  struct sphere sphere;
  struct RTCRayN* rayN;
  ASSERT(args && args->primID == 0 && args->N == 1 && args->valid[0] != 0);

  geom = args->geometryUserPtr;
  ASSERT(geom && geom->type == GEOM_SPHERE);

  rayN = RAYHITN_GET_RAYN(args->rayhit, args->N);
  ray_org[0] = RAYN_GRAB(rayN, args->N, 0, float, org_x);
  ray_org[1] = RAYN_GRAB(rayN, args->N, 0, float, org_y);
  ray_org[2] = RAYN_GRAB(rayN, args->N, 0, float, org_z);
  ray_dir[0] = RAYN_GRAB(rayN, args->N, 0, float, dir_x);
  ray_dir[1] = RAYN_GRAB(rayN, args->N, 0, float, dir_y);
  ray_dir[2] = RAYN_GRAB(rayN, args->N, 0, float, dir_z);

  sphere = *geom->data.sphere;
  f3_sub(v, ray_org, sphere.pos);
  A = f3_dot(ray_dir, ray_dir);
  B = 2*f3_dot(v, ray_dir);
  C = f3_dot(v, v) - sphere.radius*sphere.radius;
  D = B*B - 4*A*C;

  if(D < 0.0f) return;
  Q = (float)sqrt(D);
  rcpA = 1.f / A;
  t0 = 0.5f * rcpA * (-B - Q);
  t1 = 0.5f * rcpA * (-B + Q);

  if(RAYN_GRAB(rayN, args->N, 0, float, tnear) < t0
  && RAYN_GRAB(rayN, args->N, 0, float, tfar)  > t0) {
    sphere_ray_hit_setup(args, t0);
  }
  if(RAYN_GRAB(rayN, args->N, 0, float, tnear) < t1
  && RAYN_GRAB(rayN, args->N, 0, float, tfar)  > t1) {
    sphere_ray_hit_setup(args, t1);
  }
}

