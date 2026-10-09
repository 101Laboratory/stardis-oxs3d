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

#ifndef S2D_C_H
#define S2D_C_H

#include "s2d.h"
#include "s2d_backend.h"

#include <rsys/rsys.h>

static FINLINE res_T
rtc_error_to_res_T(const enum RTCError err)
{
  switch(err) {
    case RTC_ERROR_NONE: return RES_OK;
    case RTC_ERROR_UNKNOWN: return RES_UNKNOWN_ERR;
    case RTC_ERROR_INVALID_ARGUMENT: return RES_BAD_ARG;
    case RTC_ERROR_INVALID_OPERATION: return RES_BAD_ARG;
    case RTC_ERROR_OUT_OF_MEMORY: return RES_MEM_ERR;
    case RTC_ERROR_UNSUPPORTED_CPU: return RES_BAD_ARG;
    case RTC_ERROR_CANCELLED: return RES_UNKNOWN_ERR;
    default: FATAL("Unreachable code\n"); break;
  }
}

static INLINE const char*
rtc_error_string(const enum RTCError err)
{
  const char* str = NULL;
  switch(err) {
    case RTC_ERROR_NONE: str = "No error"; break;
    case RTC_ERROR_UNKNOWN: str = "Unknown error"; break;
    case RTC_ERROR_INVALID_ARGUMENT: str = "Invalid argument"; break;
    case RTC_ERROR_INVALID_OPERATION: str = "Invalid operation"; break;
    case RTC_ERROR_OUT_OF_MEMORY: str = "Out of memory"; break;
    case RTC_ERROR_UNSUPPORTED_CPU: str = "Unsupported CPU"; break;
    case RTC_ERROR_CANCELLED: str = "Cancelled operation"; break;
    default: FATAL("Unreachable code\n"); break;
  }
  return str;
}

static INLINE unsigned
s2d_type_get_dimension(const enum s2d_type type)
{
  switch(type) {
    case S2D_FLOAT: return 1;
    case S2D_FLOAT2: return 2;
    case S2D_FLOAT3: return 3;
    default: FATAL("Unreachable code\n"); break;
  }
}

#define RAYN_GRAB(RayN, N, i, Type, Attr) \
  (((Type*)((char*)(RayN)+(offsetof(struct RTCRay, Attr)*N)))[i])
#define HITN_GRAB(HitN, N, i, Type, Attr) \
  (((Type*)((char*)(HitN)+(offsetof(struct RTCHit, Attr)*N)))[i])
#define RAYHITN_GET_RAYN(RayHitN, N) \
  ((struct RTCRayN*)((char*)RayHitN+offsetof(struct RTCRayHit, ray)*N))
#define RAYHITN_GET_HITN(RayHitN, N) \
  ((struct RTCHitN*)((char*)RayHitN+offsetof(struct RTCRayHit, hit)*N))

static FINLINE void
rtc_rayN_get_ray
  (const struct RTCRayN* rayN, /* SoA layout */
   const size_t N, /* SoA width */
   const size_t i, /* Id of the ray */
   struct RTCRay* ray)
{
  ASSERT(rayN && ray && i < N);
  ray->org_x = RAYN_GRAB(rayN, N, i, float, org_x);
  ray->org_y = RAYN_GRAB(rayN, N, i, float, org_y);
  ray->org_z = RAYN_GRAB(rayN, N, i, float, org_z);
  ray->tnear = RAYN_GRAB(rayN, N, i, float, tnear);
  ray->dir_x = RAYN_GRAB(rayN, N, i, float, dir_x);
  ray->dir_y = RAYN_GRAB(rayN, N, i, float, dir_y);
  ray->dir_z = RAYN_GRAB(rayN, N, i, float, dir_z);
  ray->time  = RAYN_GRAB(rayN, N, i, float, time);
  ray->tfar  = RAYN_GRAB(rayN, N, i, float, tfar);
  ray->mask  = RAYN_GRAB(rayN, N, i, unsigned, mask);
  ray->id    = RAYN_GRAB(rayN, N, i, unsigned, id);
  ray->flags = RAYN_GRAB(rayN, N, i, unsigned, flags);
}

static FINLINE void
rtc_hitN_get_hit
  (const struct RTCHitN* hitN,
   const size_t N,
   const size_t i,
   struct RTCHit* hit)
{
  size_t id;
  ASSERT(hitN && hit && i < N);
  hit->Ng_x   = HITN_GRAB(hitN, N, i, float, Ng_x);
  hit->Ng_y   = HITN_GRAB(hitN, N, i, float, Ng_y);
  hit->Ng_z   = HITN_GRAB(hitN, N, i, float, Ng_z);
  hit->u      = HITN_GRAB(hitN, N, i, float, u);
  hit->v      = HITN_GRAB(hitN, N, i, float, v);
  hit->primID = HITN_GRAB(hitN, N, i, unsigned, primID);
  hit->geomID = HITN_GRAB(hitN, N, i, unsigned, geomID);
  FOR_EACH(id, 0, RTC_MAX_INSTANCE_LEVEL_COUNT) {
    hit->instID[id] = HITN_GRAB(hitN, N, i, unsigned, instID[id]);
  }
}

static FINLINE void
rtc_rayN_set_ray
  (struct RTCRayN* rayN,
   const size_t N,
   const size_t i,
   const struct RTCRay* ray)
{
  ASSERT(rayN && ray && i < N);
  RAYN_GRAB(rayN, N, i, float, org_x) = ray->org_x;
  RAYN_GRAB(rayN, N, i, float, org_y) = ray->org_y;
  RAYN_GRAB(rayN, N, i, float, org_z) = ray->org_z;
  RAYN_GRAB(rayN, N, i, float, tnear) = ray->tnear;
  RAYN_GRAB(rayN, N, i, float, dir_x) = ray->dir_x;
  RAYN_GRAB(rayN, N, i, float, dir_y) = ray->dir_y;
  RAYN_GRAB(rayN, N, i, float, dir_z) = ray->dir_z;
  RAYN_GRAB(rayN, N, i, float, time) = ray->time;
  RAYN_GRAB(rayN, N, i, float, tfar) = ray->tfar;
  RAYN_GRAB(rayN, N, i, unsigned, mask) = ray->mask;
  RAYN_GRAB(rayN, N, i, unsigned, id) = ray->id;
  RAYN_GRAB(rayN, N, i, unsigned, flags) = ray->flags;
}

static FINLINE void
rtc_hitN_set_hit
  (const struct RTCHitN* hitN,
   const size_t N,
   const size_t i,
   struct RTCHit* hit)
{
  size_t id;
  ASSERT(hitN && hit && i < N);
  HITN_GRAB(hitN, N, i, float, Ng_x) = hit->Ng_x;
  HITN_GRAB(hitN, N, i, float, Ng_y) = hit->Ng_y;
  HITN_GRAB(hitN, N, i, float, Ng_z) = hit->Ng_z;
  HITN_GRAB(hitN, N, i, float, u) = hit->u;
  HITN_GRAB(hitN, N, i, float, v) = hit->v;
  HITN_GRAB(hitN, N, i, unsigned, primID) = hit->primID;
  HITN_GRAB(hitN, N, i, unsigned, geomID) = hit->geomID;
  FOR_EACH(id, 0, RTC_MAX_INSTANCE_LEVEL_COUNT) {
    HITN_GRAB(hitN, N, i, unsigned, instID[id]) = hit->instID[id];
  }
}

#endif /* S2D_C_H */
