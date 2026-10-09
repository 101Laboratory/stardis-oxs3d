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
#include "test_s3d_utils.h"

#include <rsys/float3.h>
#include <rsys/float2.h>

#include <string.h>

struct mesh_context {
  const float* verts;
  const unsigned* ids;
};

static int
filter
  (const struct s3d_hit* hit,
   const float org[3],
   const float dir[3],
   const float range[2],
   void* ray_data,
   void* filter_data)
{
  (void)org, (void)dir, (void)range, (void)ray_data, (void)filter_data;
  CHK(S3D_HIT_NONE(hit) == 0);
  return hit->prim.prim_id % 2 == 0;
}

/*******************************************************************************
 * Cube data
 ******************************************************************************/
static const float cube_verts[] = {
  0.f, 0.f, 0.f,
  1.f, 0.f, 0.f,
  0.f, 1.f, 0.f,
  1.f, 1.f, 0.f,
  0.f, 0.f, 1.f,
  1.f, 0.f, 1.f,
  0.f, 1.f, 1.f,
  1.f, 1.f, 1.f
};
static const unsigned cube_nverts = sizeof(cube_verts) / (sizeof(float)*3);

/* Front faces are CW. The normals point into the cube */
static const unsigned cube_ids[] = {
  0, 2, 1, 1, 2, 3, /* Front */
  0, 4, 2, 2, 4, 6, /* Left */
  4, 5, 6, 6, 5, 7, /* Back */
  3, 7, 1, 1, 7, 5, /* Right */
  2, 6, 3, 3, 6, 7, /* Top */
  0, 1, 4, 4, 1, 5 /* Bottom */
};
static const unsigned cube_ntris = sizeof(cube_ids) / (sizeof(unsigned)*3);

/*******************************************************************************
 * Plane data
 ******************************************************************************/
static const float plane_verts[] = {
  0.f, 0.f, 0.5f,
  1.f, 0.f, 0.5f,
  1.f, 1.f, 0.5f,
  0.f, 1.f, 0.5f
};
static const unsigned plane_nverts = sizeof(plane_verts) / (sizeof(float)*3);

static const unsigned plane_ids[] = { 0, 1, 2, 2, 3, 0 };
static const unsigned plane_ntris = sizeof(plane_ids) / (sizeof(unsigned)*3);

/*******************************************************************************
 * helper function
 ******************************************************************************/
static void
get_ids(const unsigned itri, unsigned ids[3], void* data)
{
  const unsigned id = itri * 3;
  const struct mesh_context* ctx = data;
  CHK(ctx != NULL);
  CHK(ids != NULL);
  ids[0] = ctx->ids[id + 0];
  ids[1] = ctx->ids[id + 1];
  ids[2] = ctx->ids[id + 2];
}

static void
get_pos(const unsigned ivert, float pos[3], void* data)
{
  const unsigned i = ivert*3;
  const struct mesh_context* ctx = data;
  CHK(ctx != NULL);
  CHK(pos != NULL);
  pos[0] = ctx->verts[i + 0];
  pos[1] = ctx->verts[i + 1];
  pos[2] = ctx->verts[i + 2];
}

static void
test_miscellaneous
  (struct s3d_device* dev,
   struct s3d_shape* cube,
   struct s3d_shape* plane)
{
  struct s3d_scene* scn;
  struct s3d_scene_view* scnview;
  float V;
  float A;
  int mask;

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, cube) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, plane) == RES_OK);

  CHK(s3d_scene_view_create(NULL, 0, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_create(scn, 0, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_create(NULL, S3D_SAMPLE, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_create(NULL, 0, &scnview) == RES_BAD_ARG);
  CHK(s3d_scene_view_create(NULL, S3D_SAMPLE, &scnview) == RES_BAD_ARG);
  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);

  CHK(s3d_scene_view_get_mask(NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_mask(scnview, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_mask(NULL, &mask) == RES_BAD_ARG);
  CHK(s3d_scene_view_get_mask(scnview, &mask) == RES_OK);
  CHK(mask == S3D_SAMPLE);

  CHK(s3d_scene_view_ref_get(NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_ref_get(scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE|S3D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s3d_scene_view_get_mask(scnview, &mask) == RES_OK);
  CHK((mask & S3D_TRACE) == S3D_TRACE);
  CHK((mask & S3D_GET_PRIMITIVE) == S3D_GET_PRIMITIVE);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE|S3D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s3d_scene_view_get_mask(scnview, &mask) == RES_OK);
  CHK((mask & S3D_SAMPLE) == S3D_SAMPLE);
  CHK((mask & S3D_GET_PRIMITIVE) == S3D_GET_PRIMITIVE);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_detach_shape(scn, plane) == RES_OK);
  CHK(s3d_scene_view_create(scn, 0, &scnview) == RES_OK);
  CHK(s3d_scene_view_get_mask(scnview, &mask) == RES_OK);
  CHK(mask == 0);
  CHK(s3d_scene_view_compute_volume(scnview, &V) == RES_OK);
  CHK(s3d_scene_view_compute_area(scnview, &A) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(eq_eps(A, 6.f, 1.e-6f));
  CHK(eq_eps(V, 1.f, 1.e-6f));

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_compute_volume(scnview, &V) == RES_OK);
  CHK(s3d_scene_view_compute_area(scnview, &A) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(eq_eps(A, 6.f, 1.e-6f));
  CHK(eq_eps(V, 1.f, 1.e-6f));

  CHK(s3d_scene_ref_put(scn) == RES_OK);
}

static void
test_trace_ray
  (struct s3d_device* dev,
   struct s3d_shape* cube,
   struct s3d_shape* plane)
{
  struct s3d_scene* scn;
  struct s3d_scene* scn2;
  struct s3d_scene* scn3;
  struct s3d_scene_view* scnview;
  struct s3d_scene_view* scnview2;
  struct s3d_scene_view* scnview3;
  struct s3d_shape* inst0;
  struct s3d_shape* inst1;
  struct s3d_hit hit, hit2;
  float org[3], dir[3], range[2];
  unsigned icube;
  unsigned iplane;
  unsigned iinst0;
  unsigned iinst1;

  f3(org, 0.5f, 0.25f, 0.25f);
  f3(dir, 0.f, 0.f, 1.f);
  f2(range, 0.f, FLT_MAX);

  CHK(s3d_shape_get_id(cube, &icube) == RES_OK);
  CHK(s3d_shape_get_id(plane, &iplane) == RES_OK);

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_create(dev, &scn2) == RES_OK);
  CHK(s3d_scene_create(dev, &scn3) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_attach_shape(scn, plane) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, cube) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, cube) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, plane) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE|S3D_GET_PRIMITIVE, &scnview) == RES_OK);

  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_BAD_OP);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview2) == RES_OK);
  CHK(s3d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 0);

  f3(dir, 0.f, 0.f, -1.f);
  CHK(s3d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == icube);
  CHK(hit.prim.prim_id == 0);
  f3(dir, 0.f, 0.f, 1.f);

  CHK(s3d_shape_enable(plane, 0) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);

  CHK(s3d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 0);

  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == icube);
  CHK(hit.prim.prim_id == 4);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview2) == RES_OK);
  CHK(s3d_shape_enable(plane, 1) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn2, S3D_TRACE, &scnview2) == RES_OK);

  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(s3d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit2) == RES_OK);
  CHK(f3_eq(hit.normal, hit2.normal) == 1);
  CHK(f2_eq(hit.uv, hit2.uv) == 1);
  CHK(hit.distance == hit2.distance);
  CHK(S3D_PRIMITIVE_EQ(&hit.prim, &hit2.prim) == 1);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 0);

  CHK(s3d_scene_detach_shape(scn2, plane) == RES_OK);
  CHK(s3d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit2) == RES_OK);
  CHK(f3_eq(hit.normal, hit2.normal) == 1);
  CHK(f2_eq(hit.uv, hit2.uv) == 1);
  CHK(hit.distance == hit2.distance);
  CHK(S3D_PRIMITIVE_EQ(&hit.prim, &hit2.prim) == 1);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview2) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn2, S3D_TRACE, &scnview2) == RES_OK);

  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(f3_eq(hit.normal, hit2.normal) == 1);
  CHK(f2_eq(hit.uv, hit2.uv) == 1);
  CHK(hit.distance == hit2.distance);
  CHK(S3D_PRIMITIVE_EQ(&hit.prim, &hit2.prim) == 1);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 0);

  CHK(s3d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit2) == RES_OK);
  CHK(hit2.prim.inst_id == S3D_INVALID_ID);
  CHK(hit2.prim.geom_id == icube);
  CHK(hit2.prim.prim_id == 4);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview2) == RES_OK);

  CHK(s3d_scene_instantiate(scn2, &inst0) == RES_OK);
  CHK(s3d_scene_instantiate(scn2, &inst1) == RES_OK);
  CHK(s3d_shape_get_id(inst0, &iinst0) == RES_OK);
  CHK(s3d_shape_get_id(inst1, &iinst1) == RES_OK);
  CHK(s3d_instance_translate
    (inst0, S3D_WORLD_TRANSFORM, f3(org,-2.f, 0.f, 0.f)) == RES_OK);
  CHK(s3d_instance_translate
    (inst1, S3D_WORLD_TRANSFORM, f3(org, 2.f, 0.f, 0.f)) == RES_OK);

  CHK(s3d_scene_attach_shape(scn3, inst0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn3, inst1) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, inst0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, inst1) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn3, S3D_TRACE, &scnview3) == RES_OK);

  f3(org, 0.5f, 0.25f, 0.25f);
  f3(dir, 0.f, 0.f, 1.f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 0);

  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);

  f3(org, -1.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == iinst0);
  CHK(hit.prim.geom_id == icube);
  CHK(hit.prim.prim_id == 4);

  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit2) == RES_OK);
  CHK(hit2.prim.inst_id == iinst0);
  CHK(hit2.prim.geom_id == icube);
  CHK(hit2.prim.prim_id == 4);

  CHK(f3_eq(hit.normal, hit2.normal) == 1);
  CHK(f2_eq(hit.uv, hit2.uv) == 1);
  CHK(hit.distance == hit2.distance);

  CHK(s3d_scene_clear(scn2) == RES_OK);

  f3(org, 2.5f, 0.25f, 0.25f);

  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == iinst1);
  CHK(hit.prim.geom_id == icube);
  CHK(hit.prim.prim_id == 4);

  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit2) == RES_OK);
  CHK(hit2.prim.inst_id == iinst1);
  CHK(hit2.prim.geom_id == icube);
  CHK(hit2.prim.prim_id == 4);

  CHK(f3_eq(hit.normal, hit2.normal) == 1);
  CHK(f2_eq(hit.uv, hit2.uv) == 1);
  CHK(hit.distance == hit2.distance);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);

  f3(org, -1.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);
  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == iinst0);
  CHK(hit.prim.geom_id == icube);
  CHK(hit.prim.prim_id == 4);

  f3(org, 2.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);
  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == iinst1);
  CHK(hit.prim.geom_id == icube);
  CHK(hit.prim.prim_id == 4);

  f3(org, 0.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 0);
  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);

  CHK(s3d_scene_attach_shape(scn2, plane) == RES_OK);
  CHK(s3d_mesh_set_hit_filter_function(plane, filter, NULL) == RES_OK);

  f3(org, 0.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 0);

  CHK(s3d_scene_view_ref_put(scnview3) == RES_OK);
  CHK(s3d_scene_view_create(scn3, S3D_TRACE, &scnview3) == RES_OK);

  f3(org, -1.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);
  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);

  f3(org, -1.5f, 0.75f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);
  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == iinst0);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 1);

  f3(org, 2.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);
  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);

  f3(org, 2.5f, 0.75f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);
  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == iinst1);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 1);

  f3(org, 0.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 0);
  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);

  f3(org, 0.5f, 0.75f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 1);
  CHK(s3d_scene_view_trace_ray(scnview3, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  f3(org, 0.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == icube);
  CHK(hit.prim.prim_id == 4);
  f3(org, 0.5f, 0.75f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == S3D_INVALID_ID);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 1);

  f3(org, -1.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);
  f3(org, -1.5f, 0.75f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == iinst0);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 1);

  f3(org, 2.5f, 0.25f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit) == 1);
  f3(org, 2.5f, 0.75f, 0.25f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(hit.prim.inst_id == iinst1);
  CHK(hit.prim.geom_id == iplane);
  CHK(hit.prim.prim_id == 1);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview3) == RES_OK);

  CHK(s3d_shape_ref_put(inst0) == RES_OK);
  CHK(s3d_shape_ref_put(inst1) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_ref_put(scn2) == RES_OK);
  CHK(s3d_scene_ref_put(scn3) == RES_OK);
}

static void
test_sample
  (struct s3d_device* dev,
   struct s3d_shape* cube,
   struct s3d_shape* plane)
{
  #define NSAMPS 512
  struct s3d_scene* scn;
  struct s3d_scene* scn2;
  struct s3d_scene* scn3;
  struct s3d_scene_view* scnview;
  struct s3d_scene_view* scnview3;
  struct s3d_shape* inst0;
  struct s3d_shape* inst1;
  struct s3d_primitive prims[NSAMPS];
  float u, v, w, st[2];
  float pos[3];
  unsigned icube;
  unsigned iplane;
  unsigned iinst0;
  unsigned iinst1;
  int nsamps_cube;
  int nsamps_plane;
  int nsamps_inst0;
  int nsamps_inst1;
  int i;

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_create(dev, &scn2) == RES_OK);
  CHK(s3d_scene_create(dev, &scn3) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, cube) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, plane) == RES_OK);
  CHK(s3d_shape_get_id(cube, &icube) == RES_OK);
  CHK(s3d_shape_get_id(plane, &iplane) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_sample(scnview, 0.f, 0.f, 0.f, &prims[0], st) == RES_BAD_OP);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_sample(scnview, 0.f, 0.f, 0.f, &prims[0], st) == RES_OK);
  CHK(prims[0].inst_id == S3D_INVALID_ID);
  CHK(prims[0].geom_id == icube || prims[0].geom_id == iplane);

  nsamps_cube = 0;
  nsamps_plane = 0;
  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic(), w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview, u, v, w, &prims[i], st) == RES_OK);
    CHK(prims[i].inst_id == S3D_INVALID_ID);
    if(prims[i].geom_id == icube) {
      ++nsamps_cube;
    } else {
      CHK(prims[i].geom_id == iplane);
      ++nsamps_plane;
    }
  }
  CHK(nsamps_cube != 0);
  CHK(nsamps_plane != 0);

  CHK(s3d_shape_enable(cube, 0) == RES_OK);
  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    struct s3d_primitive prim;
    u = rand_canonic(), v = rand_canonic(), w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview, u, v, w, &prim, st) == RES_OK);
    CHK(S3D_PRIMITIVE_EQ(&prim, &prims[i]) == 1);
  }

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);

  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic(), w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview, u, v, w, &prims[i], st) == RES_OK);
    CHK(prims[i].inst_id == S3D_INVALID_ID);
    CHK(prims[i].geom_id == iplane);
  }

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_shape_enable(cube, 1) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, cube) == RES_OK);
  CHK(s3d_scene_instantiate(scn2, &inst0) == RES_OK);
  CHK(s3d_scene_instantiate(scn2, &inst1) == RES_OK);
  CHK(s3d_shape_get_id(inst0, &iinst0) == RES_OK);
  CHK(s3d_shape_get_id(inst1, &iinst1) == RES_OK);
  CHK(s3d_instance_translate
    (inst0, S3D_WORLD_TRANSFORM, f3(pos,-2.f, 0.f, 0.f)) == RES_OK);
  CHK(s3d_instance_translate
    (inst1, S3D_WORLD_TRANSFORM, f3(pos, 2.f, 0.f, 0.f)) == RES_OK);

  CHK(s3d_scene_attach_shape(scn, inst0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, inst1) == RES_OK);
  CHK(s3d_scene_attach_shape(scn3, inst0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn3, inst1) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn3, S3D_SAMPLE, &scnview3) == RES_OK);

  CHK(s3d_scene_detach_shape(scn2, cube) == RES_OK);

  nsamps_cube = 0;
  nsamps_inst0 = 0;
  nsamps_inst1 = 0;
  nsamps_plane = 0;
  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic(), w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview, u, v, w, &prims[i], st) == RES_OK);
    if(prims[i].inst_id != S3D_INVALID_ID) {
      CHK(prims[i].geom_id == icube);
      if(prims[i].inst_id == iinst0) {
        ++nsamps_inst0;
      } else {
        CHK(prims[i].inst_id == iinst1);
        ++nsamps_inst1;
      }
    } else {
      if(prims[i].geom_id == icube) {
        ++nsamps_cube;
      } else {
        CHK(prims[i].geom_id == iplane);
        ++nsamps_plane;
      }
    }

  }
  CHK(nsamps_cube != 0);
  CHK(nsamps_inst0 != 0);
  CHK(nsamps_inst1 != 0);
  CHK(nsamps_plane != 0);

  nsamps_inst0 = 0;
  nsamps_inst1 = 0;
  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic(), w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview3, u, v, w, &prims[i], st) == RES_OK);
    CHK(prims[i].geom_id == icube);
    if(prims[i].inst_id == iinst0) {
      ++nsamps_inst0;
    } else {
      CHK(prims[i].inst_id == iinst1);
      ++nsamps_inst1;
    }
  }
  CHK(nsamps_inst0 != 0);
  CHK(nsamps_inst1 != 0);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);

  nsamps_cube = 0;
  nsamps_plane = 0;
  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic(), w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview, u, v, w, &prims[i], st) == RES_OK);
    CHK(prims[i].inst_id == S3D_INVALID_ID);
    if(prims[i].geom_id == icube) {
      ++nsamps_cube;
    } else {
      CHK(prims[i].geom_id == iplane);
      ++nsamps_plane;
    }
  }
  CHK(nsamps_cube != 0);
  CHK(nsamps_plane != 0);

  nsamps_inst0 = 0;
  nsamps_inst1 = 0;
  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic(), w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview3, u, v, w, &prims[i], st) == RES_OK);
    CHK(prims[i].geom_id == icube);
    if(prims[i].inst_id == iinst0) {
      ++nsamps_inst0;
    } else {
      CHK(prims[i].inst_id == iinst1);
      ++nsamps_inst1;
    }
  }
  CHK(nsamps_inst0 != 0);
  CHK(nsamps_inst1 != 0);

  CHK(s3d_scene_attach_shape(scn2, plane) == RES_OK);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);

  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic(), w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview, u, v, w, &prims[i], st) == RES_OK);
    if(prims[i].inst_id != S3D_INVALID_ID) {
      CHK(prims[i].geom_id == iplane);
      if(prims[i].inst_id == iinst0) {
        ++nsamps_inst0;
      } else {
        CHK(prims[i].inst_id == iinst1);
        ++nsamps_inst1;
      }
    } else {
      if(prims[i].geom_id == icube) {
        ++nsamps_cube;
      } else {
        CHK(prims[i].geom_id == iplane);
        ++nsamps_plane;
      }
    }

  }
  CHK(nsamps_cube != 0);
  CHK(nsamps_inst0 != 0);
  CHK(nsamps_inst1 != 0);
  CHK(nsamps_plane != 0);

  nsamps_inst0 = 0;
  nsamps_inst1 = 0;
  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic(), w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview3, u, v, w, &prims[i], st) == RES_OK);
    CHK(prims[i].geom_id == icube);
    if(prims[i].inst_id == iinst0) {
      ++nsamps_inst0;
    } else {
      CHK(prims[i].inst_id == iinst1);
      ++nsamps_inst1;
    }
  }
  CHK(nsamps_inst0 != 0);
  CHK(nsamps_inst1 != 0);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview3) == RES_OK);

  CHK(s3d_scene_view_create(scn3, S3D_SAMPLE, &scnview3) == RES_OK);
  nsamps_inst0 = 0;
  nsamps_inst1 = 0;
  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic(), w = rand_canonic();
    CHK(s3d_scene_view_sample(scnview3, u, v, w, &prims[i], st) == RES_OK);
    CHK(prims[i].geom_id == iplane);
    if(prims[i].inst_id == iinst0) {
      ++nsamps_inst0;
    } else {
      CHK(prims[i].inst_id == iinst1);
      ++nsamps_inst1;
    }
  }
  CHK(nsamps_inst0 != 0);
  CHK(nsamps_inst1 != 0);

  CHK(s3d_scene_view_ref_put(scnview3) == RES_OK);

  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_ref_put(scn2) == RES_OK);
  CHK(s3d_scene_ref_put(scn3) == RES_OK);
  CHK(s3d_shape_ref_put(inst0) == RES_OK);
  CHK(s3d_shape_ref_put(inst1) == RES_OK);
}

static void
test_get_primitive
  (struct s3d_device* dev,
   struct s3d_shape* cube,
   struct s3d_shape* plane)
{
  struct s3d_scene* scn;
  struct s3d_scene* scn2;
  struct s3d_scene* scn3;
  struct s3d_scene_view* scnview;
  struct s3d_scene_view* scnview3;
  struct s3d_shape* inst0;
  struct s3d_shape* inst1;
  struct s3d_primitive prim;
  size_t nprims;
  unsigned i;
  unsigned icube;
  unsigned iplane;
  unsigned iinst0;
  unsigned iinst1;
  float pos[3];
  int cube_prims[12];
  int plane_prims[2];
  int inst0_prims[12];
  int inst1_prims[12];

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_create(dev, &scn2) == RES_OK);
  CHK(s3d_scene_create(dev, &scn3) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, cube) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, plane) == RES_OK);
  CHK(s3d_shape_get_id(cube, &icube) == RES_OK);
  CHK(s3d_shape_get_id(plane, &iplane) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_get_primitive(scnview, 0, &prim) == RES_BAD_OP);
  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 14);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 14);

  memset(cube_prims, 0, sizeof(cube_prims));
  memset(plane_prims, 0, sizeof(plane_prims));
  FOR_EACH(i, 0, nprims) {
    CHK(s3d_scene_view_get_primitive(scnview, i, &prim) == RES_OK);
    CHK(prim.inst_id == S3D_INVALID_ID);
    CHK(prim.scene_prim_id == i);
    if(prim.geom_id == icube) {
      cube_prims[prim.prim_id] = 1;
    } else {
      CHK(prim.geom_id == iplane);
      plane_prims[prim.prim_id] = 1;
    }
  }
  FOR_EACH(i, 0, 12) CHK(cube_prims[i] == 1);
  FOR_EACH(i, 0, 2) CHK(plane_prims[i] == 1);

  CHK(s3d_scene_detach_shape(scn, cube) == RES_OK);
  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 14);
  memset(cube_prims, 0, sizeof(cube_prims));
  memset(plane_prims, 0, sizeof(plane_prims));
  FOR_EACH(i, 0, nprims) {
    CHK(s3d_scene_view_get_primitive(scnview, i, &prim) == RES_OK);
    CHK(prim.inst_id == S3D_INVALID_ID);
    CHK(prim.scene_prim_id == i);
    if(prim.geom_id == icube) {
      cube_prims[prim.prim_id] = 1;
    } else {
      CHK(prim.geom_id == iplane);
      plane_prims[prim.prim_id] = 1;
    }
  }
  FOR_EACH(i, 0, 12) CHK(cube_prims[i] == 1);
  FOR_EACH(i, 0, 2) CHK(plane_prims[i] == 1);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 2);
  memset(plane_prims, 0, sizeof(plane_prims));
  FOR_EACH(i, 0, nprims) {
    CHK(s3d_scene_view_get_primitive(scnview, i, &prim) == RES_OK);
    CHK(prim.inst_id == S3D_INVALID_ID);
    CHK(prim.scene_prim_id == i);
    CHK(prim.geom_id == iplane);
    plane_prims[prim.prim_id] = 1;
  }
  FOR_EACH(i, 0, 2) CHK(plane_prims[i] == 1);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_scene_attach_shape(scn, cube) == RES_OK);

  CHK(s3d_scene_attach_shape(scn2, plane) == RES_OK);
  CHK(s3d_scene_instantiate(scn2, &inst0) == RES_OK);
  CHK(s3d_scene_instantiate(scn2, &inst1) == RES_OK);
  CHK(s3d_shape_get_id(inst0, &iinst0) == RES_OK);
  CHK(s3d_shape_get_id(inst1, &iinst1) == RES_OK);
  CHK(s3d_instance_translate
    (inst0, S3D_WORLD_TRANSFORM, f3(pos,-2.f, 0.f, 0.f)) == RES_OK);
  CHK(s3d_instance_translate
    (inst1, S3D_WORLD_TRANSFORM, f3(pos, 2.f, 0.f, 0.f)) == RES_OK);

  CHK(s3d_scene_attach_shape(scn, inst0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn3, inst0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn3, inst1) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s3d_scene_view_create(scn3, S3D_GET_PRIMITIVE, &scnview3) == RES_OK);

  CHK(s3d_scene_clear(scn2) == RES_OK);

  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 16);
  memset(plane_prims, 0, sizeof(plane_prims));
  memset(cube_prims, 0, sizeof(cube_prims));
  memset(inst0_prims, 0, sizeof(inst0_prims));
  FOR_EACH(i, 0, nprims) {
    CHK(s3d_scene_view_get_primitive(scnview, i, &prim) == RES_OK);
    if(prim.inst_id != S3D_INVALID_ID) {
      CHK(prim.inst_id == iinst0);
      CHK(prim.geom_id == iplane);
      inst0_prims[prim.prim_id] = 1;
    } else {
      if(prim.geom_id == icube) {
        cube_prims[prim.prim_id] = 1;
      } else {
        CHK(prim.geom_id == iplane);
        plane_prims[prim.prim_id] = 1;
      }
    }
  }
  FOR_EACH(i, 0, 12) CHK(cube_prims[i] == 1);
  FOR_EACH(i, 0, 2) CHK(plane_prims[i] == 1);
  FOR_EACH(i, 0, 2) CHK(inst0_prims[i] == 1);

  CHK(s3d_scene_view_primitives_count(scnview3, &nprims) == RES_OK);
  CHK(nprims == 4);
  memset(inst0_prims, 0, sizeof(inst0_prims));
  memset(inst1_prims, 0, sizeof(inst1_prims));
  FOR_EACH(i, 0, nprims) {
    CHK(s3d_scene_view_get_primitive(scnview3, i, &prim) == RES_OK);
    CHK(prim.inst_id != S3D_INVALID_ID);
    CHK(prim.geom_id == iplane);
    if(prim.inst_id == iinst0) {
      inst0_prims[prim.prim_id] = 1;
    } else {
      CHK(prim.inst_id == iinst1);
      inst1_prims[prim.prim_id] = 1;
    }
  }
  FOR_EACH(i, 0, 2) CHK(inst1_prims[i] == 1);
  FOR_EACH(i, 0, 2) CHK(inst1_prims[i] == 1);

  CHK(s3d_scene_view_ref_put(scnview3) == RES_OK);

  CHK(s3d_scene_view_create(scn3, S3D_GET_PRIMITIVE, &scnview3) == RES_OK);
  CHK(s3d_scene_view_primitives_count(scnview3, &nprims) == RES_OK);
  CHK(nprims == 0);
  CHK(s3d_scene_view_ref_put(scnview3) == RES_OK);

  CHK(s3d_scene_attach_shape(scn2, cube) == RES_OK);
  CHK(s3d_scene_view_create(scn3, S3D_GET_PRIMITIVE, &scnview3) == RES_OK);

  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 16);
  memset(plane_prims, 0, sizeof(plane_prims));
  memset(cube_prims, 0, sizeof(cube_prims));
  memset(inst0_prims, 0, sizeof(inst0_prims));
  FOR_EACH(i, 0, nprims) {
    CHK(s3d_scene_view_get_primitive(scnview, i, &prim) == RES_OK);
    if(prim.inst_id != S3D_INVALID_ID) {
      CHK(prim.inst_id == iinst0);
      CHK(prim.geom_id == iplane);
      inst0_prims[prim.prim_id] = 1;
    } else {
      if(prim.geom_id == icube) {
        cube_prims[prim.prim_id] = 1;
      } else {
        CHK(prim.geom_id == iplane);
        plane_prims[prim.prim_id] = 1;
      }
    }
  }
  FOR_EACH(i, 0, 12) CHK(cube_prims[i] == 1);
  FOR_EACH(i, 0, 2) CHK(plane_prims[i] == 1);
  FOR_EACH(i, 0, 2) CHK(inst0_prims[i] == 1);

  CHK(s3d_scene_view_primitives_count(scnview3, &nprims) == RES_OK);
  CHK(nprims == 24);
  memset(inst0_prims, 0, sizeof(inst0_prims));
  memset(inst1_prims, 0, sizeof(inst1_prims));
  FOR_EACH(i, 0, nprims) {
    CHK(s3d_scene_view_get_primitive(scnview3, i, &prim) == RES_OK);
    CHK(prim.inst_id != S3D_INVALID_ID);
    CHK(prim.geom_id == icube);
    if(prim.inst_id == iinst0) {
      inst0_prims[prim.prim_id] = 1;
    } else {
      CHK(prim.inst_id == iinst1);
      inst1_prims[prim.prim_id] = 1;
    }
  }
  FOR_EACH(i, 0, 12) CHK(inst1_prims[i] == 1);
  FOR_EACH(i, 0, 12) CHK(inst1_prims[i] == 1);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview3) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_GET_PRIMITIVE, &scnview) == RES_OK);

  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 26);
  memset(plane_prims, 0, sizeof(plane_prims));
  memset(cube_prims, 0, sizeof(cube_prims));
  memset(inst0_prims, 0, sizeof(inst0_prims));
  FOR_EACH(i, 0, nprims) {
    CHK(s3d_scene_view_get_primitive(scnview, i, &prim) == RES_OK);
    if(prim.inst_id != S3D_INVALID_ID) {
      CHK(prim.inst_id == iinst0);
      CHK(prim.geom_id == icube);
      inst0_prims[prim.prim_id] = 1;
    } else {
      if(prim.geom_id == icube) {
        cube_prims[prim.prim_id] = 1;
      } else {
        CHK(prim.geom_id == iplane);
        plane_prims[prim.prim_id] = 1;
      }
    }
  }
  FOR_EACH(i, 0, 12) CHK(cube_prims[i] == 1);
  FOR_EACH(i, 0, 2) CHK(plane_prims[i] == 1);
  FOR_EACH(i, 0, 12) CHK(inst0_prims[i] == 1);

  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_shape_ref_put(inst0) == RES_OK);
  CHK(s3d_shape_ref_put(inst1) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_ref_put(scn2) == RES_OK);
  CHK(s3d_scene_ref_put(scn3) == RES_OK);
}

/*******************************************************************************
 * Main test function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct mesh_context ctx;
  struct s3d_device* dev;
  struct s3d_shape* cube;
  struct s3d_shape* plane;
  struct s3d_vertex_data vdata;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s3d_device_create(NULL, &allocator, 1, &dev) == RES_OK);

  vdata.type = S3D_FLOAT3;
  vdata.usage = S3D_POSITION;
  vdata.get = get_pos;

  ctx.ids = cube_ids;
  ctx.verts = cube_verts;
  CHK(s3d_shape_create_mesh(dev, &cube) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (cube, cube_ntris, get_ids, cube_nverts, &vdata, 1, &ctx) == RES_OK);

  ctx.ids = plane_ids;
  ctx.verts = plane_verts;
  CHK(s3d_shape_create_mesh(dev, &plane) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (plane, plane_ntris, get_ids, plane_nverts, &vdata, 1, &ctx) == RES_OK);

  test_miscellaneous(dev, cube, plane);
  test_trace_ray(dev, cube, plane);
  test_sample(dev, cube, plane);
  test_get_primitive(dev, cube, plane);

  CHK(s3d_shape_ref_put(cube) == RES_OK);
  CHK(s3d_shape_ref_put(plane) == RES_OK);
  CHK(s3d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

