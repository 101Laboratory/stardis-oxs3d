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

#include <star/s3dut.h>
#include <rsys/math.h>

#define Tfluid 350.0 /* Temperature of the fluid in Kelvin */
#define Hcoef 1.0 /* Convection coefficient */
#define Nreals 10000 /* #realisations */
#define UNKNOWN -1

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
compute_aabb
  (const double* positions,
   const size_t nvertices,
   double lower[3],
   double upper[3])
{
  size_t i;
  CHK(positions && nvertices && lower && upper);

  lower[0] = lower[1] = lower[2] = DBL_MAX;
  upper[0] = upper[1] = upper[2] =-DBL_MAX;

  FOR_EACH(i, 0, nvertices) {
    lower[0] = MMIN(lower[0], positions[i*3+0]);
    lower[1] = MMIN(lower[1], positions[i*3+1]);
    lower[2] = MMIN(lower[2], positions[i*3+2]);
    upper[0] = MMAX(upper[0], positions[i*3+0]);
    upper[1] = MMAX(upper[1], positions[i*3+1]);
    upper[2] = MMAX(upper[2], positions[i*3+2]);
  }
}

static double
trilinear_temperature(const double pos[3])
{
  const double a = 333;
  const double b = 432;
  const double c = 579;
  double x, y, z;
  CHK(pos[0] >= -10.0 && pos[0] <= 10.0);
  CHK(pos[1] >= -10.0 && pos[1] <= 10.0);
  CHK(pos[2] >= -10.0 && pos[2] <= 10.0);

  x = (pos[0] + 10.0) / 20.0;
  y = (pos[1] + 10.0) / 20.0;
  z = (pos[2] + 10.0) / 20.0;

  return a*x + b*y + c*z;
}

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct context {
  struct s3dut_mesh_data msh;
  struct sdis_interface* interf;
};

static void
get_indices(const size_t itri, size_t ids[3], void* context)
{
  const struct context* ctx = context;
  CHK(ids && ctx && itri < ctx->msh.nprimitives);
  ids[0] = ctx->msh.indices[itri*3+0];
  ids[2] = ctx->msh.indices[itri*3+1];
  ids[1] = ctx->msh.indices[itri*3+2];
}

static void
get_position(const size_t ivert, double pos[3], void* context)
{
  const struct context* ctx = context;
  CHK(pos && ctx && ivert < ctx->msh.nvertices);
  pos[0] = ctx->msh.positions[ivert*3+0];
  pos[1] = ctx->msh.positions[ivert*3+1];
  pos[2] = ctx->msh.positions[ivert*3+2];
}

static void
get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  const struct context* ctx = context;
  CHK(bound && ctx && itri < ctx->msh.nprimitives);
  *bound = ctx->interf;
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
struct interf {
  double h;
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf;
  CHK(data != NULL && frag != NULL);
  interf = sdis_data_cget(data);
  return interf->h > 0 ? UNKNOWN : trilinear_temperature(frag->P);
}

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->h;;
}

/*******************************************************************************
 * Fluid
 ******************************************************************************/
static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL);
  (void)data;
  return Tfluid;
}

/*******************************************************************************
 * Solid API
 ******************************************************************************/
struct solid {
  double delta;
  double cp;
  double lambda;
  double rho;
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

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3dut_super_formula f0 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_super_formula f1 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_mesh* msh = NULL;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_device* dev = NULL;
  struct sdis_data* data = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct interf* interf_param = NULL;
  struct solid* solid_param = NULL;
  struct context ctx;
  double probe[3];
  double lower[3];
  double upper[3];
  double size[3];
  const double time[2] = {DBL_MAX, DBL_MAX};
  double ref;
  size_t nreals;
  size_t nfails;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev));

  /* Create the fluid medium */
  fluid_shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->delta = 0.01;
  solid_param->lambda = 10;
  solid_param->cp = 1.0;
  solid_param->rho = 1.0;
  solid_param->temperature = -1;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Create the interface */
  OK(sdis_data_create
    (dev, sizeof(struct interf), ALIGNOF(struct interf), NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 1;
  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;
  OK(sdis_interface_create(dev, solid, fluid, &interf_shader, data, &interf));
  OK(sdis_data_ref_put(data));

  /* Create the solid super shape */
  f0.A = 1; f0.B = 1; f0.M = 20; f0.N0 = 1; f0.N1 = 1; f0.N2 = 5;
  f1.A = 1; f1.B = 1; f1.M = 7; f1.N0 = 1; f1.N1 = 2; f1.N2 = 5;
  OK(s3dut_create_super_shape(&allocator, &f0, &f1, 1, 128, 64, &msh));
  OK(s3dut_mesh_get_data(msh, &ctx.msh));

  compute_aabb(ctx.msh.positions, ctx.msh.nvertices, lower, upper);

  /* Create the scene */
  ctx.interf = interf;
  OK(sdis_scene_create(dev, ctx.msh.nprimitives, get_indices, get_interface,
    ctx.msh.nvertices, get_position, &ctx, &scn));
  /*dump_mesh(stdout, ctx.msh.positions,
     ctx.msh.nvertices, ctx.msh.indices, ctx.msh.nprimitives);*/

  /* Compute the delta of the solid random walk */
  size[0] = upper[0] - lower[0];
  size[1] = upper[1] - lower[1];
  size[2] = upper[2] - lower[2];
  solid_param->delta = MMIN(MMIN(size[0], size[1]), size[2]) / 20.0;

  /* Launch probe estimation */
  probe[0] = 0;
  probe[1] = 0;
  probe[2] = 0;
  interf_param->h = 0;
  OK(sdis_solve_probe(scn, Nreals, probe, time, 1.0, -1, 0,
    SDIS_HEAT_PATH_FAILED, &estimator));

  /* Print estimation results */
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_ref_put(estimator));
  CHK(nfails < Nreals * 0.0001);
  ref = trilinear_temperature(probe);
  printf("T = %g ~ %g +/- %g; #failures = %lu / %lu\n",
    ref, T.E, T.SE, (unsigned long)nfails, (unsigned long)Nreals);
  CHK(eq_eps(T.E, ref, T.SE*3));

  /* Launch medium integration */
  interf_param->h = 1; /* H delta T */
  OK(sdis_solve_medium(scn, Nreals, solid, time, 1.0, -1, 0,
    SDIS_HEAT_PATH_FAILED, &estimator));
  /*dump_heat_paths(stdout, estimator);*/

  /* Print estimation results */
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  OK(sdis_estimator_ref_put(estimator));
  printf("T ~ %g +/- %g; #failures = %lu / %lu\n", T.E, T.SE,
    (unsigned long)nfails, (unsigned long)Nreals);
  CHK(nfails < Nreals * 0.001);

  /* Release */
  OK(s3dut_mesh_ref_put(msh));
  OK(sdis_device_ref_put(dev));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_scene_ref_put(scn));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

