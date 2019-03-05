/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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
#include <rsys/stretchy_array.h>
#include <star/s3dut.h>

#include <string.h>

#define Tf0 300.0
#define Tf1 330.0

/*
 * The scene is composed of 2 super shape with unknown temperature. The first
 * super shape is surrounded by a fluid whose temperature is Tf0 while the
 * second one is in fluid whose temperatire is Tf1
 */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct context {
  struct s3dut_mesh_data msh0;
  struct s3dut_mesh_data msh1;
  struct sdis_interface* interf0;
  struct sdis_interface* interf1;
};

static void
get_indices(const size_t itri, size_t ids[3], void* context)
{
  const struct context* ctx = context;
  if(itri < ctx->msh0.nprimitives) {
    ids[0] = ctx->msh0.indices[itri*3+0];
    ids[2] = ctx->msh0.indices[itri*3+1];
    ids[1] = ctx->msh0.indices[itri*3+2];
  } else {
    const size_t itri2 = itri - ctx->msh0.nprimitives;
    ids[0] = ctx->msh1.indices[itri2*3+0] + ctx->msh0.nvertices;
    ids[2] = ctx->msh1.indices[itri2*3+1] + ctx->msh0.nvertices;
    ids[1] = ctx->msh1.indices[itri2*3+2] + ctx->msh0.nvertices;
  }
}

static void
get_position(const size_t ivert, double pos[3], void* context)
{
  const struct context* ctx = context;
  if(ivert < ctx->msh0.nvertices) {
    pos[0] = ctx->msh0.positions[ivert*3+0] - 2.0;
    pos[1] = ctx->msh0.positions[ivert*3+1];
    pos[2] = ctx->msh0.positions[ivert*3+2];
  } else {
    const size_t ivert2 = ivert - ctx->msh0.nvertices;
    pos[0] = ctx->msh1.positions[ivert2*3+0] + 2.0;
    pos[1] = ctx->msh1.positions[ivert2*3+1];
    pos[2] = ctx->msh1.positions[ivert2*3+2];
  }
}

static void
get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  const struct context* ctx = context;
  *bound = itri < ctx->msh0.nprimitives ? ctx->interf0 : ctx->interf1;
}

/*******************************************************************************
 * Fluid medium
 ******************************************************************************/
struct fluid {
  double temperature;
};

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct fluid*)sdis_data_cget(data))->temperature;
}

/*******************************************************************************
 * Solid medium
 ******************************************************************************/
struct solid {
  double cp;
  double lambda;
  double rho;
  double delta;
  double temperature;
};

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->cp;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->lambda;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->rho;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->delta;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->temperature;
}

struct interf {
  double hc;
  double epsilon;
  double specular_fraction;
};

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->hc;
}

static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->epsilon;
}

static double
interface_get_specular_fraction
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->specular_fraction;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3dut_super_formula f0 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_super_formula f1 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_mesh* msh0 = NULL;
  struct s3dut_mesh* msh1 = NULL;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid0 = NULL;
  struct sdis_medium* solid1 = NULL;
  struct sdis_medium* fluid0 = NULL;
  struct sdis_medium* fluid1 = NULL;
  struct sdis_interface* solid0_fluid0 = NULL;
  struct sdis_interface* solid1_fluid1 = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_data* data = NULL;
  struct sdis_estimator* estimator = NULL;
  struct fluid* fluid_param = NULL;
  struct solid* solid_param = NULL;
  struct interf* interface_param = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interface_shader = SDIS_INTERFACE_SHADER_NULL;
  struct context ctx;
  const double trange[2] = {0, INF};
  const size_t N = 1000;
  size_t nreals;
  size_t nfails;
  size_t ntris;
  size_t nverts;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev));

  fluid_shader.temperature = fluid_get_temperature;

  /* Create the fluid0 medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tf0;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid0));
  OK(sdis_data_ref_put(data));

  /* Create the fluid1 medium */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = 330;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid1));
  OK(sdis_data_ref_put(data));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid0 medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 1.0;
  solid_param->lambda = 0.1;
  solid_param->rho = 1.0;
  solid_param->delta = 1.0/20.0;
  solid_param->temperature = -1; /* Unknown temperature */
  OK(sdis_solid_create(dev, &solid_shader, data, &solid0));
  OK(sdis_data_ref_put(data));

  /* Create the solid1 medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->cp = 1.0;
  solid_param->lambda = 10.0;
  solid_param->rho = 1.0;
  solid_param->delta = 1.0/20.0;
  solid_param->temperature = -1; /* Unknown temperature */
  OK(sdis_solid_create(dev, &solid_shader, data, &solid1));
  OK(sdis_data_ref_put(data));

  /* Create the interfaces */
  OK(sdis_data_create(dev, sizeof(struct interf),
    ALIGNOF(struct interf), NULL, &data));
  interface_param = sdis_data_get(data);
  interface_param->hc = 0.5;
  interface_param->epsilon = 0;
  interface_param->specular_fraction = 0;
  interface_shader.convection_coef = interface_get_convection_coef;
  interface_shader.front = SDIS_INTERFACE_SIDE_SHADER_NULL;
  interface_shader.back.temperature = NULL;
  interface_shader.back.emissivity = interface_get_emissivity;
  interface_shader.back.specular_fraction = interface_get_specular_fraction;
  OK(sdis_interface_create
    (dev, solid0, fluid0, &interface_shader, data, &solid0_fluid0));
  OK(sdis_interface_create
    (dev, solid1, fluid1, &interface_shader, data, &solid1_fluid1));
  OK(sdis_data_ref_put(data));

  /* Create the mesh0 */
  f0.A = 1; f0.B = 1; f0.M = 3; f0.N0 = 1; f0.N1 = 1; f0.N2 = 2;
  f1.A = 1; f1.B = 1; f1.M = 10; f1.N0 = 1; f1.N1 = 1; f1.N2 = 3;
  OK(s3dut_create_super_shape(&allocator, &f0, &f1, 1, 64, 32, &msh0));
  OK(s3dut_mesh_get_data(msh0, &ctx.msh0));

  /* Create the mesh1 */
  f0.A = 1; f0.B = 1; f0.M = 10; f0.N0 = 1; f0.N1 = 1; f0.N2 = 5;
  f1.A = 1; f1.B = 1; f1.M = 1; f1.N0 = 1; f1.N1 = 1; f1.N2 = 1;
  OK(s3dut_create_super_shape(&allocator, &f0, &f1, 1, 64, 32, &msh1));
  OK(s3dut_mesh_get_data(msh1, &ctx.msh1));

  /* Create the scene */
  ctx.interf0 = solid0_fluid0;
  ctx.interf1 = solid1_fluid1;
  ntris = ctx.msh0.nprimitives + ctx.msh1.nprimitives;
  nverts = ctx.msh0.nvertices + ctx.msh1.nvertices;
#if 0
  {
    double* vertices = NULL;
    size_t* indices = NULL;
    size_t i;
    CHK(vertices = MEM_CALLOC(&allocator, nverts*3, sizeof(*vertices)));
    CHK(indices = MEM_CALLOC(&allocator, ntris*3, sizeof(*indices)));
    FOR_EACH(i, 0, ntris) get_indices(i, indices + i*3, &ctx);
    FOR_EACH(i, 0, nverts) get_position(i, vertices + i*3, &ctx);
    dump_mesh(stdout, vertices, nverts, indices, ntris);
    MEM_RM(&allocator, vertices);
    MEM_RM(&allocator, indices);
  }
#endif

  OK(sdis_scene_create(dev, ntris, get_indices, get_interface, nverts,
    get_position, &ctx, &scn));

  BA(sdis_solve_medium(NULL, N, solid0, trange, 1.f, -1, 0, 0, &estimator));
  BA(sdis_solve_medium(scn, 0, solid0, trange, 1.f, -1, 0, 0, &estimator));
  BA(sdis_solve_medium(scn, N, NULL, trange, 1.f, -1, 0, 0, &estimator));
  BA(sdis_solve_medium(scn, N, solid0, NULL, 1.f, -1, 0, 0, &estimator));
  BA(sdis_solve_medium(scn, N, solid0, trange, 0.f, -1, 0, 0, &estimator));
  BA(sdis_solve_medium(scn, N, solid0, trange, 1.f, -1, 0, 0, NULL));
  OK(sdis_solve_medium(scn, N, solid0, trange, 1.f, -1, 0, 0, &estimator));

  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &T));
  printf("Solid0 = %g ~ %g +/- %g\n", Tf0, T.E, T.SE);
  printf("#failures = %lu/%lu\n", nfails, N);
  CHK(eq_eps(T.E, Tf0, T.SE));
  CHK(nreals + nfails == N);
  OK(sdis_estimator_ref_put(estimator));

  OK(sdis_solve_medium(scn, N, solid1, trange, 1.f, -1, 0, 0, &estimator));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_get_temperature(estimator, &T));
  printf("Solid1 = %g ~ %g +/- %g\n", Tf1, T.E, T.SE);
  printf("#failures = %lu/%lu\n", nfails, N);
  CHK(eq_eps(T.E, Tf1, T.SE));
  CHK(nreals + nfails == N);
  OK(sdis_estimator_ref_put(estimator));

  /* Release */
  OK(s3dut_mesh_ref_put(msh0));
  OK(s3dut_mesh_ref_put(msh1));
  OK(sdis_device_ref_put(dev));
  OK(sdis_medium_ref_put(fluid0));
  OK(sdis_medium_ref_put(fluid1));
  OK(sdis_medium_ref_put(solid0));
  OK(sdis_medium_ref_put(solid1));
  OK(sdis_interface_ref_put(solid0_fluid0));
  OK(sdis_interface_ref_put(solid1_fluid1));
  OK(sdis_scene_ref_put(scn));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;

}
