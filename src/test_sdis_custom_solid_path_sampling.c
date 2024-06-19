/* Copyright (C) 2016-2024 |Méso|Star> (contact@meso-star.com)
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
#include "test_sdis_mesh.h"
#include "test_sdis_utils.h"

#include <star/s3dut.h>

/*
 * The system is a trilinear profile of the temperature at steady state, i.e. at
 * each point of the system we can calculate the temperature analytically. Two
 * forms are immersed in this temperature field: a super shape and a sphere
 * included in the super shape. On the Monte Carlo side, the temperature is
 * unknown everywhere  except on the surface of the super shape whose
 * temperature is defined from the aformentionned trilinear profile.
 *
 * We will estimate the temperature at the position of a probe in solids by
 * providing a user-side function to sample the conductive path in the sphere.
 * We should find the temperature of the trilinear profile at the probe position
 * by Monte Carlo, independently of this coupling with an external path sampling
 * routine.
 *
 *
 *                       /\ <-- T(x,y,z)
 *                   ___/  \___
 *      T(z)         \   __   /
 *       |  T(y)      \ /  \ /
 *       |/         T=? *__/ \
 *       o--- T(x)   /_  __  _\
 *                     \/  \/
 */

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static double
trilinear_profile(const double pos[3])
{
  /* Range in X, Y and Z in which the trilinear profile is defined */
  const double lower = -4;
  const double upper = +4;

  /* Upper temperature limit in X, Y and Z [K]. Lower temperature limit is
   * implicitly 0 */
  const double a = 333; /* Upper temperature limit in X [K] */
  const double b = 432; /* Upper temperature limit in Y [K] */
  const double c = 579; /* Upper temperature limit in Z [K] */

  double x, y, z;

  /* Check pre-conditions */
  CHK(pos);
  CHK(lower <= pos[0] && pos[0] <= upper);
  CHK(lower <= pos[1] && pos[1] <= upper);
  CHK(lower <= pos[2] && pos[2] <= upper);

  x = (pos[0] - lower) / (upper - lower);
  y = (pos[1] - lower) / (upper - lower);
  z = (pos[2] - lower) / (upper - lower);
  return a*x + b*y + c*z;
}

/*******************************************************************************
 * Mesh, i.e. supershape and sphere
 ******************************************************************************/
static void
mesh_add_super_shape(struct mesh* mesh)
{
  struct s3dut_mesh* sshape = NULL;
  struct s3dut_mesh_data sshape_data;
  struct s3dut_super_formula f0 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_super_formula f1 = S3DUT_SUPER_FORMULA_NULL;
  const double radius = 2;
  const unsigned nslices = 256;

  f0.A = 1.5; f0.B = 1; f0.M = 11.0; f0.N0 = 1; f0.N1 = 1; f0.N2 = 2.0;
  f1.A = 1.0; f1.B = 2; f1.M =  3.6; f1.N0 = 1; f1.N1 = 2; f1.N2 = 0.7;
  OK(s3dut_create_super_shape(NULL, &f0, &f1, radius, nslices, nslices/2, &sshape));
  OK(s3dut_mesh_get_data(sshape, &sshape_data));
  mesh_append(mesh, sshape_data.positions, sshape_data.nvertices,
    sshape_data.indices, sshape_data.nprimitives, NULL);
  OK(s3dut_mesh_ref_put(sshape));
}

static void
mesh_add_sphere(struct mesh* mesh)
{
  struct s3dut_mesh* sphere = NULL;
  struct s3dut_mesh_data sphere_data;
  const double radius = 1;
  const unsigned nslices = 128;

  OK(s3dut_create_sphere(NULL, radius, nslices, nslices/2, &sphere));
  OK(s3dut_mesh_get_data(sphere, &sphere_data));
  mesh_append(mesh, sphere_data.positions, sphere_data.nvertices,
    sphere_data.indices, sphere_data.nprimitives, NULL);
  OK(s3dut_mesh_ref_put(sphere));
}

/*******************************************************************************
 * Custom conductive path
 ******************************************************************************/
static res_T
sample_steady_diffusive_path
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct sdis_path* path,
   struct sdis_data* data)
{
  CHK(scn && rng && path);
  (void)data; /* Avoid the "unused variable" warning */
  /* TODO */
  return RES_OK;
}

/*******************************************************************************
 * The solids, i.e. media of the super shape and the sphere
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
SOLID_PROP(calorific_capacity, 500.0) /* [J/K/Kg] */
SOLID_PROP(thermal_conductivity, 25.0) /* [W/m/K] */
SOLID_PROP(volumic_mass, 7500.0) /* [kg/m^3] */
SOLID_PROP(temperature, SDIS_TEMPERATURE_NONE/*<=> unknown*/) /* [K] */
SOLID_PROP(delta, 1.0/20.0) /* [m] */

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

  OK(sdis_solid_create(sdis, &shader, NULL, &solid));
  return solid;
}

static struct sdis_medium*
create_custom
  (struct sdis_device* sdis,
   double* positions, /* Nodes of the solid */
   size_t* indices) /* Indices of the solid */
{
  /* Stardis variables */
  struct sdis_solid_shader shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_data* data = NULL;

  /* Mesh variables */
  struct mesh* mesh = NULL;
  const size_t sz = sizeof(struct mesh);
  const size_t al = ALIGNOF(struct mesh);

  OK(sdis_data_create(sdis, sz, al, NULL, &data));
  mesh = sdis_data_get(data);
  mesh->positions = positions;
  mesh->indices = indices;

  shader.calorific_capacity = solid_get_calorific_capacity;
  shader.thermal_conductivity = solid_get_thermal_conductivity;
  shader.volumic_mass = solid_get_volumic_mass;
  shader.delta = solid_get_delta;
  shader.temperature = solid_get_temperature;
  /*shader.sample_path = sample_steady_diffusive_path;*/

  OK(sdis_solid_create(sdis, &shader, data, &solid));
  OK(sdis_data_ref_put(data));

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
 * Interface: its temperature is fixed to the trilinear profile
 ******************************************************************************/
static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  (void)data; /* Avoid the "unused variable" warning */
  return trilinear_profile(frag->P);
}

static struct sdis_interface*
create_interface
  (struct sdis_device* sdis,
   struct sdis_medium* front,
   struct sdis_medium* back)
{
  struct sdis_interface* interf = NULL;
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;

  /* Fluid/solid interface: fix the temperature to the temperature profile  */
  if(sdis_medium_get_type(front) != sdis_medium_get_type(back)) {
    shader.front.temperature = interface_get_temperature;
    shader.back.temperature = interface_get_temperature;
  }

  OK(sdis_interface_create(sdis, front, back, &shader, NULL, &interf));
  return interf;
}

/*******************************************************************************
 * Scene, i.e. the system to simulate
 ******************************************************************************/
struct scene_context {
  const struct mesh* mesh;
  size_t sshape_end_id; /* Last triangle index of the super shape */
  struct sdis_interface* sshape;
  struct sdis_interface* sphere;
};
#define SCENE_CONTEXT_NULL__ {NULL, 0, 0, 0}
static const struct scene_context SCENE_CONTEXT_NULL = SCENE_CONTEXT_NULL__;

static void
scene_get_indices(const size_t itri, size_t ids[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(ids && context && itri < mesh_ntriangles(context->mesh));
  /* Flip the indices to ensure that the normal points into the super shape */
  ids[0] = (unsigned)context->mesh->indices[itri*3+0];
  ids[1] = (unsigned)context->mesh->indices[itri*3+2];
  ids[2] = (unsigned)context->mesh->indices[itri*3+1];
}

static void
scene_get_interface(const size_t itri, struct sdis_interface** interf, void* ctx)
{
  struct scene_context* context = ctx;
  CHK(interf && context && itri < mesh_ntriangles(context->mesh));
  if(itri < context->sshape_end_id) {
    *interf = context->sshape;
  } else {
    *interf = context->sphere;
  }
}

static void
scene_get_position(const size_t ivert, double pos[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(pos && context && ivert < mesh_nvertices(context->mesh));
  pos[0] = context->mesh->positions[ivert*3+0];
  pos[1] = context->mesh->positions[ivert*3+1];
  pos[2] = context->mesh->positions[ivert*3+2];
}

static struct sdis_scene*
create_scene(struct sdis_device* sdis, struct scene_context* ctx)

{
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;

  scn_args.get_indices = scene_get_indices;
  scn_args.get_interface = scene_get_interface;
  scn_args.get_position = scene_get_position;
  scn_args.nprimitives = mesh_ntriangles(ctx->mesh);
  scn_args.nvertices = mesh_nvertices(ctx->mesh);
  scn_args.context = ctx;
  OK(sdis_scene_create(sdis, &scn_args, &scn));
  return scn;
}

/*******************************************************************************
 * Validations
 ******************************************************************************/
static void
check_probe(struct sdis_scene* scn, const int is_master_process)
{
  struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  double ref = 0;

  args.position[0] = 0.125;
  args.position[1] = 0.250;
  args.position[2] = 0.375;

  OK(sdis_solve_probe(scn, &args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &T));

  if(!is_master_process) return;

  ref = trilinear_profile(args.position);

  printf("T(%g, %g, %g) = %g ~ %g +/- %g\n",
    SPLIT3(args.position), ref, T.E, T.SE);

  CHK(eq_eps(ref, T.E, T.SE*3));
  OK(sdis_estimator_ref_put(estimator));
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  /* Stardis */
  struct sdis_device* dev = NULL;
  struct sdis_interface* solid_dummy = NULL;
  struct sdis_interface* custom_solid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* custom = NULL;
  struct sdis_medium* dummy = NULL; /* Medium surrounding the solid */
  struct sdis_scene* scn = NULL;

  /* Miscellaneous */
  struct scene_context ctx = SCENE_CONTEXT_NULL;
  struct mesh mesh = MESH_NULL;
  size_t sshape_end_id = 0; /* Last index of the super shape */
  int is_master_process = 1;
  (void)argc, (void)argv;

  create_default_device(&argc, &argv, &is_master_process, &dev);

  /* Mesh */
  mesh_init(&mesh);
  mesh_add_super_shape(&mesh);
  sshape_end_id = mesh_ntriangles(&mesh);
  mesh_add_sphere(&mesh);

  /* Physical properties */
  dummy = create_dummy(dev);
  solid = create_solid(dev);
  custom = create_custom(dev, mesh.positions, mesh.indices + sshape_end_id);
  solid_dummy = create_interface(dev, solid, dummy);
  custom_solid = create_interface(dev, custom, solid);

  /* Scene */
  ctx.mesh = &mesh;
  ctx.sshape_end_id = sshape_end_id;
  ctx.sshape = solid_dummy;
  ctx.sphere = custom_solid;
  scn = create_scene(dev, &ctx);

  check_probe(scn, is_master_process);

  mesh_release(&mesh);

  OK(sdis_interface_ref_put(solid_dummy));
  OK(sdis_interface_ref_put(custom_solid));
  OK(sdis_medium_ref_put(custom));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_scene_ref_put(scn));

  free_default_device(dev);

  CHK(mem_allocated_size() == 0);
  return 0;
}
