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
#include "s3d_instance.h"
#include "s3d_geometry.h"
#include "s3d_mesh.h"
#include "s3d_scene_view_c.h"
#include "s3d_sphere.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/float33.h>

struct point_query_context {
  struct RTCPointQueryContext rtc;
  struct s3d_scene_view* scnview;
  float radius; /* Submitted radius */
  void* data; /* Per point query defined data */
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE double*
closest_point_triangle
  (const double p[3], /* Point */
   const double a[3], /* 1st triangle vertex */
   const double b[3], /* 2nd triangle vertex */
   const double c[3], /* 3rd triangle vertex */
    double closest_pt[3], /* Closest position */
    double uv[2]) /* UV of the closest position */
{
  double ab[3], ac[3], ap[3], bp[3], cp[3];
  double d1, d2, d3, d4, d5, d6;
  double va, vb, vc;
  double rcp_triangle_area;
  double v, w;
  ASSERT(p && a && b && c && closest_pt && uv);

  d3_sub(ab, b, a);
  d3_sub(ac, c, a);

  /* Check if the closest point is the triangle vertex 'a' */
  d3_sub(ap, p, a);
  d1 = d3_dot(ab, ap);
  d2 = d3_dot(ac, ap);
  if(d1 <= 0 && d2 <= 0) {
    uv[0] = 1;
    uv[1] = 0;
    return d3_set(closest_pt, a);
  }

  /* Check if the closest point is the triangle vertex 'b' */
  d3_sub(bp, p, b);
  d3 = d3_dot(ab, bp);
  d4 = d3_dot(ac, bp);
  if(d3 >= 0 && d4 <= d3) {
    uv[0] = 0;
    uv[1] = 1;
    return d3_set(closest_pt, b);
  }

  /* Check if the closest point is the triangle vertex 'c' */
  d3_sub(cp, p, c);
  d5 = d3_dot(ab, cp);
  d6 = d3_dot(ac, cp);
  if(d6 >= 0 && d5 <= d6) {
    uv[0] = 0;
    uv[1] = 0;
    return d3_set(closest_pt, c);
  }

  /* Check if the closest point is on the triangle edge 'ab' */
  vc = d1*d4 - d3*d2;
  if(vc <= 0 && d1 >= 0 && d3 <= 0) {
    const double s = d1 / (d1 - d3);
    closest_pt[0] = a[0] + s*ab[0];
    closest_pt[1] = a[1] + s*ab[1];
    closest_pt[2] = a[2] + s*ab[2];
    uv[0] = 1 - s;
    uv[1] = s;
    return closest_pt;
  }

  /* Check if the closest point is on the triangle edge 'ac' */
  vb = d5*d2 - d1*d6;
  if(vb <= 0 && d2 >= 0 && d6 <= 0) {
    const double s = d2 / (d2 - d6);
    closest_pt[0] = a[0] + s*ac[0];
    closest_pt[1] = a[1] + s*ac[1];
    closest_pt[2] = a[2] + s*ac[2];
    uv[0] = 1 - s;
    uv[1] = 0;
    return closest_pt;
  }

  /* Check if the closest point is on the triangle edge 'bc' */
  va = d3*d6 - d5*d4;
  if(va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    const double s = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    closest_pt[0] = b[0] + s*(c[0] - b[0]);
    closest_pt[1] = b[1] + s*(c[1] - b[1]);
    closest_pt[2] = b[2] + s*(c[2] - b[2]);
    uv[0] = 0;
    uv[1] = 1 - s;
    return closest_pt;
  }

  /* The closest point lies in the triangle: compute its barycentric
   * coordinates */
  rcp_triangle_area = 1 / (va + vb + vc);
  v = vb * rcp_triangle_area;
  w = vc * rcp_triangle_area;

  /* Save the uv barycentric coordinates */
  uv[0] = 1 - v - w;
  uv[1] = v;
  ASSERT(eq_eps(uv[0] + uv[1] + w, 1, 1.e-4));

  if(uv[0] < 0) { /* Handle precision issues */
    if(uv[1] > w) uv[1] += uv[0];
    uv[0] = 0;
  }

  /* Use the barycentric coordinates to compute the world space position of the
   * closest point onto the triangle */
  closest_pt[0] = a[0] + v*ab[0] + w*ac[0];
  closest_pt[1] = a[1] + v*ab[1] + w*ac[1];
  closest_pt[2] = a[2] + v*ab[2] + w*ac[2];
  return closest_pt;
}

static bool
closest_point_mesh
  (struct RTCPointQueryFunctionArguments* args,
   struct geometry* geom,
   struct geometry* inst, /* Can be NULL */
   const float radius,
   void* query_data)
{
  struct s3d_hit hit = S3D_HIT_NULL;
  struct s3d_hit* out_hit = NULL;
  struct hit_filter* filter = NULL;
  const uint32_t* ids = NULL;
  double closest_point[3];
  double query_pos_ws[3]; /* World space query position */
  double query_pos_ls[3]; /* Local space query position */
  double v0[3], v1[3], v2[3];
  float E0[3], E1[3], Ng[3];
  double vec[3];
  double uv[2];
  float dst;
  float pos[3], dir[3], range[2];
  int flip_surface = 0;
  ASSERT(args && geom && geom->type == GEOM_MESH && radius >= 0);
  ASSERT(args->primID < mesh_get_ntris(geom->data.mesh));

  /* Fetch triangle indices */
  ids = mesh_get_ids(geom->data.mesh) + args->primID*3/*#indices per triangle*/;

  /* Fetch triangle vertices */
  ASSERT(geom->data.mesh->attribs_type[S3D_POSITION] == S3D_FLOAT3);
  d3_set_f3(v0, mesh_get_pos(geom->data.mesh) + ids[0]*3/*#coords per vertex*/);
  d3_set_f3(v1, mesh_get_pos(geom->data.mesh) + ids[1]*3/*#coords per vertex*/);
  d3_set_f3(v2, mesh_get_pos(geom->data.mesh) + ids[2]*3/*#coords per vertex*/);

  /* Local copy of the query position */
  query_pos_ws[0] = args->query->x;
  query_pos_ws[1] = args->query->y;
  query_pos_ws[2] = args->query->z;

  if(!args->context->instStackSize) { /* The mesh is instantiated */
    query_pos_ls[0] = query_pos_ws[0];
    query_pos_ls[1] = query_pos_ws[1];
    query_pos_ls[2] = query_pos_ws[2];
  } else {
    const float* world2inst;
    double a[3], b[3], c[3], tmp[3];
    ASSERT(args->context->instStackSize == 1);
    ASSERT(inst && inst->type == GEOM_INSTANCE);

    world2inst = args->context->world2inst[0];

    /* Transform the query position in instance space */
    d3_muld(a, d3_set_f3(tmp, world2inst+0), query_pos_ws[0]);
    d3_muld(b, d3_set_f3(tmp, world2inst+4), query_pos_ws[1]);
    d3_muld(c, d3_set_f3(tmp, world2inst+8), query_pos_ws[2]);
    query_pos_ls[0] = a[0] + b[0] + c[0] + world2inst[12];
    query_pos_ls[1] = a[1] + b[1] + c[1] + world2inst[13];
    query_pos_ls[2] = a[2] + b[2] + c[2] + world2inst[14];

    flip_surface = inst->flip_surface;
  }

  /* Compute the closest point onto the triangle from the submitted point */
  closest_point_triangle(query_pos_ls, v0, v1, v2, closest_point, uv);

  /* Compute the distance */
  d3_sub(vec, closest_point, query_pos_ls);
  dst = (float)d3_len(vec);

  /* Transform the distance in world space */
  if(args->context->instStackSize != 0) {
    ASSERT(args->similarityScale > 0);
    dst /= args->similarityScale;
  }

  if(dst >= args->query->radius) return 0;

  /* Compute the triangle normal in world space (left hand convention). Keep
   * it in float to avoid double-cast accuracy loss wrt user computed result */
  f3_sub(E0, mesh_get_pos(geom->data.mesh) + ids[1] * 3,
    mesh_get_pos(geom->data.mesh) + ids[0] * 3);
  f3_sub(E1, mesh_get_pos(geom->data.mesh) + ids[2] * 3,
    mesh_get_pos(geom->data.mesh) + ids[0] * 3);
  f3_cross(Ng, E1, E0);

  /* Flip the geometric normal wrt the flip surface flag */
  flip_surface ^= geom->flip_surface;
  if(flip_surface) f3_minus(Ng, Ng);

  /* Setup the hit */
  hit.prim.shape__ = geom;
  hit.prim.inst__ = inst;
  hit.distance = dst;
  hit.uv[0] = (float)uv[0];
  hit.uv[1] = (float)uv[1];
  hit.normal[0] = Ng[0];
  hit.normal[1] = Ng[1];
  hit.normal[2] = Ng[2];
  hit.prim.prim_id = args->primID;
  hit.prim.geom_id = geom->name;
  hit.prim.inst_id = inst ? inst->name : S3D_INVALID_ID;
  hit.prim.scene_prim_id =
    hit.prim.prim_id
  + geom->scene_prim_id_offset
  + (inst ? inst->scene_prim_id_offset : 0);

  range[0] = 0;
  range[1] = radius;

  /* `dir' is the direction along which the closest point was found. We thus
   * submit it to the filter function as the direction corresponding to the
   * computed hit */
  f3_set_d3(dir, vec);
  f3_set_d3(pos, query_pos_ws);

  filter = &geom->data.mesh->filter;
  if(filter->func
  && filter->func(&hit, pos, dir, range, query_data, filter->data)) {
    return 0; /* This point is filtered. Discard it! */
  }

  /* Update output data */
  out_hit = args->userPtr;
  *out_hit = hit;

  /* Shrink the query radius */
  args->query->radius = dst;

  return 1; /* Notify that the query radius was updated */
}

static bool
closest_point_sphere
  (struct RTCPointQueryFunctionArguments* args,
   struct geometry* geom,
   struct geometry* inst,
   const float radius, /* User defined radius */
   void* query_data)
{
  struct s3d_hit hit = S3D_HIT_NULL;
  struct s3d_hit* out_hit = NULL;
  struct hit_filter* filter = NULL;
  float query_pos[3];
  float sphere_pos[3];
  float Ng[3];
  float dir[3], range[2];
  float uv[2];
  float dst;
  float len;
  int flip_surface = 0;
  ASSERT(args && geom && geom->type == GEOM_SPHERE && args->primID == 0);
  ASSERT(radius >= 0);

  /* Local copy of the query position */
  query_pos[0] = args->query->x;
  query_pos[1] = args->query->y;
  query_pos[2] = args->query->z;

  f3_set(sphere_pos, geom->data.sphere->pos);
  if(args->context->instStackSize) { /* The sphere is instantiated */
    const float* transform;
    transform = inst->data.instance->transform;
    ASSERT(args->context->instStackSize == 1);
    ASSERT(inst && inst->type == GEOM_INSTANCE);
    ASSERT(f3_eq_eps(transform+0, args->context->inst2world[0]+0, 1.e-6f));
    ASSERT(f3_eq_eps(transform+3, args->context->inst2world[0]+4, 1.e-6f));
    ASSERT(f3_eq_eps(transform+6, args->context->inst2world[0]+8, 1.e-6f));
    ASSERT(f3_eq_eps(transform+9, args->context->inst2world[0]+12,1.e-6f));

    /* Transform the sphere position in world space */
    f33_mulf3(sphere_pos, transform, sphere_pos);
    f3_add(sphere_pos, transform+9, sphere_pos);

    flip_surface = inst->flip_surface;
  }

  /* Compute the distance from the query pos to the sphere center */
  f3_sub(Ng, query_pos, sphere_pos);
  len = f3_len(Ng);

  /* Evaluate the distance from the query pos to the sphere surface */
  dst = fabsf(len - geom->data.sphere->radius);

  /* The closest point onto the sphere is outside the query radius */
  if(dst >= args->query->radius)
    return 0;

  /* Normalize the hit normal */
  if(len > 0) {
    f3_divf(Ng, Ng, len);
  } else {
    /* The query position is equal to the sphere center. Arbitrarily choose the
     * point along the +X axis as the closest point */
    Ng[0] = 1.f;
    Ng[1] = 0.f;
    Ng[2] = 0.f;
  }

  /* Compute the uv of the found point */
  sphere_normal_to_uv(Ng, uv);

  /* Flip the geometric normal wrt the flip surface flag */
  flip_surface ^= geom->flip_surface;
  if(flip_surface) f3_minus(Ng, Ng);

  /* Setup the hit */
  hit.prim.shape__ = geom;
  hit.prim.inst__ = inst;
  hit.distance = dst;
  hit.uv[0] = uv[0];
  hit.uv[1] = uv[1];
  hit.normal[0] = Ng[0];
  hit.normal[1] = Ng[1];
  hit.normal[2] = Ng[2];
  hit.prim.prim_id = args->primID;
  hit.prim.geom_id = geom->name;
  hit.prim.inst_id = inst ? inst->name : S3D_INVALID_ID;
  hit.prim.scene_prim_id =
    hit.prim.prim_id
  + geom->scene_prim_id_offset
  + (inst ? inst->scene_prim_id_offset : 0);

  range[0] = 0;
  range[1] = radius;

  /* Use the reversed geometric normal as the hit direction since it is along
   * this vector that the closest point was effectively computed */
  f3_minus(dir, Ng);
  filter = &geom->data.sphere->filter;
  if(filter->func
  && filter->func(&hit, query_pos, dir, range, query_data, filter->data)) {
    return 0;
  }

  /* Update output data */
  out_hit = args->userPtr;
  *out_hit = hit;

  /* Shrink the query radius */
  args->query->radius = dst;

  return 1; /* Notify that the query radius was updated */
}

static bool
closest_point(struct RTCPointQueryFunctionArguments* args)
{
  struct point_query_context* ctx = NULL;
  struct geometry* geom = NULL;
  struct geometry* inst = NULL;
  bool query_radius_is_upd = false;
  ASSERT(args);

  ctx = CONTAINER_OF(args->context, struct point_query_context, rtc);
  if(args->context->instStackSize == 0) {
    geom = scene_view_geometry_from_embree_id
      (ctx->scnview, args->geomID);
  } else {
    ASSERT(args->context->instStackSize == 1);
    inst = scene_view_geometry_from_embree_id
      (ctx->scnview, args->context->instID[0]);
    geom = scene_view_geometry_from_embree_id
      (inst->data.instance->scnview, args->geomID);
  }

  switch(geom->type) {
    case GEOM_MESH:
      query_radius_is_upd = closest_point_mesh
        (args, geom, inst, ctx->radius, ctx->data);
      break;
    case GEOM_SPHERE:
      query_radius_is_upd = closest_point_sphere
        (args, geom, inst, ctx->radius, ctx->data);
      break;
    default: FATAL("Unreachable code\n"); break;
  }
  return query_radius_is_upd;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s3d_scene_view_closest_point
  (struct s3d_scene_view* scnview,
   const float pos[3],
   const float radius,
   void* query_data,
   struct s3d_hit* hit)
{
  struct RTCPointQuery query;
  struct point_query_context query_ctx;

  if(!scnview || !pos || radius <= 0 || !hit)
    return RES_BAD_ARG;
  if((scnview->mask & S3D_TRACE) == 0) {
    log_error(scnview->scn->dev,
      "%s: the S3D_TRACE flag is not active onto the submitted scene view.\n",
      FUNC_NAME);
    return RES_BAD_OP;
  }

  *hit = S3D_HIT_NULL;

  /* Initialise the point query */
  query.x = pos[0];
  query.y = pos[1];
  query.z = pos[2];
  query.radius = radius;
  query.time = FLT_MAX; /* Invalid fields */

  /* Initialise the point query context */
  rtcInitPointQueryContext(&query_ctx.rtc);
  query_ctx.scnview = scnview;
  query_ctx.radius = radius;
  query_ctx.data = query_data;

  /* Here we go! */
  rtcPointQuery(scnview->rtc_scn, &query, &query_ctx.rtc, closest_point, hit);

  return RES_OK;
}

