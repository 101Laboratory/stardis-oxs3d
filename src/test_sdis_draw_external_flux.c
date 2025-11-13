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
#include "test_sdis_mesh.h"
#include "test_sdis_utils.h"

#include <star/s3dut.h>

#define IMG_WIDTH 320
#define IMG_HEIGHT 240
#define IMG_SPP 64
#define SOURCE_POWER 30e6 /* W */
#define EMISSIVITY 0.5
#define T_RAD 300 /* [K] */
#define T_REF 300 /* [K] */

/*
 * The system consists of a floor (i.e. a parallelepiped), a super-form floating
 * above it and a spherical external source. The test attempts to render the
 * scene and thus verify the entire sampling of conductive-radiative paths
 * coupled to an external flux from the external source.
 */

/*******************************************************************************
 * Geometries
 ******************************************************************************/
static void
mesh_add_super_shape(struct mesh* mesh)
{
  struct s3dut_mesh* sshape = NULL;
  struct s3dut_mesh_data sshape_data;
  struct s3dut_super_formula f0 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_super_formula f1 = S3DUT_SUPER_FORMULA_NULL;
  const double radius = 1;
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
mesh_add_ground(struct mesh* mesh)
{
  #define DEPTH 2.0
  const double translate[3] = {0, 0, -DEPTH-1};

  struct s3dut_mesh* ground = NULL;
  struct s3dut_mesh_data ground_data;

  OK(s3dut_create_cuboid(NULL, 20, 20, DEPTH, &ground));
  OK(s3dut_mesh_get_data(ground, &ground_data));
  mesh_append(mesh, ground_data.positions, ground_data.nvertices,
    ground_data.indices, ground_data.nprimitives, translate);
  OK(s3dut_mesh_ref_put(ground));
  #undef DEPTH
}

/*******************************************************************************
 * Media & interface
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
MEDIUM_PROP(solid, calorific_capacity, 500.0) /* [J/K/Kg] */
MEDIUM_PROP(solid, thermal_conductivity, 25.0) /* [W/m/K] */
MEDIUM_PROP(solid, volumic_mass, 7500.0) /* [kg/m^3] */
MEDIUM_PROP(solid, temperature, 310) /* [K] */
MEDIUM_PROP(solid, delta, 1.0/20.0) /* [m] */
MEDIUM_PROP(fluid, calorific_capacity, 2.0) /* [J/K/Kg] */
MEDIUM_PROP(fluid, volumic_mass, 25.0) /* |kg/m^3] */
MEDIUM_PROP(fluid, temperature, SDIS_TEMPERATURE_NONE) /* [K] */
#undef MEDIUM_PROP

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
create_fluid(struct sdis_device* sdis)
{
  struct sdis_fluid_shader shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_medium* fluid = NULL;

  shader.calorific_capacity = fluid_get_calorific_capacity;
  shader.volumic_mass = fluid_get_volumic_mass;
  shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(sdis, &shader, NULL, &fluid));
  return fluid;
}

static double
interface_get_reference_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  (void)frag, (void)data; /* Avoid the "unused variable" warning */
  return T_REF;
}

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  (void)frag, (void)data;/* Avoid the "unused variable" warning */
  return SDIS_TEMPERATURE_NONE;
}

static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  /* Avoid the "unused variable" warning */
  (void)frag, (void)source_id, (void)data;
  return EMISSIVITY;
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
  shader.front.reference_temperature = interface_get_reference_temperature;
  shader.front.emissivity = interface_get_emissivity;
  shader.back.temperature = interface_get_temperature;
  OK(sdis_interface_create(sdis, front, back, &shader, NULL, &interf));
  return interf;
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
  pos[0] = 5.0;
  pos[1] = 5.0;
  pos[2] = 5.0;
}

static double
source_get_power(const double time, struct sdis_data* data)
{
  (void)time, (void)data; /* Avoid the "unusued variable" warning */
  return SOURCE_POWER; /* [W] */
}

static double
source_get_diffuse_radiance
  (const double time,
   const double dir[3],
   struct sdis_data* data)
{
  (void)time, (void)data; /* Avoid the "unusued variable" warning */
  CHK(d3_is_normalized(dir));
  return 50;
}

static struct sdis_source*
create_source(struct sdis_device* sdis)
{
  struct sdis_spherical_source_shader shader = SDIS_SPHERICAL_SOURCE_SHADER_NULL;
  struct sdis_source* source = NULL;

  shader.position = source_get_position;
  shader.power = source_get_power;
  shader.diffuse_radiance = source_get_diffuse_radiance;
  shader.radius = 3e-1; /* [m] */
  OK(sdis_spherical_source_create(sdis, &shader, NULL, &source));
  return source;
}

/*******************************************************************************
 * Radiative environment
 ******************************************************************************/
static double
radenv_get_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray, (void)data;
  return T_RAD;
}

static double
radenv_get_reference_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray, (void)data;
  return T_REF;
}

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
 * Scene, i.e. the system to simulate
 ******************************************************************************/
struct scene_context {
  const struct mesh* mesh;
  struct sdis_interface* interf;
};
static const struct scene_context SCENE_CONTEXT_NULL = {NULL, NULL};

static void
scene_get_indices(const size_t itri, size_t ids[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(ids && context && itri < mesh_ntriangles(context->mesh));
  ids[0] = (unsigned)context->mesh->indices[itri*3+0];
  ids[1] = (unsigned)context->mesh->indices[itri*3+1];
  ids[2] = (unsigned)context->mesh->indices[itri*3+2];
}

static void
scene_get_interface(const size_t itri, struct sdis_interface** interf, void* ctx)
{
  struct scene_context* context = ctx;
  CHK(interf && context && itri < mesh_ntriangles(context->mesh));
  *interf = context->interf;
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
create_scene
  (struct sdis_device* sdis,
   const struct mesh* mesh,
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
  scn_args.nprimitives = mesh_ntriangles(mesh);
  scn_args.nvertices = mesh_nvertices(mesh);
  scn_args.t_range[0] = MMIN(T_RAD, T_REF);
  scn_args.t_range[1] = MMAX(T_RAD, T_REF);
  scn_args.source = source;
  scn_args.radenv = radenv;
  scn_args.context = &context;
  OK(sdis_scene_create(sdis, &scn_args, &scn));
  return scn;
}

/*******************************************************************************
 * Rendering point of view
 ******************************************************************************/
static struct sdis_camera*
create_camera(struct sdis_device* sdis)
{
  const double pos[3] = {-3.81, 11.23, 5.29};
  const double tgt[3] = {-0.46, 0, -0.32};
  const double up[3] = {0, 0, 1};
  struct sdis_camera* cam = NULL;

  OK(sdis_camera_create(sdis, &cam));
  OK(sdis_camera_set_proj_ratio(cam, (double)IMG_WIDTH/(double)IMG_HEIGHT));
  OK(sdis_camera_set_fov(cam, MDEG2RAD(30)));
  OK(sdis_camera_look_at(cam, pos, tgt, up));
  return cam;
}

/*******************************************************************************
 * Draw the submitted scene
 ******************************************************************************/
/* Write an image in htrdr-image(5) format */
static void
write_image(FILE* stream, struct sdis_estimator_buffer* image)
{
  size_t x, y;

  /* Header */
  fprintf(stream, "%d %d\n", IMG_WIDTH, IMG_HEIGHT);

  /* Pixels ordered by row */
  FOR_EACH(y, 0, IMG_HEIGHT) {
    FOR_EACH(x, 0, IMG_WIDTH) {
      const struct sdis_estimator* pixel = NULL;
      struct sdis_mc temperature = SDIS_MC_NULL;

      OK(sdis_estimator_buffer_at(image, x, y, &pixel));
      OK(sdis_estimator_get_temperature(pixel, &temperature));
      fprintf(stream, "%g %g 0 0 0 0 0 0\n", temperature.E, temperature.SE);
    }
  }
}

static void
draw(struct sdis_scene* scn, struct sdis_camera* cam)
{
  struct sdis_solve_camera_args args = SDIS_SOLVE_CAMERA_ARGS_DEFAULT;
  struct sdis_estimator_buffer* image = NULL;

  args.cam = cam;
  args.image_definition[0] = IMG_WIDTH;
  args.image_definition[1] = IMG_HEIGHT;
  args.spp = IMG_SPP;

  OK(sdis_solve_camera(scn, &args, &image));
  write_image(stdout, image);
  OK(sdis_estimator_buffer_ref_put(image));
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  /* Stardis */
  struct sdis_camera* cam = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_source* source = NULL;
  struct sdis_radiative_env* radenv = NULL;

  /* Miscellaneous */
  struct mesh mesh = MESH_NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  mesh_init(&mesh);
  mesh_add_super_shape(&mesh);
  mesh_add_ground(&mesh);

  solid = create_solid(dev);
  fluid = create_fluid(dev);
  interf = create_interface(dev, fluid, solid);
  source = create_source(dev);
  radenv = create_radenv(dev);
  scn = create_scene(dev, &mesh, interf, source, radenv);
  cam = create_camera(dev);

  draw(scn, cam);

  /* For debug of scene geometry */
  /*dump_mesh(stdout,
    mesh.positions, mesh_nvertices(&mesh),
    mesh.indices, mesh_ntriangles(&mesh));*/

  mesh_release(&mesh);
  OK(sdis_camera_ref_put(cam));
  OK(sdis_device_ref_put(dev));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_scene_ref_put(scn));
  OK(sdis_source_ref_put(source));
  OK(sdis_radiative_env_ref_put(radenv));
  CHK(mem_allocated_size() == 0);
  return 0;
}
