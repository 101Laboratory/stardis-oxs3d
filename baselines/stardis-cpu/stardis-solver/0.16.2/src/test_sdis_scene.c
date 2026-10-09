/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#include "sdis.h"
#include "test_sdis_utils.h"

#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys_math.h>
#include <star/senc2d.h>
#include <star/senc3d.h>

struct context {
  const double* positions;
  const size_t* indices;
  struct sdis_interface* interf;
};

static INLINE double
rand_canonic()
{
  return (double)rand()/(double)((double)RAND_MAX+1);
}

static INLINE void
get_indices_3d(const size_t itri, size_t ids[3], void* context)
{
  struct context* ctx = context;
  CHK(ctx != NULL);
  CHK(itri < box_ntriangles);
  ids[0] = ctx->indices[itri*3+0];
  ids[1] = ctx->indices[itri*3+1];
  ids[2] = ctx->indices[itri*3+2];
}

static INLINE void
get_indices_2d(const size_t iseg, size_t ids[2], void* context)
{
  struct context* ctx = context;
  CHK(ctx != NULL);
  CHK(iseg < square_nsegments);
  ids[0] = ctx->indices[iseg*2+0];
  ids[1] = ctx->indices[iseg*2+1];
}

static INLINE void
get_position_3d(const size_t ivert, double pos[3], void* context)
{
  struct context* ctx = context;
  CHK(ctx != NULL);
  CHK(ivert < box_nvertices);
  pos[0] = ctx->positions[ivert*3+0];
  pos[1] = ctx->positions[ivert*3+1];
  pos[2] = ctx->positions[ivert*3+2];
}

static INLINE void
get_position_2d(const size_t ivert, double pos[2], void* context)
{
  struct context* ctx = context;
  CHK(ctx != NULL);
  CHK(ivert < square_nvertices);
  pos[0] = ctx->positions[ivert*2+0];
  pos[1] = ctx->positions[ivert*2+1];
}

static INLINE void
get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct context* ctx = context;
  CHK(ctx != NULL);
  CHK(itri < box_ntriangles);
  CHK(bound != NULL);
  *bound = ctx->interf;
}

static void
test_scene_3d
  (struct sdis_device* dev,
   struct sdis_interface* interf,
   struct sdis_radiative_env* in_radenv)
{
  size_t duplicated_indices[] = { 0, 1, 2, 0, 2, 1 };
  size_t degenerated_indices[] = { 0, 1, 1 };
  double duplicated_vertices[] = { 0, 0, 0, 1, 1, 1, 0, 0, 0 };
  double lower[3], upper[3];
  double uv0[2], uv1[2], uv2[2], pos[3], pos1[3];
  struct context ctx;
  struct senc2d_scene* scn2d;
  struct senc3d_scene* scn3d;
  struct s2d_scene_view* view2d;
  struct s3d_scene_view* view3d;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_scene_find_closest_point_args closest_pt_args =
    SDIS_SCENE_FIND_CLOSEST_POINT_ARGS_NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_device* dev2 = NULL;
  struct sdis_radiative_env* radenv = NULL;
  size_t ntris, npos;
  size_t iprim;
  size_t i;
  double dst = 0;
  size_t dup_vrtx_indices[] = { 0, 1, 2 };
  enum sdis_scene_dimension dim;

  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.interf = interf;
  ntris = box_ntriangles;
  npos = box_nvertices;

  scn_args.get_indices = get_indices_3d;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position_3d;
  scn_args.nprimitives = ntris;
  scn_args.nvertices = npos;
  scn_args.context = &ctx;
  BA(sdis_scene_create(NULL, &SDIS_SCENE_CREATE_ARGS_DEFAULT, NULL));
  BA(sdis_scene_create(NULL, &SDIS_SCENE_CREATE_ARGS_DEFAULT, NULL));

  scn_args.nprimitives = 0;
  BA(sdis_scene_create(dev, &scn_args, &scn));
  scn_args.nprimitives = ntris;
  scn_args.get_indices = NULL;
  BA(sdis_scene_create(dev, &scn_args, &scn));
  scn_args.get_indices = get_indices_3d;
  scn_args.get_interface = NULL;
  BA(sdis_scene_create(dev, &scn_args, &scn));
  scn_args.get_interface = get_interface;
  scn_args.get_position = NULL;
  BA(sdis_scene_create(dev, &scn_args, &scn));
  scn_args.get_position = get_position_3d;
  scn_args.nvertices = 0;
  BA(sdis_scene_create(dev, &scn_args, &scn));
  scn_args.nvertices = npos;
  scn_args.fp_to_meter = 0;
  BA(sdis_scene_create(dev, &scn_args, &scn));
  scn_args.fp_to_meter = 1;
  /* Duplicated vertex */
  ctx.positions = duplicated_vertices;
  ctx.indices = dup_vrtx_indices;
  BA(sdis_scene_create(dev, &scn_args, &scn));
  /* Duplicated triangle */
  ctx.positions = box_vertices;
  ctx.indices = duplicated_indices;
  BA(sdis_scene_create(dev, &scn_args, &scn));
  /* Degenerated triangle */
  ctx.indices = degenerated_indices;
  BA(sdis_scene_create(dev, &scn_args, &scn));
  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  BA(sdis_scene_create(dev, &SDIS_SCENE_CREATE_ARGS_DEFAULT, &scn));
  BA(sdis_scene_create(NULL, &scn_args, &scn));
  BA(sdis_scene_create(dev, NULL, &scn));
  BA(sdis_scene_create(dev, &scn_args, NULL));
  OK(sdis_scene_create(dev, &scn_args, &scn));

  BA(sdis_scene_get_dimension(NULL, &dim));
  BA(sdis_scene_get_dimension(scn, NULL));
  OK(sdis_scene_get_dimension(scn, &dim));
  CHK(dim == SDIS_SCENE_3D);

  BA(sdis_scene_get_device(NULL, &dev2));
  BA(sdis_scene_get_device(scn, NULL));
  OK(sdis_scene_get_device(scn, &dev2));
  CHK(dev == dev2);

  BA(sdis_scene_get_aabb(NULL, lower, upper));
  BA(sdis_scene_get_aabb(scn, NULL, upper));
  BA(sdis_scene_get_aabb(scn, lower, NULL));
  OK(sdis_scene_get_aabb(scn, lower, upper));
  CHK(eq_eps(lower[0], 0, 1.e-6));
  CHK(eq_eps(lower[1], 0, 1.e-6));
  CHK(eq_eps(lower[2], 0, 1.e-6));
  CHK(eq_eps(upper[0], 1, 1.e-6));
  CHK(eq_eps(upper[1], 1, 1.e-6));
  CHK(eq_eps(upper[2], 1, 1.e-6));

  uv0[0] = 0.3;
  uv0[1] = 0.3;

  BA(sdis_scene_get_boundary_position(NULL, 6, uv0, pos));
  BA(sdis_scene_get_boundary_position(scn, 12, uv0, pos));
  BA(sdis_scene_get_boundary_position(scn, 6, NULL, pos));
  BA(sdis_scene_get_boundary_position(scn, 6, uv0, NULL));
  OK(sdis_scene_get_boundary_position(scn, 6, uv0, pos) );

  BA(sdis_scene_boundary_project_position(NULL, 6, pos, uv1));
  BA(sdis_scene_boundary_project_position(scn, 12, pos, uv1));
  BA(sdis_scene_boundary_project_position(scn, 6, NULL, uv1));
  BA(sdis_scene_boundary_project_position(scn, 6, pos, NULL));
  OK(sdis_scene_boundary_project_position(scn, 6, pos, uv1));

  closest_pt_args.position[0] = pos[0];
  closest_pt_args.position[1] = pos[1];
  closest_pt_args.position[2] = pos[2];
  closest_pt_args.radius = INF;
  BA(sdis_scene_find_closest_point(NULL, &closest_pt_args, &iprim, uv2));
  BA(sdis_scene_find_closest_point(scn, NULL, &iprim, uv2));
  BA(sdis_scene_find_closest_point(scn, &closest_pt_args, NULL, uv2));
  BA(sdis_scene_find_closest_point(scn, &closest_pt_args, &iprim, NULL));
  OK(sdis_scene_find_closest_point(scn, &closest_pt_args, &iprim, uv2));

  CHK(iprim == 6);
  CHK(d2_eq_eps(uv0, uv1, 1.e-6));
  CHK(d2_eq_eps(uv1, uv2, 1.e-6));

  pos[0] = 0.5;
  pos[1] = 0.1;
  pos[2] = 0.25;
  closest_pt_args.position[0] = pos[0];
  closest_pt_args.position[1] = pos[1];
  closest_pt_args.position[2] = pos[2];
  OK(sdis_scene_find_closest_point(scn, &closest_pt_args, &iprim, uv2));
  CHK(iprim == 10);

  OK(sdis_scene_boundary_project_position(scn, 10, pos, uv0));
  CHK(d2_eq_eps(uv0, uv2, 1.e-6));

  OK(sdis_scene_get_boundary_position(scn, iprim, uv2, pos1));
  dst = d3_len(d3_sub(pos1, pos, pos1));
  CHK(eq_eps(dst, 0.1, 1.e-6));

  closest_pt_args.position[0] = pos[0];
  closest_pt_args.position[1] = pos[1];
  closest_pt_args.position[2] = pos[2];
  closest_pt_args.radius = 0.09;
  OK(sdis_scene_find_closest_point(scn, &closest_pt_args, &iprim, uv2));
  CHK(iprim == SDIS_PRIMITIVE_NONE);

  FOR_EACH(i, 0, 64) {
    uv0[0] = rand_canonic();
    uv0[1] = rand_canonic() * (1 - uv0[0]);

    OK(sdis_scene_get_boundary_position(scn, 4, uv0, pos));
    OK(sdis_scene_boundary_project_position(scn, 4, pos, uv1));

    closest_pt_args.position[0] = pos[0];
    closest_pt_args.position[1] = pos[1];
    closest_pt_args.position[2] = pos[2];
    closest_pt_args.radius = INF;
    OK(sdis_scene_find_closest_point(scn, &closest_pt_args, &iprim, uv2));
    CHK(d2_eq_eps(uv0, uv1, 1.e-6));
    CHK(d2_eq_eps(uv1, uv2, 1.e-6));
    CHK(iprim == 4);
  }

  pos[0] = 10;
  pos[1] = 0.1;
  pos[2] = 0.5;
  OK(sdis_scene_boundary_project_position(scn, 6, pos, uv1));
  OK(sdis_scene_get_boundary_position(scn, 6, uv1, pos1));
  CHK(!d3_eq_eps(pos1, pos, 1.e-6));

  BA(sdis_scene_get_senc3d_scene(NULL, NULL));
  BA(sdis_scene_get_senc3d_scene(scn, NULL));
  BA(sdis_scene_get_senc3d_scene(NULL, &scn3d));
  OK(sdis_scene_get_senc3d_scene(scn, &scn3d));
  BA(sdis_scene_get_senc2d_scene(scn, &scn2d)); /* No 2D available */

  BA(sdis_scene_get_s3d_scene_view(NULL, NULL));
  BA(sdis_scene_get_s3d_scene_view(NULL, &view3d));
  BA(sdis_scene_get_s3d_scene_view(scn, NULL));
  OK(sdis_scene_get_s3d_scene_view(scn, &view3d));
  BA(sdis_scene_get_s2d_scene_view(scn, &view2d)); /* No 2D available */

  BA(sdis_scene_get_radiative_env(NULL, &radenv));
  BA(sdis_scene_get_radiative_env(scn, NULL));
  OK(sdis_scene_get_radiative_env(scn, &radenv));
  CHK(radenv == NULL);

  BA(sdis_scene_ref_get(NULL));
  OK(sdis_scene_ref_get(scn));
  BA(sdis_scene_ref_put(NULL));
  OK(sdis_scene_ref_put(scn));
  OK(sdis_scene_ref_put(scn));

  scn_args.radenv = in_radenv;
  OK(sdis_scene_create(dev, &scn_args, &scn));
  OK(sdis_scene_get_radiative_env(scn, &radenv));
  CHK(radenv == in_radenv);
  CHK(radenv != NULL);

  OK(sdis_scene_ref_put(scn));
}

static void
test_scene_2d
  (struct sdis_device* dev,
   struct sdis_interface* interf,
   struct sdis_radiative_env* in_radenv)
{
  size_t duplicated_indices[] = { 0, 1, 1, 0 };
  size_t degenerated_indices[] = { 0, 0 };
  double duplicated_vertices[] = { 0, 0, 0, 0 };
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_scene_find_closest_point_args closest_pt_args =
    SDIS_SCENE_FIND_CLOSEST_POINT_ARGS_NULL;
  struct sdis_radiative_env* radenv = NULL;
  double lower[2], upper[2];
  double t_range[2];
  double u0, u1, u2, pos[2], pos1[2];
  double dst, fp;
  struct context ctx;
  struct senc2d_scene* scn2d;
  struct senc3d_scene* scn3d;
  struct s2d_scene_view* view2d;
  struct s3d_scene_view* view3d;
  size_t nsegs, npos;
  size_t i;
  size_t iprim;
  size_t dup_vrtx_indices[] = { 0, 1 };
  enum sdis_scene_dimension dim;

  ctx.positions = square_vertices;
  ctx.indices = square_indices;
  ctx.interf = interf;
  nsegs = square_nsegments;
  npos = square_nvertices;

  scn_args.get_indices = get_indices_2d;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position_2d;
  scn_args.nprimitives = nsegs;
  scn_args.nvertices = npos;
  scn_args.context = &ctx;

  BA(sdis_scene_2d_create(NULL, &SDIS_SCENE_CREATE_ARGS_DEFAULT, NULL));
  scn_args.nprimitives = 0;
  BA(sdis_scene_2d_create(dev, &scn_args, &scn));
  scn_args.nprimitives = nsegs;
  scn_args.get_indices = NULL;
  BA(sdis_scene_2d_create(dev, &scn_args, &scn));
  scn_args.get_indices = get_indices_2d;
  scn_args.get_interface = NULL;
  BA(sdis_scene_2d_create(dev, &scn_args, &scn));
  scn_args.get_interface = get_interface;
  scn_args.get_position = NULL;
  BA(sdis_scene_2d_create(dev, &scn_args, &scn));
  scn_args.get_position = get_position_2d;
  scn_args.nvertices = 0;
  BA(sdis_scene_2d_create(dev, &scn_args, &scn));
  scn_args.nvertices = npos;
  scn_args.fp_to_meter = 0;
  BA(sdis_scene_2d_create(dev, &scn_args, &scn));
  scn_args.fp_to_meter = 1;
  /* Duplicated vertex */
  ctx.positions = duplicated_vertices;
  ctx.indices = dup_vrtx_indices;
  BA(sdis_scene_2d_create(dev, &scn_args, &scn));
  /* Duplicated segment */
  ctx.positions = square_vertices;
  ctx.indices = duplicated_indices;
  BA(sdis_scene_2d_create(dev, &scn_args, &scn));
  /* Degenerated segment */
  ctx.indices = degenerated_indices;
  BA(sdis_scene_2d_create(dev, &scn_args, &scn));
  ctx.positions = square_vertices;
  ctx.indices = square_indices;
  BA(sdis_scene_2d_create(dev, &SDIS_SCENE_CREATE_ARGS_DEFAULT, &scn));
  BA(sdis_scene_2d_create(NULL, &scn_args, &scn));
  BA(sdis_scene_2d_create(dev, NULL, &scn));
  BA(sdis_scene_2d_create(dev, &scn_args, NULL));
  OK(sdis_scene_2d_create(dev, &scn_args, &scn));

  BA(sdis_scene_get_dimension(NULL, &dim));
  BA(sdis_scene_get_dimension(scn, NULL));
  OK(sdis_scene_get_dimension(scn, &dim));
  CHK(dim == SDIS_SCENE_2D);

  BA(sdis_scene_get_aabb(NULL, lower, upper));
  BA(sdis_scene_get_aabb(scn, NULL, upper));
  BA(sdis_scene_get_aabb(scn, lower, NULL));
  OK(sdis_scene_get_aabb(scn, lower, upper));
  CHK(eq_eps(lower[0], 0, 1.e-6));
  CHK(eq_eps(lower[1], 0, 1.e-6));
  CHK(eq_eps(upper[0], 1, 1.e-6));
  CHK(eq_eps(upper[1], 1, 1.e-6));

  u0 = 0.5;

  BA(sdis_scene_get_fp_to_meter(NULL, NULL));
  BA(sdis_scene_get_fp_to_meter(scn, NULL));
  BA(sdis_scene_get_fp_to_meter(NULL, &fp));
  OK(sdis_scene_get_fp_to_meter(scn, &fp));
  CHK(fp == 1);

  fp = 0;
  BA(sdis_scene_set_fp_to_meter(NULL, fp));
  BA(sdis_scene_set_fp_to_meter(scn, fp));
  fp = 2;
  OK(sdis_scene_set_fp_to_meter(scn, fp));
  OK(sdis_scene_get_fp_to_meter(scn, &fp));
  CHK(fp == 2);

  BA(sdis_scene_get_temperature_range(NULL, NULL));
  BA(sdis_scene_get_temperature_range(scn, NULL));
  BA(sdis_scene_get_temperature_range(NULL, t_range));
  OK(sdis_scene_get_temperature_range(scn, t_range));
  if(SDIS_TEMPERATURE_IS_KNOWN(t_range[0])) {
    CHK(t_range[0] == SDIS_SCENE_CREATE_ARGS_DEFAULT.t_range[0]);
  } else {
    CHK(SDIS_TEMPERATURE_IS_UNKNOWN(SDIS_SCENE_CREATE_ARGS_DEFAULT.t_range[0]));
  }
  if(SDIS_TEMPERATURE_IS_KNOWN(t_range[1])) {
    CHK(t_range[1] == SDIS_SCENE_CREATE_ARGS_DEFAULT.t_range[1]);
  } else {
    CHK(SDIS_TEMPERATURE_IS_UNKNOWN(SDIS_SCENE_CREATE_ARGS_DEFAULT.t_range[0]));
  }

  t_range[0] = 1;
  t_range[1] = 100;

  BA(sdis_scene_set_temperature_range(NULL, t_range));
  BA(sdis_scene_set_temperature_range(scn, NULL));
  OK(sdis_scene_set_temperature_range(scn, t_range));
  t_range[0] = SDIS_TEMPERATURE_NONE;
  t_range[1] = SDIS_TEMPERATURE_NONE;
  OK(sdis_scene_get_temperature_range(scn, t_range));
  CHK(t_range[0] == 1);
  CHK(t_range[1] == 100);

  BA(sdis_scene_get_boundary_position(NULL, 1, &u0, pos));
  BA(sdis_scene_get_boundary_position(scn, 4, &u0, pos));
  BA(sdis_scene_get_boundary_position(scn, 1, NULL, pos));
  BA(sdis_scene_get_boundary_position(scn, 1, &u0, NULL));
  OK(sdis_scene_get_boundary_position(scn, 1, &u0, pos));

  BA(sdis_scene_boundary_project_position(NULL, 1, pos, &u1));
  BA(sdis_scene_boundary_project_position(scn, 4, pos, &u1));
  BA(sdis_scene_boundary_project_position(scn, 1, NULL, &u1));
  BA(sdis_scene_boundary_project_position(scn, 1, pos, NULL));
  OK(sdis_scene_boundary_project_position(scn, 1, pos, &u1));

  closest_pt_args.position[0] = pos[0];
  closest_pt_args.position[1] = pos[1];
  closest_pt_args.radius = INF;
  BA(sdis_scene_find_closest_point(NULL, &closest_pt_args, &iprim, &u2));
  BA(sdis_scene_find_closest_point(scn, NULL, &iprim, &u2));
  BA(sdis_scene_find_closest_point(scn, &closest_pt_args, NULL, &u2));
  BA(sdis_scene_find_closest_point(scn, &closest_pt_args, &iprim, NULL));
  OK(sdis_scene_find_closest_point(scn, &closest_pt_args, &iprim, &u2));

  CHK(eq_eps(u0, u1, 1.e-6));
  CHK(eq_eps(u1, u2, 1.e-6));
  CHK(iprim == 1);

  pos[0] = 0.5;
  pos[1] = 0.1;
  closest_pt_args.position[0] = pos[0];
  closest_pt_args.position[1] = pos[1];
  closest_pt_args.radius = INF;
  OK(sdis_scene_find_closest_point(scn, &closest_pt_args, &iprim, &u2));
  CHK(iprim == 0);

  OK(sdis_scene_boundary_project_position(scn, 0, pos, &u0));
  CHK(eq_eps(u0, u2, 1.e-6));

  OK(sdis_scene_get_boundary_position(scn, iprim, &u2, pos1));
  dst = d2_len(d2_sub(pos1, pos, pos1));
  CHK(eq_eps(dst, 0.1, 1.e-6));

  closest_pt_args.position[0] = pos[0];
  closest_pt_args.position[1] = pos[1];
  closest_pt_args.radius = 0.09;
  OK(sdis_scene_find_closest_point(scn, &closest_pt_args, &iprim, &u2));
  CHK(iprim == SDIS_PRIMITIVE_NONE);

  FOR_EACH(i, 0, 64) {
    u0 = rand_canonic();

    OK(sdis_scene_get_boundary_position(scn, 2, &u0, pos));
    OK(sdis_scene_boundary_project_position(scn, 2, pos, &u1));

    closest_pt_args.position[0] = pos[0];
    closest_pt_args.position[1] = pos[1];
    closest_pt_args.radius = INF;
    OK(sdis_scene_find_closest_point(scn, &closest_pt_args, &iprim, &u2));
    CHK(eq_eps(u0, u1, 1.e-6));
    CHK(eq_eps(u1, u2, 1.e-6));
    CHK(iprim == 2);
  }

  d2(pos, 5, 0.5);
  OK(sdis_scene_boundary_project_position(scn, 3, pos, &u0));
  CHK(eq_eps(u0, 0.5, 1.e-6));

  d2(pos, 1, 2);
  OK(sdis_scene_boundary_project_position(scn, 3, pos, &u0));
  CHK(eq_eps(u0, 0, 1.e-6));

  d2(pos, 1, -1);
  OK(sdis_scene_boundary_project_position(scn, 3, pos, &u0));
  CHK(eq_eps(u0, 1, 1.e-6));

  BA(sdis_scene_get_senc2d_scene(NULL, NULL));
  BA(sdis_scene_get_senc2d_scene(scn, NULL));
  BA(sdis_scene_get_senc2d_scene(NULL, &scn2d));
  OK(sdis_scene_get_senc2d_scene(scn, &scn2d));
  BA(sdis_scene_get_senc3d_scene(scn, &scn3d)); /* No 3D available */

  BA(sdis_scene_get_s2d_scene_view(NULL, NULL));
  BA(sdis_scene_get_s2d_scene_view(NULL, &view2d));
  BA(sdis_scene_get_s2d_scene_view(scn, NULL));
  OK(sdis_scene_get_s2d_scene_view(scn, &view2d));
  BA(sdis_scene_get_s3d_scene_view(scn, &view3d)); /* No 3D available */

  BA(sdis_scene_get_radiative_env(NULL, NULL));
  BA(sdis_scene_get_radiative_env(scn, NULL));
  BA(sdis_scene_get_radiative_env(NULL, &radenv));
  OK(sdis_scene_get_radiative_env(scn, &radenv));
  CHK(radenv == NULL);

  BA(sdis_scene_ref_get(NULL));
  OK(sdis_scene_ref_get(scn));
  BA(sdis_scene_ref_put(NULL));
  OK(sdis_scene_ref_put(scn));
  OK(sdis_scene_ref_put(scn));

  scn_args.radenv = in_radenv;
  OK(sdis_scene_2d_create(dev, &scn_args, &scn));
  OK(sdis_scene_get_radiative_env(scn, &radenv));
  CHK(radenv == in_radenv);
  CHK(radenv != NULL);

  OK(sdis_scene_ref_put(scn));
}

int
main(int argc, char** argv)
{
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_radiative_env_shader ray_shader = DUMMY_RAY_SHADER;
  (void)argc, (void)argv;

  interface_shader.convection_coef = DUMMY_INTERFACE_SHADER.convection_coef;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));
  OK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, NULL, &interf));
  OK(sdis_radiative_env_create(dev, &ray_shader, NULL, &radenv));

  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  test_scene_3d(dev, interf, radenv);
  test_scene_2d(dev, interf, radenv);

  OK(sdis_device_ref_put(dev));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_radiative_env_ref_put(radenv));

  CHK(mem_allocated_size() == 0);
  return 0;
}

