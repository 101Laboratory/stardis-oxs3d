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

#include <star/s3dut.h>
#include <rsys_math.h>

#define Tfluid 350.0 /* Temperature of the fluid in Kelvin */
#define LAMBDA 10.0 /* Thermal conductivity */
#define Hcoef 1.0 /* Convection coefficient */
#define Pw 10000.0 /* Volumetric power */
#define Nreals 10000 /* #realisations */

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
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

static double
volumetric_temperature(const double pos[3], const double upper[3])
{
  const double beta = - 1.0 / 3.0 * Pw / (2*LAMBDA);
  const double temp =
    beta * (pos[0]*pos[0] - upper[0]*upper[0])
  + beta * (pos[1]*pos[1] - upper[1]*upper[1])
  + beta * (pos[2]*pos[2] - upper[2]*upper[2]);
  CHK(temp > 0);
  return temp;
}

static const char*
algo_cstr(const enum sdis_diffusion_algorithm diff_algo)
{
  const char* cstr = "none";

  switch(diff_algo) {
    case SDIS_DIFFUSION_DELTA_SPHERE: cstr = "delta sphere"; break;
    case SDIS_DIFFUSION_WOS: cstr = "WoS"; break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return cstr;
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

static struct sdis_scene*
create_scene(struct sdis_device* dev, struct sdis_interface* interf)
{
  /* Star-3DUT */
  struct s3dut_super_formula f0 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_super_formula f1 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_mesh* msh = NULL;

  /* Stardis */
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_scene* scn = NULL;

  /* Miscellaneous */
  struct context ctx;

  ASSERT(dev && interf);

  /* Create the solid super shape */
  f0.A = 1; f0.B = 1; f0.M = 20; f0.N0 = 1; f0.N1 = 1; f0.N2 = 5;
  f1.A = 1; f1.B = 1; f1.M = 7; f1.N0 = 1; f1.N1 = 2; f1.N2 = 5;
  OK(s3dut_create_super_shape(NULL, &f0, &f1, 1, 128, 64, &msh));
  OK(s3dut_mesh_get_data(msh, &ctx.msh));

  /*dump_mesh(stdout, ctx.msh.positions,
     ctx.msh.nvertices, ctx.msh.indices, ctx.msh.nprimitives);*/

  /* Create the scene */
  ctx.interf = interf;
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = ctx.msh.nprimitives;
  scn_args.nvertices = ctx.msh.nvertices;
  scn_args.context = &ctx;
  OK(sdis_scene_create(dev, &scn_args, &scn));

  OK(s3dut_mesh_ref_put(msh));

  return scn;
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
enum profile {
  PROFILE_UNKNOWN,
  PROFILE_TRILINEAR,
  PROFILE_VOLUMETRIC_POWER,
  PROFILE_COUNT__
};
struct interf {
  enum profile profile;
  double upper[3]; /* Upper bound of the scene */
  double h;
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf;
  double temperature;
  CHK(data != NULL && frag != NULL);
  interf = sdis_data_cget(data);
  switch(interf->profile) {
    case PROFILE_UNKNOWN:
      temperature = SDIS_TEMPERATURE_NONE;
      break;
    case PROFILE_VOLUMETRIC_POWER:
      temperature = volumetric_temperature(frag->P, interf->upper);
      break;
    case PROFILE_TRILINEAR:
      temperature = trilinear_temperature(frag->P);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return temperature;
}

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->h;;
}

static struct sdis_interface*
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back)
{
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_data* data = NULL;
  struct interf* interf_param = NULL;
  ASSERT(dev && front && back);

  OK(sdis_data_create
    (dev, sizeof(struct interf), ALIGNOF(struct interf), NULL, &data));
  interf_param = sdis_data_get(data);
  interf_param->h = 1;

  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;
  OK(sdis_interface_create(dev, front, back, &interf_shader, data, &interf));
  OK(sdis_data_ref_put(data));

  return interf;
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
static struct sdis_medium*
create_fluid(struct sdis_device* dev)
{
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_medium* fluid = NULL;
  ASSERT(dev);

  /* Create the fluid medium */
  fluid_shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  return fluid;
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
  double power;
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

static double
solid_get_volumetric_power
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->power;
}

static struct sdis_medium*
create_solid(struct sdis_device* dev)
{
  struct sdis_solid_shader solid_shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_data* data = NULL;
  struct solid* solid_param;
  ASSERT(dev);

  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  solid_param->delta = 0.01;
  solid_param->lambda = 10;
  solid_param->cp = 1.0;
  solid_param->rho = 1.0;
  solid_param->temperature = SDIS_TEMPERATURE_NONE;
  solid_param->power = SDIS_VOLUMIC_POWER_NONE;

  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  solid_shader.volumic_power = solid_get_volumetric_power;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  return solid;
}

/*******************************************************************************
 * Solve functions
 ******************************************************************************/
static void
check_estimation
  (const struct sdis_estimator* estimator, const double Tref)
{
  struct sdis_mc T = SDIS_MC_NULL;
  size_t nreals;
  size_t nfails;

  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  CHK(nfails <= Nreals * 0.0005);
  printf("T = %g ~ %g +/- %g [%g, %g]; #failures = %lu / %lu\n",
    Tref, T.E, T.SE, T.E - 3*T.SE, T.E + 3*T.SE,
    (unsigned long)nfails, (unsigned long)Nreals);
  CHK(eq_eps(T.E, Tref, T.SE*3));
}

static void
check_probe
  (struct sdis_scene* scn,
   const enum profile profile,
   const enum sdis_diffusion_algorithm diff_algo)
{
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_estimator* estimator = NULL;
  double lower[3], upper[3];
  double Tref;
  ASSERT(scn);

  printf("algo: %s\n", algo_cstr(diff_algo));

  solve_args.nrealisations = Nreals;
  solve_args.position[0] = 0;
  solve_args.position[1] = 0;
  solve_args.position[2] = 0;
  solve_args.time_range[0] = INF;
  solve_args.time_range[1] = INF;
  solve_args.diff_algo = diff_algo;
  solve_args.register_paths = SDIS_HEAT_PATH_FAILURE;
  OK(sdis_solve_probe(scn, &solve_args, &estimator));

  switch(profile) {
    case PROFILE_TRILINEAR:
      Tref = trilinear_temperature(solve_args.position);
      break;
    case PROFILE_VOLUMETRIC_POWER:
      OK(sdis_scene_get_aabb(scn, lower, upper));
     Tref = volumetric_temperature(solve_args.position, upper);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

  check_estimation(estimator, Tref);
  OK(sdis_estimator_ref_put(estimator));
}

static void
check_medium
  (struct sdis_scene* scn,
   struct sdis_medium* medium,
   const enum sdis_diffusion_algorithm diff_algo)
{
  struct sdis_solve_medium_args solve_mdm_args = SDIS_SOLVE_MEDIUM_ARGS_DEFAULT;
  struct sdis_estimator* estimator = NULL;
  ASSERT(scn);

  printf("algo: %s\n", algo_cstr(diff_algo));

  solve_mdm_args.nrealisations = Nreals;
  solve_mdm_args.medium = medium;
  solve_mdm_args.time_range[0] = INF;
  solve_mdm_args.time_range[1] = INF;
  solve_mdm_args.diff_algo = diff_algo;
  OK(sdis_solve_medium(scn, &solve_mdm_args, &estimator));

  check_estimation(estimator, Tfluid);
  /*dump_heat_paths(stdout, estimator);*/
  OK(sdis_estimator_ref_put(estimator));
}

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_scene* scn = NULL;
  struct interf* interf_param = NULL;
  struct solid* solid_param = NULL;
  double lower[3];
  double upper[3];
  double spread;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  fluid = create_fluid(dev);
  solid = create_solid(dev);
  interf = create_interface(dev, solid, fluid);
  scn = create_scene(dev, interf);

  solid_param = sdis_data_get(sdis_medium_get_data(solid));
  interf_param = sdis_data_get(sdis_interface_get_data(interf));

  OK(sdis_scene_get_medium_spread(scn, solid, &spread));
  OK(sdis_scene_get_aabb(scn, lower, upper));

  /* Compute the delta of the solid random walk */
  solid_param->delta = 0.4 / spread;

  /* Setup the upper boundary required to solve the trilinear profile */
  interf_param->upper[0] = upper[0];
  interf_param->upper[1] = upper[1];
  interf_param->upper[2] = upper[2];

  /* Launch probe estimation with trilinear profile set at interfaces */
  solid_param->delta = 0.4 / spread;
  interf_param->profile = PROFILE_TRILINEAR;
  check_probe(scn, PROFILE_TRILINEAR, SDIS_DIFFUSION_DELTA_SPHERE);
  solid_param->delta = 1e-5 / spread; /* Make life difficult for WoS */
  check_probe(scn, PROFILE_TRILINEAR, SDIS_DIFFUSION_WOS);

  /* Launch probe estimation with volumetric power profile set at interfaces */
  solid_param->delta = 0.4 / spread;
  solid_param->power = Pw;
  interf_param->profile = PROFILE_VOLUMETRIC_POWER;
  check_probe(scn, PROFILE_VOLUMETRIC_POWER, SDIS_DIFFUSION_DELTA_SPHERE);
  solid_param->delta = 1e-5 / spread; /* Make life difficult for WoS */
  check_probe(scn, PROFILE_VOLUMETRIC_POWER, SDIS_DIFFUSION_WOS);
  solid_param->power = SDIS_VOLUMIC_POWER_NONE;

  /* Launch medium integration. Do not use an analytic profile as a boundary
   * condition but a Robin boundary condition */
  interf_param->profile = PROFILE_UNKNOWN;
  solid_param->delta = 0.4 / spread;
  check_medium(scn, solid, SDIS_DIFFUSION_DELTA_SPHERE);

  /* Contrary to previous WoS tests, don't reduce the delta parameter to avoid
   * prohibitive increases in calculation time: too small a delta would trap the
   * path in the solid */
  check_medium(scn, solid, SDIS_DIFFUSION_WOS);

  /* Release */
  OK(sdis_device_ref_put(dev));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_scene_ref_put(scn));

  CHK(mem_allocated_size() == 0);
  return 0;
}
