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

#include <rsys/mem_allocator.h>

/*
 * The system is an unsteady-state temperature profile, meaning that at any
 * point, at any time, we can analytically calculate the temperature. We
 * immerse in this temperature field a supershape representing a solid in which
 * we want to evaluate the temperature by Monte Carlo at a given position and
 * observation time. On the Monte Carlo side, the temperature of the supershape
 * is unknown. Only the boundary temperature is fixed at the temperature of the
 * unsteady trilinear profile mentioned above. Monte Carlo would then have to
 * find the temperature defined by the unsteady profile.
 *
 *      T(z)             /\ <-- T(x,y,z,t)
 *       |  T(y)     ___/  \___
 *       |/          \  . T=? /
 *       o--- T(x)   /_  __  _\
 *                     \/  \/
 */

#define LAMBDA 0.1  /* [W/(m.K)] */
#define RHO 25.0 /* [kg/m^3] */
#define CP 2.0 /* [J/K/kg)] */
#define DELTA 1.0/20.0 /* [m/fp_to_meter] */

#define NREALISATIONS 100000

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static double
temperature(const double pos[3], const double time)
{
  const double kx = PI/4.0;
  const double ky = PI/4.0;
  const double kz = PI/4.0;
  const double alpha = LAMBDA / (RHO*CP); /* Diffusivity */

  const double A = 0; /* No termal source */
  const double B1 = 10;
  const double B2 = 1000;

  double x, y, z, t;
  double a, b, c;
  double temp;

  ASSERT(pos);

  x = pos[0];
  y = pos[1];
  z = pos[2];
  t = time;

  a = B1*(x*x*x*z-3*x*y*y*z);
  b = B2*sin(kx*x)*sin(ky*y)*sin(kz*z)*exp(-alpha*(kx*kx + ky*ky + kz*kz)*t);
  c = A * x*x*x*x * y*y*y * z*z;

  temp = (a + b - c) / LAMBDA;
  return temp;
}

static INLINE void
dump_s3dut_mesh(FILE* fp, const struct s3dut_mesh* mesh)
{
  struct s3dut_mesh_data mesh_data;

  OK(s3dut_mesh_get_data(mesh, &mesh_data));
  dump_mesh(fp, mesh_data .positions, mesh_data.nvertices,
    mesh_data.indices, mesh_data.nprimitives);
}

static void
dump_paths
  (FILE* fp,
   struct sdis_scene* scn,
   const enum sdis_diffusion_algorithm diff_algo,
   const double pos[3],
   const double time,
   const size_t npaths)
{
  struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_estimator* estimator = NULL;

  args.nrealisations = npaths;
  args.position[0] = pos[0];
  args.position[1] = pos[1];
  args.position[2] = pos[2];
  args.time_range[0] = time;
  args.time_range[1] = time;
  args.diff_algo = diff_algo;
  args.register_paths = SDIS_HEAT_PATH_ALL;
  OK(sdis_solve_probe(scn, &args, &estimator));

  dump_heat_paths(fp, estimator);

  OK(sdis_estimator_ref_put(estimator));
}

/*******************************************************************************
 * Geometry
 ******************************************************************************/
static struct s3dut_mesh*
create_super_shape(void)
{
  struct s3dut_mesh* mesh = NULL;
  struct s3dut_super_formula f0 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_super_formula f1 = S3DUT_SUPER_FORMULA_NULL;
  const double radius = 1;
  const unsigned nslices = 256;

  f0.A = 1.5; f0.B = 1; f0.M = 11.0; f0.N0 = 1; f0.N1 = 1; f0.N2 = 2.0;
  f1.A = 1.0; f1.B = 2; f1.M =  3.6; f1.N0 = 1; f1.N1 = 2; f1.N2 = 0.7;
  OK(s3dut_create_super_shape(NULL, &f0, &f1, radius, nslices, nslices/2, &mesh));

  return mesh;
}

/*******************************************************************************
 * Scene, i.e. the system to simulate
 ******************************************************************************/
struct scene_context {
  struct s3dut_mesh_data mesh_data;
  struct sdis_interface* interf;
};
static const struct scene_context SCENE_CONTEXT_NULL = {{0}, NULL};

static void
scene_get_indices(const size_t itri, size_t ids[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(ids && context && itri < context->mesh_data.nprimitives);
  /* Flip the indices to ensure that the normal points into the super shape */
  ids[0] = (unsigned)context->mesh_data.indices[itri*3+0];
  ids[1] = (unsigned)context->mesh_data.indices[itri*3+2];
  ids[2] = (unsigned)context->mesh_data.indices[itri*3+1];
}

static void
scene_get_interface(const size_t itri, struct sdis_interface** interf, void* ctx)
{
  struct scene_context* context = ctx;
  CHK(interf && context && itri < context->mesh_data.nprimitives);
  *interf = context->interf;
}

static void
scene_get_position(const size_t ivert, double pos[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(pos && context && ivert < context->mesh_data.nvertices);
  pos[0] = context->mesh_data.positions[ivert*3+0];
  pos[1] = context->mesh_data.positions[ivert*3+1];
  pos[2] = context->mesh_data.positions[ivert*3+2];
}

static struct sdis_scene*
create_scene
  (struct sdis_device* sdis,
   const struct s3dut_mesh* mesh,
   struct sdis_interface* interf)
{
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct scene_context context = SCENE_CONTEXT_NULL;

  OK(s3dut_mesh_get_data(mesh, &context.mesh_data));
  context.interf = interf;

  scn_args.get_indices = scene_get_indices;
  scn_args.get_interface = scene_get_interface;
  scn_args.get_position = scene_get_position;
  scn_args.nprimitives = context.mesh_data.nprimitives;
  scn_args.nvertices = context.mesh_data.nvertices;
  scn_args.context = &context;
  OK(sdis_scene_create(sdis, &scn_args, &scn));
  return scn;
}

/*******************************************************************************
 * Solid, i.e. medium of the super shape
 ******************************************************************************/
#define SOLID_PROP(Prop, Val)                                                  \
  static double                                                                \
  solid_get_##Prop                                                             \
    (const struct sdis_rwalk_vertex* vtx,                                      \
     struct sdis_data* data)                                                   \
  {                                                                            \
    (void)vtx, (void)data; /* Avoid the "unused variable" warning */           \
    return Val;                                                                \
  }
SOLID_PROP(calorific_capacity, CP) /* [J/K/kg] */
SOLID_PROP(thermal_conductivity, LAMBDA) /* [W/m/K] */
SOLID_PROP(volumic_mass, RHO) /* [kg/m^3] */
SOLID_PROP(delta, DELTA) /* [m/fp_to_meter] */
SOLID_PROP(temperature, SDIS_TEMPERATURE_NONE/*<=> unknown*/) /* [K] */
#undef SOLID_PROP

static struct sdis_medium*
create_solid(struct sdis_device* sdis)
{
  struct sdis_solid_shader shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_medium* solid = NULL;

  shader.calorific_capacity = solid_get_calorific_capacity;
  shader.thermal_conductivity = solid_get_thermal_conductivity;
  shader.volumic_mass = solid_get_volumic_mass;
  shader.delta = solid_get_delta;
  shader.temperature = solid_get_temperature;
  shader.t0 = -INF;
  OK(sdis_solid_create(sdis, &shader, NULL, &solid));
  return solid;
}

/*******************************************************************************
 * Dummy environment, i.e. environment surrounding the super shape. It is
 * defined only for Stardis compliance: in Stardis, an interface must divide 2
 * media.
 ******************************************************************************/
static struct sdis_medium*
create_dummy(struct sdis_device* sdis)
{
  struct sdis_fluid_shader shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_medium* dummy = NULL;

  shader.calorific_capacity = dummy_medium_getter;
  shader.volumic_mass = dummy_medium_getter;
  shader.temperature = dummy_medium_getter;
  OK(sdis_fluid_create(sdis, &shader, NULL, &dummy));
  return dummy;
}

/*******************************************************************************
 * Interface: its temperature is fixed to the unsteady-state profile
 ******************************************************************************/
static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  (void)data;
  return temperature(frag->P, frag->time);
}

static struct sdis_interface*
create_interface
  (struct sdis_device* sdis,
   struct sdis_medium* front,
   struct sdis_medium* back)
{
  struct sdis_interface* interf = NULL;
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;

  shader.front.temperature = interface_get_temperature;
  shader.back.temperature = interface_get_temperature;
  OK(sdis_interface_create(sdis, front, back, &shader, NULL, &interf));
  return interf;
}

/*******************************************************************************
 * Validations
 ******************************************************************************/
static void
check_probe
  (struct sdis_scene* scn,
   const enum sdis_diffusion_algorithm diff_algo,
   const double pos[3],
   const double time,  /* [s] */
   const int green)
{
  struct sdis_solve_probe_args args  = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  double ref = 0;

  args.nrealisations = NREALISATIONS;
  args.position[0] = pos[0];
  args.position[1] = pos[1];
  args.position[2] = pos[2];
  args.time_range[0] = time;
  args.time_range[1] = time;
  args.diff_algo = diff_algo;

  if(!green) {
    OK(sdis_solve_probe(scn, &args, &estimator));
  } else {
    struct sdis_green_function* greenfn = NULL;

    OK(sdis_solve_probe_green_function(scn, &args, &greenfn));
    OK(sdis_green_function_solve(greenfn, &estimator));
    OK(sdis_green_function_ref_put(greenfn));
  }

  OK(sdis_estimator_get_temperature(estimator, &T));

  ref = temperature(pos, time);
  printf("T(%g, %g, %g, %g) = %g ~ %g +/- %g\n",
    SPLIT3(pos), time, ref, T.E, T.SE);
  CHK(eq_eps(ref, T.E, 3*T.SE));

  OK(sdis_estimator_ref_put(estimator));
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  /* Stardis */
  struct sdis_device* sdis = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* dummy = NULL; /* Medium surrounding the solid */
  struct sdis_scene* scn = NULL;

  /* Miscellaneous */
  FILE* fp = NULL;
  struct s3dut_mesh* super_shape = NULL;
  const double pos[3] = {0.2,0.3,0.4}; /* [m/fp_to_meter] */
  const double time = 5; /* [s] */

  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &sdis));

  super_shape = create_super_shape();

  /* Save the super shape geometry for debug and visualisation */
  CHK(fp = fopen("super_shape_3d.obj", "w"));
  dump_s3dut_mesh(fp, super_shape);
  CHK(fclose(fp) == 0);

  solid = create_solid(sdis);
  dummy = create_dummy(sdis);
  interf = create_interface(sdis, solid, dummy);
  scn = create_scene(sdis, super_shape, interf);

  check_probe(scn, SDIS_DIFFUSION_DELTA_SPHERE, pos, time, 0/*green*/);
  check_probe(scn, SDIS_DIFFUSION_WOS, pos, time, 0/*green*/);
  check_probe(scn, SDIS_DIFFUSION_WOS, pos, time, 1/*green*/);

  /* Write 10 heat paths sampled by the delta sphere algorithm */
  CHK(fp = fopen("paths_delta_sphere_3d.vtk", "w"));
  dump_paths(fp, scn, SDIS_DIFFUSION_DELTA_SPHERE, pos, time, 10);
  CHK(fclose(fp) == 0);

  /* Write 10 heat paths sampled by the WoS algorithm */
  CHK(fp = fopen("paths_wos_3d.vtk", "w"));
  dump_paths(fp, scn, SDIS_DIFFUSION_WOS, pos, time, 10);
  CHK(fclose(fp) == 0);

  OK(s3dut_mesh_ref_put(super_shape));
  OK(sdis_device_ref_put(sdis));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_scene_ref_put(scn));

  CHK(mem_allocated_size() == 0);
  return 0;
}
