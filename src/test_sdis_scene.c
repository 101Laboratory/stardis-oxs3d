/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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
  struct sdis_scene* scn = NULL;
  double lower[3], upper[3];
  double uv0[2], uv1[2], pos[3], pos1[3];
  struct context ctx;
  size_t ntris, npos;
  size_t i;

  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.interf = interf;
  ntris = box_ntriangles;
  npos = box_nvertices;

  #define CREATE sdis_scene_create
  #define IDS get_indices_3d
  #define POS get_position_3d
  #define IFA get_interface

  CHK(CREATE(NULL, 0, NULL, NULL, 0, NULL, &ctx, NULL) == RES_BAD_ARG);
  CHK(CREATE(dev, 0, IDS, IFA, npos, POS, &ctx, &scn) == RES_BAD_ARG);
  CHK(CREATE(dev, ntris, NULL, IFA, npos, POS, &ctx, &scn) == RES_BAD_ARG);
  CHK(CREATE(dev, ntris, IDS, NULL, npos, POS, &ctx, &scn) == RES_BAD_ARG);
  CHK(CREATE(dev, ntris, IDS, IFA, 0, POS, &ctx, &scn) == RES_BAD_ARG);
  CHK(CREATE(dev, ntris, IDS, IFA, npos, NULL, &ctx, &scn) == RES_BAD_ARG);
  CHK(CREATE(dev, ntris, IDS, IFA, npos, POS, &ctx, &scn) == RES_OK);

  #undef CREATE
  #undef IDS
  #undef POS
  #undef IFA

  CHK(sdis_scene_get_aabb(NULL, lower, upper) == RES_BAD_ARG);
  CHK(sdis_scene_get_aabb(scn, NULL, upper) == RES_BAD_ARG);
  CHK(sdis_scene_get_aabb(scn, lower, NULL) == RES_BAD_ARG);
  CHK(sdis_scene_get_aabb(scn, lower, upper) == RES_OK);
  CHK(eq_eps(lower[0], 0, 1.e-6));
  CHK(eq_eps(lower[1], 0, 1.e-6));
  CHK(eq_eps(lower[2], 0, 1.e-6));
  CHK(eq_eps(upper[0], 1, 1.e-6));
  CHK(eq_eps(upper[1], 1, 1.e-6));
  CHK(eq_eps(upper[2], 1, 1.e-6));

  uv0[0] = 0.3;
  uv0[1] = 0.3;

  CHK(sdis_scene_get_boundary_position(NULL, 6, uv0, pos) == RES_BAD_ARG);
  CHK(sdis_scene_get_boundary_position(scn, 12, uv0, pos) == RES_BAD_ARG);
  CHK(sdis_scene_get_boundary_position(scn, 6, NULL, pos) == RES_BAD_ARG);
  CHK(sdis_scene_get_boundary_position(scn, 6, uv0, NULL) == RES_BAD_ARG);
  CHK(sdis_scene_get_boundary_position(scn, 6, uv0, pos) == RES_OK);

  CHK(sdis_scene_boundary_project_position(NULL, 6, pos, uv1) == RES_BAD_ARG);
  CHK(sdis_scene_boundary_project_position(scn, 12, pos, uv1) == RES_BAD_ARG);
  CHK(sdis_scene_boundary_project_position(scn, 6, NULL, uv1) == RES_BAD_ARG);
  CHK(sdis_scene_boundary_project_position(scn, 6, pos, NULL) == RES_BAD_ARG);
  CHK(sdis_scene_boundary_project_position(scn, 6, pos, uv1) == RES_OK);

  CHK(d2_eq_eps(uv0, uv1, 1.e-6));

  FOR_EACH(i, 0, 64) {
    uv0[0] = rand_canonic();
    uv0[1] = rand_canonic() * (1 - uv0[0]);

    CHK(sdis_scene_get_boundary_position(scn, 4, uv0, pos) == RES_OK);
    CHK(sdis_scene_boundary_project_position(scn, 4, pos, uv1) == RES_OK);
    CHK(d2_eq_eps(uv0, uv1, 1.e-6));
  }

  pos[0] = 10;
  pos[1] = 0.1;
  pos[2] = 0.5;
  CHK(sdis_scene_boundary_project_position(scn, 6, pos, uv1) == RES_OK);
  CHK(sdis_scene_get_boundary_position(scn, 6, uv1, pos1) == RES_OK);
  CHK(!d3_eq_eps(pos1, pos, 1.e-6));

  CHK(sdis_scene_ref_get(NULL) == RES_BAD_ARG);
  CHK(sdis_scene_ref_get(scn) == RES_OK);
  CHK(sdis_scene_ref_put(NULL) == RES_BAD_ARG);
  CHK(sdis_scene_ref_put(scn) == RES_OK);
  CHK(sdis_scene_ref_put(scn) == RES_OK);
}

static void
test_scene_2d(struct sdis_device* dev, struct sdis_interface* interf)
{
  struct sdis_scene* scn = NULL;
  double lower[2], upper[2];
  double u0, u1, pos[2];
  struct context ctx;
  size_t nsegs, npos;
  size_t i;

  ctx.positions = square_vertices;
  ctx.indices = square_indices;
  ctx.interf = interf;
  nsegs = square_nsegments;
  npos = square_nvertices;

  #define CREATE sdis_scene_2d_create
  #define IDS get_indices_2d
  #define POS get_position_2d
  #define IFA get_interface

  CHK(CREATE(NULL, 0, NULL, NULL, 0, NULL, &ctx, NULL) == RES_BAD_ARG);
  CHK(CREATE(dev, 0, IDS, IFA, npos, POS, &ctx, &scn) == RES_BAD_ARG);
  CHK(CREATE(dev, nsegs, NULL, IFA, npos, POS, &ctx, &scn) == RES_BAD_ARG);
  CHK(CREATE(dev, nsegs, IDS, NULL, npos, POS, &ctx, &scn) == RES_BAD_ARG);
  CHK(CREATE(dev, nsegs, IDS, IFA, 0, POS, &ctx, &scn) == RES_BAD_ARG);
  CHK(CREATE(dev, nsegs, IDS, IFA, npos, NULL, &ctx, &scn) == RES_BAD_ARG);
  CHK(CREATE(dev, nsegs, IDS, IFA, npos, POS, &ctx, &scn) == RES_OK);

  #undef CREATE
  #undef IDS
  #undef POS
  #undef IFA

  CHK(sdis_scene_get_aabb(NULL, lower, upper) == RES_BAD_ARG);
  CHK(sdis_scene_get_aabb(scn, NULL, upper) == RES_BAD_ARG);
  CHK(sdis_scene_get_aabb(scn, lower, NULL) == RES_BAD_ARG);
  CHK(sdis_scene_get_aabb(scn, lower, upper) == RES_OK);
  CHK(eq_eps(lower[0], 0, 1.e-6));
  CHK(eq_eps(lower[1], 0, 1.e-6));
  CHK(eq_eps(upper[0], 1, 1.e-6));
  CHK(eq_eps(upper[1], 1, 1.e-6));

  u0 = 0.5;

  CHK(sdis_scene_get_boundary_position(NULL, 1, &u0, pos) == RES_BAD_ARG);
  CHK(sdis_scene_get_boundary_position(scn, 4, &u0, pos) == RES_BAD_ARG);
  CHK(sdis_scene_get_boundary_position(scn, 1, NULL, pos) == RES_BAD_ARG);
  CHK(sdis_scene_get_boundary_position(scn, 1, &u0, NULL) == RES_BAD_ARG);
  CHK(sdis_scene_get_boundary_position(scn, 1, &u0, pos) == RES_OK);

  CHK(sdis_scene_boundary_project_position(NULL, 1, pos, &u1) == RES_BAD_ARG);
  CHK(sdis_scene_boundary_project_position(scn, 4, pos, &u1) == RES_BAD_ARG);
  CHK(sdis_scene_boundary_project_position(scn, 1, NULL, &u1) == RES_BAD_ARG);
  CHK(sdis_scene_boundary_project_position(scn, 1, pos, NULL) == RES_BAD_ARG);
  CHK(sdis_scene_boundary_project_position(scn, 1, pos, &u1) == RES_OK);

  CHK(eq_eps(u0, u1, 1.e-6));

  FOR_EACH(i, 0, 64) {
    u0 = rand_canonic();

    CHK(sdis_scene_get_boundary_position(scn, 2, &u0, pos) == RES_OK);
    CHK(sdis_scene_boundary_project_position(scn, 2, pos, &u1) == RES_OK);
    CHK(eq_eps(u0, u1, 1.e-6));
  }

  d2(pos, 5, 0.5);
  CHK(sdis_scene_boundary_project_position(scn, 3, pos, &u0) == RES_OK);
  CHK(eq_eps(u0, 0.5, 1.e-6));

  d2(pos, 1, 2);
  CHK(sdis_scene_boundary_project_position(scn, 3, pos, &u0) == RES_OK);
  CHK(eq_eps(u0, 0, 1.e-6));

  d2(pos, 1, -1);
  CHK(sdis_scene_boundary_project_position(scn, 3, pos, &u0) == RES_OK);
  CHK(eq_eps(u0, 1, 1.e-6));

  CHK(sdis_scene_ref_put(scn) == RES_OK);
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

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create(NULL, &allocator, 1, 0, &dev) == RES_OK);

  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_OK);
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_OK);
  CHK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, NULL, &interf) == RES_OK);

  CHK(sdis_medium_ref_put(solid) == RES_OK);
  CHK(sdis_medium_ref_put(fluid) == RES_OK);

  test_scene_3d(dev, interf);
  test_scene_2d(dev, interf);

  CHK(sdis_device_ref_put(dev) == RES_OK);
  CHK(sdis_interface_ref_put(interf) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

