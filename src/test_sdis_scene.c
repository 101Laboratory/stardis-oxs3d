/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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
#include <rsys/math.h>
#include<star/senc2d.h>
#include<star/senc3d.h>

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
test_scene_3d(struct sdis_device* dev, struct sdis_interface* interf)
{
  size_t duplicated_indices[] = { 0, 1, 2, 0, 2, 1 };
  size_t degenerated_indices[] = { 0, 1, 1 };
  double duplicated_vertices[] = { 0, 0, 0, 1, 1, 1, 0, 0, 0 };
  double lower[3], upper[3];
  double uv0[2], uv1[2], uv2[2], pos[3], pos1[3];
  struct context ctx;
  struct senc2d_scene* scn2d;
  struct senc3d_scene* scn3d;
  struct sdis_scene* scn = NULL;
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

  #define CREATE sdis_scene_create
  #define IDS get_indices_3d
  #define POS get_position_3d
  #define IFA get_interface

  BA(CREATE(NULL, 0, NULL, NULL, 0, NULL, &ctx, NULL));
  BA(CREATE(dev, 0, IDS, IFA, npos, POS, &ctx, &scn));
  BA(CREATE(dev, ntris, NULL, IFA, npos, POS, &ctx, &scn));
  BA(CREATE(dev, ntris, IDS, NULL, npos, POS, &ctx, &scn));
  BA(CREATE(dev, ntris, IDS, IFA, 0, POS, &ctx, &scn));
  BA(CREATE(dev, ntris, IDS, IFA, npos, NULL, &ctx, &scn));
  /* Duplicated vertex */
  ctx.positions = duplicated_vertices;
  ctx.indices = dup_vrtx_indices;
  BA(CREATE(dev, 1, IDS, IFA, npos, POS, &ctx, &scn));
  /* Duplicated triangle */
  ctx.positions = box_vertices;
  ctx.indices = duplicated_indices;
  BA(CREATE(dev, 2, IDS, IFA, npos, POS, &ctx, &scn));
  /* Degenerated triangle */
  ctx.indices = degenerated_indices;
  BA(CREATE(dev, 1, IDS, IFA, npos, POS, &ctx, &scn));

  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  OK(CREATE(dev, ntris, IDS, IFA, npos, POS, &ctx, &scn));

  #undef CREATE
  #undef IDS
  #undef POS
  #undef IFA

  BA(sdis_scene_get_dimension(NULL, &dim));
  BA(sdis_scene_get_dimension(scn, NULL));
  OK(sdis_scene_get_dimension(scn, &dim));
  CHK(dim == SDIS_SCENE_3D);

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

  BA(sdis_scene_find_closest_point(NULL, pos, INF, &iprim, uv2));
  BA(sdis_scene_find_closest_point(scn, NULL, INF, &iprim, uv2));
  BA(sdis_scene_find_closest_point(scn, pos, 0, &iprim, uv2));
  BA(sdis_scene_find_closest_point(scn, pos, INF, NULL, uv2));
  BA(sdis_scene_find_closest_point(scn, pos, INF, &iprim, NULL));
  OK(sdis_scene_find_closest_point(scn, pos, INF, &iprim, uv2));

  CHK(iprim == 6);
  CHK(d2_eq_eps(uv0, uv1, 1.e-6));
  CHK(d2_eq_eps(uv1, uv2, 1.e-6));

  pos[0] = 0.5;
  pos[1] = 0.1;
  pos[2] = 0.25;
  OK(sdis_scene_find_closest_point(scn, pos, INF, &iprim, uv2));
  CHK(iprim == 10);

  OK(sdis_scene_boundary_project_position(scn, 10, pos, uv0));
  CHK(d2_eq_eps(uv0, uv2, 1.e-6));

  OK(sdis_scene_get_boundary_position(scn, iprim, uv2, pos1));
  dst = d3_len(d3_sub(pos1, pos, pos1));
  CHK(eq_eps(dst, 0.1, 1.e-6));

  OK(sdis_scene_find_closest_point(scn, pos, 0.09, &iprim, uv2));
  CHK(iprim == SDIS_PRIMITIVE_NONE);

  FOR_EACH(i, 0, 64) {
    uv0[0] = rand_canonic();
    uv0[1] = rand_canonic() * (1 - uv0[0]);

    OK(sdis_scene_get_boundary_position(scn, 4, uv0, pos));
    OK(sdis_scene_boundary_project_position(scn, 4, pos, uv1));
    OK(sdis_scene_find_closest_point(scn, pos, INF, &iprim, uv2));
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
  OK(senc3d_scene_ref_put(scn3d));
  /* No 2D available */
  BA(sdis_scene_get_senc2d_scene(scn, &scn2d));

  BA(sdis_scene_ref_get(NULL));
  OK(sdis_scene_ref_get(scn));
  BA(sdis_scene_ref_put(NULL));
  OK(sdis_scene_ref_put(scn));
  OK(sdis_scene_ref_put(scn));
}

static void
test_scene_2d(struct sdis_device* dev, struct sdis_interface* interf)
{
  size_t duplicated_indices[] = { 0, 1, 1, 0 };
  size_t degenerated_indices[] = { 0, 0 };
  double duplicated_vertices[] = { 0, 0, 0, 0 };
  struct sdis_scene* scn = NULL;
  double lower[2], upper[2];
  double u0, u1, u2, pos[2], pos1[2];
  double dst;
  struct context ctx;
  struct senc2d_scene* scn2d;
  struct senc3d_scene* scn3d;
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

  #define CREATE sdis_scene_2d_create
  #define IDS get_indices_2d
  #define POS get_position_2d
  #define IFA get_interface

  BA(CREATE(NULL, 0, NULL, NULL, 0, NULL, &ctx, NULL));
  BA(CREATE(dev, 0, IDS, IFA, npos, POS, &ctx, &scn));
  BA(CREATE(dev, nsegs, NULL, IFA, npos, POS, &ctx, &scn));
  BA(CREATE(dev, nsegs, IDS, NULL, npos, POS, &ctx, &scn));
  BA(CREATE(dev, nsegs, IDS, IFA, 0, POS, &ctx, &scn));
  BA(CREATE(dev, nsegs, IDS, IFA, npos, NULL, &ctx, &scn));
  /* Duplicated vertex */
  ctx.positions = duplicated_vertices;
  ctx.indices = dup_vrtx_indices;
  BA(CREATE(dev, 1, IDS, IFA, npos, POS, &ctx, &scn));
  /* Duplicated segment */
  ctx.positions = square_vertices;
  ctx.indices = duplicated_indices;
  BA(CREATE(dev, 2, IDS, IFA, npos, POS, &ctx, &scn));
  /* Degenerated segment */
  ctx.indices = degenerated_indices;
  BA(CREATE(dev, 1, IDS, IFA, npos, POS, &ctx, &scn));

  ctx.positions = square_vertices;
  ctx.indices = square_indices;
  OK(CREATE(dev, nsegs, IDS, IFA, npos, POS, &ctx, &scn));

  #undef CREATE
  #undef IDS
  #undef POS
  #undef IFA

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

  BA(sdis_scene_find_closest_point(NULL, pos, INF, &iprim, &u2));
  BA(sdis_scene_find_closest_point(scn, NULL, INF, &iprim, &u2));
  BA(sdis_scene_find_closest_point(scn, pos, 0, &iprim, &u2));
  BA(sdis_scene_find_closest_point(scn, pos, INF, NULL, &u2));
  BA(sdis_scene_find_closest_point(scn, pos, INF, &iprim, NULL));
  OK(sdis_scene_find_closest_point(scn, pos, INF, &iprim, &u2));

  CHK(eq_eps(u0, u1, 1.e-6));
  CHK(eq_eps(u1, u2, 1.e-6));
  CHK(iprim == 1);

  pos[0] = 0.5;
  pos[1] = 0.1;
  OK(sdis_scene_find_closest_point(scn, pos, INF, &iprim, &u2));
  CHK(iprim == 0);

  OK(sdis_scene_boundary_project_position(scn, 0, pos, &u0));
  CHK(eq_eps(u0, u2, 1.e-6));

  OK(sdis_scene_get_boundary_position(scn, iprim, &u2, pos1));
  dst = d2_len(d2_sub(pos1, pos, pos1));
  CHK(eq_eps(dst, 0.1, 1.e-6));

  OK(sdis_scene_find_closest_point(scn, pos, 0.09, &iprim, &u2));
  CHK(iprim == SDIS_PRIMITIVE_NONE);

  FOR_EACH(i, 0, 64) {
    u0 = rand_canonic();

    OK(sdis_scene_get_boundary_position(scn, 2, &u0, pos));
    OK(sdis_scene_boundary_project_position(scn, 2, pos, &u1));
    OK(sdis_scene_find_closest_point(scn, pos, INF, &iprim, &u2));
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
  OK(senc2d_scene_ref_put(scn2d));
  /* No 3D available */
  BA(sdis_scene_get_senc3d_scene(scn, &scn3d));

  BA(sdis_scene_ref_get(NULL));
  OK(sdis_scene_ref_get(scn));
  BA(sdis_scene_ref_put(NULL));
  OK(sdis_scene_ref_put(scn));
  OK(sdis_scene_ref_put(scn));
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = SDIS_INTERFACE_SHADER_NULL;
  (void)argc, (void)argv;

  interface_shader.convection_coef = DUMMY_INTERFACE_SHADER.convection_coef;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev));

  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));
  OK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, NULL, &interf));

  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));

  test_scene_3d(dev, interf);
  test_scene_2d(dev, interf);

  OK(sdis_device_ref_put(dev));
  OK(sdis_interface_ref_put(interf));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

