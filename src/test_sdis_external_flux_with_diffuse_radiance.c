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

#include "test_sdis_utils.h"
#include "sdis.h"

#include <rsys/rsys.h>
#include <star/s3dut.h>

/*
 * The system is a sphere with an emissivity of 1. It is illuminated by an
 * external spherical source that is sufficiently far away from the sphere
 * radius to assume a constant flux density received per m^2 of surface
 * perpendicular to the direction towards the source center.
 *
 * In such situation, the value of the surface temperature is equal to :
 *
 *    Ts = [q/(4*sigma)+Tamb^4]^0.25
 *    q = P/(4PI*d^2)
 *
 * with Tamb the temperature of the environment, sigma the Boltzmann constant,
 * P the source power and d the distance of the source from the center of
 * the sphere.
 *
 * If one adds a diffuse radiance Ldiff, the surface temperature becomes :
 *
 *    Ts = [(q/4+Ldiff*PI)/sigma+Tamb^4]^0.25
 *
 * The test checks that we retrieved these analatycal results by Monte Carlo
 *
 *
 *              R = 0.01 m                    External source
 *               __             d = 10 m            ###
 *     T = ? -> / .\ ..............................##### r = 0.3 m
 *              \__/                               #####
 *              E=1                                 ###
 *                                               P = 3^7 W
 */

/* The source */
#define SOURCE_POWER 1.0e5 /* [W] */
#define SOURCE_RADIUS 0.3 /* [m/fp_to_meter] */
#define SOURCE_DISTANCE 10 /* [m/fp_to_meter] */

/* Miscellaneous */
#define SPHERE_RADIUS 0.01 /* [m/fp_to_meter] */
#define SPHERE_T_REF 320 /* [K] */
#define T_RAD 300.0 /* [K] */
#define T_REF 300.0 /* [K] */
#define T_RAD4 (T_RAD*T_RAD*T_RAD*T_RAD)

#define NREALISATIONS 10000

/*******************************************************************************
 * Geometry
 ******************************************************************************/
static struct s3dut_mesh*
create_sphere(void)
{
  struct s3dut_mesh* mesh = NULL;
  OK(s3dut_create_sphere(NULL, SPHERE_RADIUS, 128, 64, &mesh));
  return mesh;
}

/*******************************************************************************
 * Media
 ******************************************************************************/
#define MEDIUM_PROP(Type, Prop, Val)                                           \
  static double                                                                \
  Type##_get_##Prop                                                            \
    (const struct sdis_rwalk_vertex* vtx,                                      \
     struct sdis_data* data)                                                   \
  {                                                                            \
    (void)vtx, (void)data; /* Avoid the "unused variable" warning */           \
    return Val;                                                                \
  }
MEDIUM_PROP(medium, volumic_mass, 1) /* [kj/m^3] */
MEDIUM_PROP(medium, calorific_capacity, 1) /* [J/K/Kg] */
MEDIUM_PROP(medium, temperature, SDIS_TEMPERATURE_NONE) /* [K] */
MEDIUM_PROP(solid, thermal_conductivity, 1) /* [W/m/K] */
MEDIUM_PROP(solid, delta, (2*SPHERE_RADIUS)/10.0) /* [m] */
#undef MEDIUM_PROP

static struct sdis_medium*
create_solid(struct sdis_device* sdis)
{
  struct sdis_solid_shader shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_medium* solid = NULL;

  shader.calorific_capacity = medium_get_calorific_capacity;
  shader.thermal_conductivity = solid_get_thermal_conductivity;
  shader.volumic_mass = medium_get_volumic_mass;
  shader.delta = solid_get_delta;
  shader.temperature = medium_get_temperature;
  OK(sdis_solid_create(sdis, &shader, NULL, &solid));
  return solid;
}

static struct sdis_medium*
create_fluid(struct sdis_device* sdis)
{
  struct sdis_fluid_shader shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_medium* fluid = NULL;

  shader.calorific_capacity = medium_get_calorific_capacity;
  shader.volumic_mass = medium_get_volumic_mass;
  shader.temperature = medium_get_temperature;
  OK(sdis_fluid_create(sdis, &shader, NULL, &fluid));
  return fluid;
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  (void)frag, (void)source_id, (void)data;
  return 1;
}

static double
interface_get_reference_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  (void)frag, (void)data;
  return  SPHERE_T_REF; /* [K] */
}

static struct sdis_interface*
create_interface
  (struct sdis_device* sdis,
   struct sdis_medium* front,
   struct sdis_medium* back)
{
  struct sdis_interface* interf = NULL;
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;

  shader.front.emissivity = interface_get_emissivity;
  shader.front.reference_temperature = interface_get_reference_temperature;
  OK(sdis_interface_create(sdis, front, back, &shader, NULL, &interf));
  return interf;
}

/*******************************************************************************
 * Radiative environment
 ******************************************************************************/
#define RADENV_PROP(Prop, Val)                                                 \
  static double                                                                \
  radenv_get_##Prop                                                            \
    (const struct sdis_radiative_ray* ray,                                     \
     struct sdis_data* data)                                                   \
  {                                                                            \
    (void)ray, (void)data;                                                     \
    return (Val); /* [K] */                                                    \
  }
RADENV_PROP(temperature, T_RAD)
RADENV_PROP(reference_temperature, T_REF)
#undef RADENV_PROP

static struct sdis_radiative_env*
create_radenv(struct sdis_device* sdis)
{
  struct sdis_radiative_env_shader shader = SDIS_RADIATIVE_ENV_SHADER_NULL;
  struct sdis_radiative_env* radenv = NULL;

  shader.temperature = radenv_get_temperature;
  shader.reference_temperature = radenv_get_reference_temperature;
  OK(sdis_radiative_env_create(sdis, &shader, NULL, &radenv));
  return radenv;
}

/*******************************************************************************
 * External source
 ******************************************************************************/
static void
source_get_position
  (const double time,
   double pos[3],
   struct sdis_data* data)
{
  (void)time, (void)data; /* Avoid the "unusued variable" warning */
  pos[0] = SOURCE_DISTANCE;
  pos[1] = 0;
  pos[2] = 0;
}

static double
source_get_power(const double time, struct sdis_data* data)
{
  (void)time, (void)data; /* Avoid the "unused variable" warning */
  return SOURCE_POWER; /* [W] */
}

static double
source_get_diffuse_radiance
  (const double time,
   const double dir[3],
   struct sdis_data* data)
{
  const double* Ldiff = sdis_data_cget(data);;
  (void)time, (void)dir; /* Avoid the "unused variable" warning */
  return *Ldiff; /* [W/m^2/sr] */
}

static struct sdis_source*
create_source(struct sdis_device* sdis, double** diffuse_radiance)
{
  struct sdis_spherical_source_shader shader = SDIS_SPHERICAL_SOURCE_SHADER_NULL;
  struct sdis_data* data = NULL;
  struct sdis_source* src = NULL;

  OK(sdis_data_create(sdis, sizeof(double), ALIGNOF(double), NULL, &data));
  *diffuse_radiance = sdis_data_get(data);
  **diffuse_radiance = 0;

  shader.position = source_get_position;
  shader.power = source_get_power;
  shader.diffuse_radiance = source_get_diffuse_radiance;
  shader.radius = SOURCE_RADIUS;
  OK(sdis_spherical_source_create(sdis, &shader, data, &src));
  OK(sdis_data_ref_put(data));
  return src;
}

/*******************************************************************************
 * The scene
 ******************************************************************************/
struct scene_context {
  const struct s3dut_mesh_data* mesh;
  struct sdis_interface* interf;
};
static const struct scene_context SCENE_CONTEXT_NULL = {NULL, NULL};

static void
scene_get_indices(const size_t itri, size_t ids[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(ids && context && itri < context->mesh->nprimitives);
  ids[0] = (unsigned)context->mesh->indices[itri*3+0];
  ids[1] = (unsigned)context->mesh->indices[itri*3+1];
  ids[2] = (unsigned)context->mesh->indices[itri*3+2];
}

static void
scene_get_interface(const size_t itri, struct sdis_interface** interf, void* ctx)
{
  struct scene_context* context = ctx;
  CHK(interf && context && itri < context->mesh->nprimitives);
  *interf = context->interf;
}

static void
scene_get_position(const size_t ivert, double pos[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(pos && context && ivert < context->mesh->nvertices);
  pos[0] = context->mesh->positions[ivert*3+0];
  pos[1] = context->mesh->positions[ivert*3+1];
  pos[2] = context->mesh->positions[ivert*3+2];
}

static struct sdis_scene*
create_scene
  (struct sdis_device* sdis,
   const struct s3dut_mesh_data* mesh,
   struct sdis_interface* interf,
   struct sdis_source* source,
   struct sdis_radiative_env* radenv)
{
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct scene_context context = SCENE_CONTEXT_NULL;

  context.mesh = mesh;
  context.interf = interf;

  scn_args.get_indices = scene_get_indices;
  scn_args.get_interface = scene_get_interface;
  scn_args.get_position = scene_get_position;
  scn_args.nprimitives = mesh->nprimitives;
  scn_args.nvertices = mesh->nvertices;
  scn_args.t_range[0] = MMIN(T_REF, SPHERE_T_REF);
  scn_args.t_range[1] = MMAX(T_REF, SPHERE_T_REF);
  scn_args.source = source;
  scn_args.radenv = radenv;
  scn_args.context = &context;
  OK(sdis_scene_create(sdis, &scn_args, &scn));
  return scn;
}

/*******************************************************************************
 * Validations
 ******************************************************************************/
static double
analytic_temperature(const double Ldiff)
{
  const double q = SOURCE_POWER/(4*PI*SOURCE_DISTANCE*SOURCE_DISTANCE);
  const double Ts = pow((q/4 + Ldiff*PI)/BOLTZMANN_CONSTANT + T_RAD4, 0.25);
  return Ts;
}

static void
check
  (struct sdis_scene* scn,
   const struct s3dut_mesh_data* mesh,
   const double Ldiff)
{
  struct sdis_solve_boundary_args args = SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  enum sdis_side* sides = NULL;
  double ref = 0;
  size_t i = 0;

  sides = mem_alloc(mesh->nprimitives*sizeof(*sides));
  FOR_EACH(i, 0, mesh->nprimitives) sides[i] = SDIS_FRONT;

  args.primitives = mesh->indices;
  args.sides = sides;
  args.nprimitives = mesh->nprimitives;
  args.nrealisations = NREALISATIONS;
  OK(sdis_solve_boundary(scn, &args, &estimator));

  OK(sdis_estimator_get_temperature(estimator, &T));
  ref = analytic_temperature(Ldiff);
  printf("Ts = %g ~ %g +/- %g\n", ref, T.E, T.SE);
  OK(sdis_estimator_ref_put(estimator));

  CHK(eq_eps(T.E, ref, T.SE * 3));

  mem_rm(sides);
}

static void
solve_green(struct sdis_green_function* green, const double Ldiff)
{
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  double ref = 0;

  OK(sdis_green_function_solve(green, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &T));

  ref = analytic_temperature(Ldiff);
  printf("Ts = %g ~ %g +/- %g\n", ref, T.E, T.SE);
  CHK(eq_eps(T.E, ref, T.SE * 3));

  OK(sdis_estimator_ref_put(estimator));
}

static void
check_green
  (struct sdis_scene* scn,
   const struct s3dut_mesh_data* mesh,
   double* Ldiff)
{
  struct sdis_solve_boundary_args args = SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT;
  struct sdis_green_function* green = NULL;
  enum sdis_side* sides = NULL;
  size_t i = 0;

  CHK(Ldiff);

  sides = mem_alloc(mesh->nprimitives*sizeof(*sides));
  FOR_EACH(i, 0, mesh->nprimitives) sides[i] = SDIS_FRONT;

  *Ldiff = 0;

  args.primitives = mesh->indices;
  args.sides = sides;
  args.nprimitives = mesh->nprimitives;
  args.nrealisations = NREALISATIONS;
  OK(sdis_solve_boundary_green_function(scn, &args, &green));

  solve_green(green, (*Ldiff = 50));
  solve_green(green, (*Ldiff = 45));
  solve_green(green, (*Ldiff = 40));

  OK(sdis_green_function_ref_put(green));

  mem_rm(sides);
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  /* Stardis */
  struct sdis_device* dev = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_scene* scene = NULL;
  struct sdis_source* source = NULL;

  /* Miscellaneous */
  struct s3dut_mesh_data mesh;
  struct s3dut_mesh* sphere = NULL;
  double* Ldiff = NULL;

  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  sphere = create_sphere();
  OK(s3dut_mesh_get_data(sphere, &mesh));

  fluid = create_fluid(dev);
  solid = create_solid(dev);
  interf = create_interface(dev, fluid, solid);
  radenv = create_radenv(dev);
  source = create_source(dev, &Ldiff);

  scene = create_scene(dev, &mesh, interf, source, radenv);

  check(scene, &mesh, (*Ldiff = 50));
  check_green(scene, &mesh, Ldiff);

  OK(s3dut_mesh_ref_put(sphere));
  OK(sdis_device_ref_put(dev));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_radiative_env_ref_put(radenv));
  OK(sdis_scene_ref_put(scene));
  OK(sdis_source_ref_put(source));
  CHK(mem_allocated_size() == 0);
  return 0;
}
