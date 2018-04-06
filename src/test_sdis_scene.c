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
#include <rsys/math.h>

struct context {
  const double* positions;
  const size_t* indices;
  struct sdis_interface* interf;
};

static INLINE void
get_indices(const size_t itri, size_t ids[3], void* context)
{
  struct context* ctx = context;
  CHK(ctx != NULL);
  CHK(itri < box_ntriangles);
  ids[0] = ctx->indices[itri*3+0];
  ids[1] = ctx->indices[itri*3+1];
  ids[2] = ctx->indices[itri*3+2];
}

static INLINE void
get_position(const size_t ivert, double pos[3], void* context)
{
  struct context* ctx = context;
  CHK(ctx != NULL);
  CHK(ivert < box_nvertices);
  pos[0] = ctx->positions[ivert*3+0];
  pos[1] = ctx->positions[ivert*3+1];
  pos[2] = ctx->positions[ivert*3+2];
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

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = DUMMY_INTERFACE_SHADER;
  double lower[3], upper[3];
  struct context ctx;
  size_t ntris, npos;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create(NULL, &allocator, 1, 0, &dev) == RES_OK);

  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_OK);
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_OK);
  CHK(sdis_interface_create
    (dev, solid, fluid, &interface_shader, NULL, &interf) == RES_OK);

  ctx.positions = box_vertices;
  ctx.indices = box_indices;
  ctx.interf = interf;
  ntris = box_ntriangles;
  npos = box_nvertices;

  #define CREATE sdis_scene_create
  #define IDS get_indices
  #define POS get_position
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

  CHK(sdis_scene_ref_get(NULL) == RES_BAD_ARG);
  CHK(sdis_scene_ref_get(scn) == RES_OK);
  CHK(sdis_scene_ref_put(NULL) == RES_BAD_ARG);
  CHK(sdis_scene_ref_put(scn) == RES_OK);
  CHK(sdis_scene_ref_put(scn) == RES_OK);

  CHK(sdis_device_ref_put(dev) == RES_OK);
  CHK(sdis_interface_ref_put(interf) == RES_OK);
  CHK(sdis_medium_ref_put(solid) == RES_OK);
  CHK(sdis_medium_ref_put(fluid) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

