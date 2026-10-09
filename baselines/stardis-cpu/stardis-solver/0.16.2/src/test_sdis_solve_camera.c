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

#include <star/ssp.h>
#include <star/s3dut.h>

#include <rsys/algorithm.h>
#include <rsys/image.h>
#include <rsys/double3.h>
#include <rsys/double33.h>
#include <rsys_math.h>
#include <rsys/stretchy_array.h>

#include <string.h>

#define IMG_WIDTH 157
#define IMG_HEIGHT 53
#define SPP 32 /* #Samples per pixel, i.e. #realisations per pixel */

/*
 * The scene consists of a solid cube whose temperature is unknown. The
 * emissivity of the cube is 1 and the convection coefficient with the
 * surrounding fluid at 300 K is 0.1. At the center of the cube is a spherical
 * cavity of fluid with a temperature of 350 K. The convection coefficient
 * between the solid and the cavity is 1, and the emissivity of this interface
 * is zero. The ambient radiative temperature of the system is 300 K.
 *
 * Finally, a parallelepiped below the cube symbolizes the ground. The
 * temperature of its Robin condition is 280 K. This geometry verifies that a
 * camera can draw a scene in an enclosure containing several media, such as
 * those used to define several boundary conditions.
 *
 * In this test, we calculate the radiative temperature that reaches a camera
 * looking at the cube and produce an image of the result written to the
 * standard output in htrdr-image(5) format.
 *
 *
 *                +----------------+
 *               /'     #  #      /|
 *              +----*--------*--+ |   __\  300 K
 *              | ' #          # | |  /  /
 *              | ' #   350K   # | |  \__/
 *              | '  #        #  | |
 *              | +.....#..#.....|.+
 *              |/               |/
 *              +----------------+     280K
 *    +---------------------------------__\------+
 *   /                                 /  /     /|
 *  /                                  \__/    / +
 * +------------------------------------------+ /
 * |                                          |/
 * +------------------------------------------+
 */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct map_interf {
  size_t key;
  struct sdis_interface* interf;
};

static int
cmp_map_inter(const void* key, const void* elmt)
{
  const size_t* iprim = key;
  const struct map_interf* interf = elmt;
  if(*iprim < interf->key) return -1;
  else if(*iprim > interf->key) return 1;
  else return 0;
}

struct geometry {
  double* positions;
  size_t* indices;
  struct map_interf* interfaces;
};
static const struct geometry GEOMETRY_NULL = {NULL, NULL, NULL};

static void
geometry_add_shape
  (struct geometry* geom,
   const double* positions,
   const size_t nverts,
   const size_t* indices,
   const size_t nprims,
   const double transform[12], /* May be NULL <=> no transformation */
   struct sdis_interface* interf)
{
  struct map_interf* geom_interf = NULL;
  size_t nverts_prev = 0;
  size_t i;

  CHK(geom != NULL);
  CHK(positions != NULL);
  CHK(indices != NULL);
  CHK(nverts != 0);
  CHK(nprims != 0);
  CHK(interf != NULL);

  /* Save the previous number of vertices/primitives of the geometry */
  nverts_prev = sa_size(geom->positions) / 3;

  /* Add the vertices */
  FOR_EACH(i, 0, nverts) {
    double* pos = sa_add(geom->positions, 3);
    d3_set(pos, positions + i*3);
    if(transform) {
      d33_muld3(pos, transform, pos);
      d3_add(pos, transform+9, pos);
    }
  }

  /* Add the indices */
  FOR_EACH(i, 0, nprims) {
    sa_push(geom->indices, indices[i*3+0] + nverts_prev);
    sa_push(geom->indices, indices[i*3+1] + nverts_prev);
    sa_push(geom->indices, indices[i*3+2] + nverts_prev);
  }

  geom_interf = sa_add(geom->interfaces, 1);
  geom_interf->key = sa_size(geom->indices) / 3 - 1;
  geom_interf->interf = interf;
}

static void
geometry_release(struct geometry* geom)
{
  CHK(geom != NULL);
  sa_release(geom->positions);
  sa_release(geom->indices);
  sa_release(geom->interfaces);
  *geom = GEOMETRY_NULL;
}

static void
geometry_get_indices(const size_t itri, size_t ids[3], void* ctx)
{
  struct geometry* geom = ctx;

  CHK(ids != NULL);
  CHK(geom != NULL);
  CHK(itri < sa_size(geom->indices)/3/*#indices per triangle*/);

  /* Fetch the indices */
  ids[0] = geom->indices[itri*3+0];
  ids[1] = geom->indices[itri*3+1];
  ids[2] = geom->indices[itri*3+2];
}

static void
geometry_get_position(const size_t ivert, double pos[3], void* ctx)
{
  struct geometry* geom = ctx;

  CHK(pos != NULL);
  CHK(geom != NULL);
  CHK(ivert < sa_size(geom->positions)/3/*#coords per triangle*/);

  /* Fetch the vertices */
  pos[0] = geom->positions[ivert*3+0];
  pos[1] = geom->positions[ivert*3+1];
  pos[2] = geom->positions[ivert*3+2];
}

static void
geometry_get_interface
  (const size_t itri,
   struct sdis_interface** bound,
   void* ctx)
{
  struct geometry* geom = ctx;
  struct map_interf* interf = NULL;

  CHK(bound != NULL);
  CHK(geom != NULL);
  CHK(itri < sa_size(geom->indices)/3/*#indices per triangle*/);

  /* Find the interface of the triangle */
  interf = search_lower_bound(&itri, geom->interfaces,
    sa_size(geom->interfaces), sizeof(struct map_interf), cmp_map_inter);

  CHK(interf != NULL);
  CHK(interf->key >= itri);
  *bound = interf->interf;
}

static void
add_cube(struct geometry* geom, struct sdis_interface* interf)
{
  struct s3dut_mesh_data msh_data;
  struct s3dut_mesh* msh = NULL;
  CHK(geom);

  OK(s3dut_create_cuboid(NULL, 2, 2, 2, &msh));
  OK(s3dut_mesh_get_data(msh, &msh_data));
  geometry_add_shape(geom, msh_data.positions, msh_data.nvertices,
    msh_data.indices, msh_data.nprimitives, NULL, interf);
  OK(s3dut_mesh_ref_put(msh));
}

static void
add_sphere(struct geometry* geom, struct sdis_interface* interf)
{
  struct s3dut_mesh_data msh_data;
  struct s3dut_mesh* msh = NULL;
  CHK(geom);

  OK(s3dut_create_sphere(NULL, 0.5, 32, 16, &msh));
  OK(s3dut_mesh_get_data(msh, &msh_data));
  geometry_add_shape(geom, msh_data.positions, msh_data.nvertices,
    msh_data.indices, msh_data.nprimitives, NULL, interf);
  OK(s3dut_mesh_ref_put(msh));
}

static void
add_ground(struct geometry* geom, struct sdis_interface* interf)
{
  struct s3dut_mesh_data msh_data;
  struct s3dut_mesh* msh = NULL;
  const double transform[12] = {1,0,0, 0,1,0, 0,0,1, 0,0,-4};
  CHK(geom);

  OK(s3dut_create_cuboid(NULL, 10, 10, 2, &msh));
  OK(s3dut_mesh_get_data(msh, &msh_data));
  geometry_add_shape(geom, msh_data.positions, msh_data.nvertices,
    msh_data.indices, msh_data.nprimitives, transform, interf);
  OK(s3dut_mesh_ref_put(msh));
}

/*******************************************************************************
 * Media
 ******************************************************************************/
struct fluid {
  double cp;
  double rho;
  double temperature;
};
static const struct fluid FLUID_NULL = {0, 0, SDIS_TEMPERATURE_NONE};

struct solid {
  double cp;
  double lambda;
  double rho;
  double delta;
  double temperature;
};
static const struct solid SOLID_NULL = {0, 0, 0, 0, SDIS_TEMPERATURE_NONE};

#define MEDIUM_GETTER(Type, Prop)                                              \
  static double                                                                \
  Type##_get_##Prop                                                            \
    (const struct sdis_rwalk_vertex* vtx,                                      \
     struct sdis_data* data)                                                   \
  {                                                                            \
    CHK(data && vtx);                                                          \
    return ((const struct Type*)sdis_data_cget(data))->Prop;                   \
  }
/* Fluid getters */
MEDIUM_GETTER(fluid, cp)
MEDIUM_GETTER(fluid, rho)
MEDIUM_GETTER(fluid, temperature)
/* Solid getters */
MEDIUM_GETTER(solid, cp)
MEDIUM_GETTER(solid, lambda)
MEDIUM_GETTER(solid, rho)
MEDIUM_GETTER(solid, delta)
MEDIUM_GETTER(solid, temperature)
#undef MEDIUM_GETTER

static struct sdis_medium*
create_fluid
  (struct sdis_device* dev,
   const struct fluid* param)
{
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_data* data = NULL;
  struct sdis_medium* fluid = NULL;

  CHK(param != NULL);

  /* Copy the fluid parameters into the Stardis memory space */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  memcpy(sdis_data_get(data), param, sizeof(struct fluid));

  /* Setup the fluid shader */
  fluid_shader.calorific_capacity = fluid_get_cp;
  fluid_shader.volumic_mass = fluid_get_rho;
  fluid_shader.temperature = fluid_get_temperature;

  /* Create the fluid medium */
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid));
  OK(sdis_data_ref_put(data));

  return fluid;
}

static struct sdis_medium*
create_solid
  (struct sdis_device* dev,
   const struct solid* param)
{
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_data* data = NULL;
  struct sdis_medium* solid = NULL;

  CHK(param != NULL);

  /* Copy the solid parameters into the Stardis memory space */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  memcpy(sdis_data_get(data), param, sizeof(struct solid));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_cp;
  solid_shader.thermal_conductivity = solid_get_lambda;
  solid_shader.volumic_mass = solid_get_rho;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid medium */
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  return solid;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double hc;
  double epsilon;
  double specular_fraction;
  double temperature;
  double reference_temperature;
};
static const struct interf INTERF_NULL = {
  0, 0, 0, SDIS_TEMPERATURE_NONE, SDIS_TEMPERATURE_NONE
};

#define INTERFACE_GETTER(Prop)                                                 \
  static double                                                                \
  interface_get_##Prop                                                         \
    (const struct sdis_interface_fragment* frag,                               \
     struct sdis_data* data)                                                   \
  {                                                                            \
    CHK(data && frag);                                                         \
    return ((const struct interf*)sdis_data_cget(data))->Prop;                 \
  }
INTERFACE_GETTER(hc)
INTERFACE_GETTER(temperature)
INTERFACE_GETTER(reference_temperature)
#undef INTERFACE_GETTER

#define INTERFACE_GETTER(Prop)                                                 \
  static double                                                                \
  interface_get_##Prop                                                         \
    (const struct sdis_interface_fragment* frag,                               \
     const unsigned source_id,                                                 \
     struct sdis_data* data)                                                   \
  {                                                                            \
    CHK(data && frag);                                                         \
    (void)source_id;                                                           \
    return ((const struct interf*)sdis_data_cget(data))->Prop;                 \
  }
INTERFACE_GETTER(epsilon)
INTERFACE_GETTER(specular_fraction)
#undef INTERFACE_GETTER

static struct sdis_interface*
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* mdm_front,
   struct sdis_medium* mdm_back,
   const struct interf* param)
{
  struct sdis_interface_shader interface_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_data* data = NULL;
  struct sdis_interface* interf = NULL;

  CHK(mdm_front != NULL);
  CHK(mdm_back != NULL);

  /* Copy the interface parameters into the Stardis memory space */
  OK(sdis_data_create
   (dev, sizeof(struct interf), ALIGNOF(struct interf), NULL, &data));
  memcpy(sdis_data_get(data), param, sizeof(struct interf));

  /* Setup the interface shader */
  interface_shader.convection_coef = interface_get_hc;
  interface_shader.front.temperature = interface_get_temperature;
  interface_shader.back.temperature = interface_get_temperature;
  if(sdis_medium_get_type(mdm_front) == SDIS_FLUID) {
    interface_shader.front.emissivity = interface_get_epsilon;
    interface_shader.front.specular_fraction = interface_get_specular_fraction;
    interface_shader.front.reference_temperature =
      interface_get_reference_temperature;
  }
  if(sdis_medium_get_type(mdm_back) == SDIS_FLUID) {
    interface_shader.back.emissivity = interface_get_epsilon;
    interface_shader.back.specular_fraction = interface_get_specular_fraction;
    interface_shader.back.reference_temperature =
      interface_get_reference_temperature;
  }
  /* Create the interface */
  OK(sdis_interface_create
    (dev, mdm_front, mdm_back, &interface_shader, data, &interf));
  OK(sdis_data_ref_put(data));

  return interf;
}

/*******************************************************************************
 * Radiative environment
 ******************************************************************************/
struct radenv {
  double temperature; /* [K] */
  double reference; /* [K] */
};

static double
radenv_get_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray;
  return ((const struct radenv*)sdis_data_cget(data))->temperature;
}

static double
radenv_get_reference_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray;
  return ((const struct radenv*)sdis_data_cget(data))->reference;
}

static struct sdis_radiative_env*
create_radenv
  (struct sdis_device* dev,
   const double temperature,
   const double reference)
{
  struct sdis_radiative_env_shader shader = SDIS_RADIATIVE_ENV_SHADER_NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_data* data = NULL;
  struct radenv* env = NULL;

  OK(sdis_data_create(dev, sizeof(struct radenv), ALIGNOF(radenv), NULL, &data));
  env = sdis_data_get(data);
  env->temperature = temperature;
  env->reference = reference;

  shader.temperature = radenv_get_temperature;
  shader.reference_temperature = radenv_get_reference_temperature;
  OK(sdis_radiative_env_create(dev, &shader, data, &radenv));
  OK(sdis_data_ref_put(data));
  return radenv;
}

/*******************************************************************************
 * Scene
 ******************************************************************************/
static struct sdis_scene*
create_scene
  (struct sdis_device* dev,
   struct sdis_radiative_env* radenv,
   struct geometry* geom)
{
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_scene* scn = NULL;
  size_t ntris = 0;
  size_t npos = 0;
  CHK(geom);

  ntris = sa_size(geom->indices) / 3; /* #primitives */
  npos = sa_size(geom->positions) / 3; /* #positions */

  scn_args.get_indices = geometry_get_indices;
  scn_args.get_interface = geometry_get_interface;
  scn_args.get_position = geometry_get_position;
  scn_args.nprimitives = ntris;
  scn_args.nvertices = npos;
  scn_args.t_range[0] = 300;
  scn_args.t_range[1] = 350;
  scn_args.radenv = radenv;
  scn_args.context = geom;

  OK(sdis_scene_create(dev, &scn_args, &scn));
  return scn;
}

/*******************************************************************************
 * Rendering point of view
 ******************************************************************************/
static struct sdis_camera*
create_camera(struct sdis_device* dev)
{
  const double pos[3] = {3, 3, 3};
  const double tgt[3] = {0, 0, 0};
  const double up[3] = {0, 0, 1};
  struct sdis_camera* cam = NULL;

  OK(sdis_camera_create(dev, &cam));
  OK(sdis_camera_set_proj_ratio(cam, (double)IMG_WIDTH/(double)IMG_HEIGHT));
  OK(sdis_camera_set_fov(cam, MDEG2RAD(70)));
  OK(sdis_camera_look_at(cam, pos, tgt, up));
  return cam;
}

/*******************************************************************************
 * Draw the scene
 ******************************************************************************/
static void
check_pixel(const struct sdis_estimator* pixel)
{
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  size_t nfails = 0;
  size_t nreals = 0;

  OK(sdis_estimator_get_realisation_count(pixel, &nreals));
  OK(sdis_estimator_get_failure_count(pixel, &nfails));
  OK(sdis_estimator_get_temperature(pixel, &T));
  OK(sdis_estimator_get_realisation_time(pixel, &time));

  CHK(nreals + nfails == SPP);
  CHK(T.E > 0);
  CHK(time.E > 0);
}

static void
check_image(const struct sdis_estimator_buffer* img)
{
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  const struct sdis_estimator* pixel = NULL;
  struct ssp_rng* rng_state = NULL;
  size_t definition[2];
  size_t nreals, nfails;
  size_t x, y;

  BA(sdis_estimator_buffer_get_realisation_count(NULL, &nreals));
  BA(sdis_estimator_buffer_get_realisation_count(img, NULL));
  OK(sdis_estimator_buffer_get_realisation_count(img, &nreals));

  BA(sdis_estimator_buffer_get_failure_count(NULL, &nfails));
  BA(sdis_estimator_buffer_get_failure_count(img, NULL));
  OK(sdis_estimator_buffer_get_failure_count(img, &nfails));

  BA(sdis_estimator_buffer_get_temperature(NULL, &T));
  BA(sdis_estimator_buffer_get_temperature(img, NULL));
  OK(sdis_estimator_buffer_get_temperature(img, &T));

  BA(sdis_estimator_buffer_get_realisation_time(NULL, &time));
  BA(sdis_estimator_buffer_get_realisation_time(img, NULL));
  OK(sdis_estimator_buffer_get_realisation_time(img, &time));

  BA(sdis_estimator_buffer_get_rng_state(NULL, &rng_state));
  BA(sdis_estimator_buffer_get_rng_state(img, NULL));
  OK(sdis_estimator_buffer_get_rng_state(img, &rng_state));

  CHK(nreals + nfails == IMG_WIDTH*IMG_HEIGHT*SPP);

  fprintf(stderr, "Overall temperature ~ %g +/- %g\n", T.E, T.SE);
  fprintf(stderr, "Time per realisation (in usec) ~ %g +/- %g\n", time.E, time.SE);
  fprintf(stderr, "#failures = %lu/%lu\n",
    (unsigned long)nfails, (unsigned long)(IMG_WIDTH*IMG_HEIGHT*SPP));

  BA(sdis_estimator_buffer_get_definition(NULL, definition));
  BA(sdis_estimator_buffer_get_definition(img, NULL));
  OK(sdis_estimator_buffer_get_definition(img, definition));
  CHK(definition[0] == IMG_WIDTH);
  CHK(definition[1] == IMG_HEIGHT);

  BA(sdis_estimator_buffer_at(NULL, 0, 0, &pixel));
  BA(sdis_estimator_buffer_at(img, IMG_WIDTH+1,0 , &pixel));
  BA(sdis_estimator_buffer_at(img, 0, IMG_HEIGHT+1, &pixel));
  BA(sdis_estimator_buffer_at(img, 0, 0, NULL));

  /* Pixels ordered by row */
  FOR_EACH(y, 0, IMG_HEIGHT) {
    FOR_EACH(x, 0, IMG_WIDTH) {
      OK(sdis_estimator_buffer_at(img, x, y, &pixel));
      check_pixel(pixel);
    }
  }
}

/* Check that the images, although compatible from an estimation point of view,
 * are not the same. This should be the case if different random sequences are
 * used for each image */
static void
check_image_difference
  (const struct sdis_estimator_buffer* img0,
   const struct sdis_estimator_buffer* img1)
{
  struct sdis_mc T0 = SDIS_MC_NULL;
  struct sdis_mc T1 = SDIS_MC_NULL;
  size_t definition0[2];
  size_t definition1[2];
  size_t nreals0, nfails0;
  size_t nreals1, nfails1;

  OK(sdis_estimator_buffer_get_realisation_count(img0, &nreals0));
  OK(sdis_estimator_buffer_get_realisation_count(img1, &nreals1));

  OK(sdis_estimator_buffer_get_failure_count(img0, &nfails0));
  OK(sdis_estimator_buffer_get_failure_count(img1, &nfails1));
  CHK(nreals0 + nfails0 == nreals1 + nfails1);

  OK(sdis_estimator_buffer_get_definition(img0, definition0));
  OK(sdis_estimator_buffer_get_definition(img1, definition1));
  CHK(definition0[0] == definition1[0]);
  CHK(definition0[1] == definition1[1]);

  OK(sdis_estimator_buffer_get_temperature(img0, &T0));
  OK(sdis_estimator_buffer_get_temperature(img1, &T1));
  CHK(T0.E != T1.E || (T0.SE == 0 && T1.SE == 0));
  CHK(T0.E + 3*T0.SE >= T1.E - 3*T1.SE);
  CHK(T0.E - 3*T0.SE <= T1.E + 3*T1.SE);
}

/* Write an image in htrdr-image(5) format */
static void
write_image(FILE* stream, struct sdis_estimator_buffer* img)
{
  size_t x, y;

  /* Header */
  fprintf(stream, "%d %d\n", IMG_WIDTH, IMG_HEIGHT);

  /* Pixels ordered by row */
  FOR_EACH(y, 0, IMG_HEIGHT) {
    FOR_EACH(x, 0, IMG_WIDTH) {
      struct sdis_mc T = SDIS_MC_NULL;
      const struct sdis_estimator* pixel = NULL;

      OK(sdis_estimator_buffer_at(img, x, y, &pixel));
      OK(sdis_estimator_get_temperature(pixel, &T));
      fprintf(stream, "%g %g 0 0 0 0 0 0\n", T.E, T.SE);
    }
  }
}

static void
invalid_draw(struct sdis_scene* scn, struct sdis_camera* cam)
{
  struct sdis_solve_camera_args args = SDIS_SOLVE_CAMERA_ARGS_DEFAULT;
  struct sdis_estimator_buffer* img = NULL;

  CHK(cam && scn);

  args.cam = cam;
  args.time_range[0] = INF;
  args.time_range[1] = INF;
  args.image_definition[0] = IMG_WIDTH;
  args.image_definition[1] = IMG_HEIGHT;
  args.spp = SPP;

  BA(sdis_solve_camera(NULL, &args, &img));
  BA(sdis_solve_camera(scn, NULL, &img));
  BA(sdis_solve_camera(scn, &args, NULL));

  /* Invalid camera */
  args.cam = NULL;
  BA(sdis_solve_camera(scn, &args, &img));
  args.cam = cam;

  /* Invald time range */
  args.time_range[0] = args.time_range[1] = -1;
  BA(sdis_solve_camera(scn, &args, &img));
  args.time_range[0] = 1;
  BA(sdis_solve_camera(scn, &args, &img));
  args.time_range[1] = 0;
  BA(sdis_solve_camera(scn, &args, &img));
  args.time_range[0] = args.time_range[1] = INF;

  /* Invalid image definition */
  args.image_definition[0] = 0;
  BA(sdis_solve_camera(scn, &args, &img));
  args.image_definition[0] = IMG_WIDTH;
  args.image_definition[1] = 0;
  BA(sdis_solve_camera(scn, &args, &img));
  args.image_definition[1] = IMG_HEIGHT;

  /* Invalid number of samples per pixel */
  args.spp = 0;
  BA(sdis_solve_camera(scn, &args, &img));
  args.spp = SPP;
}

static void
draw
  (struct sdis_scene* scn,
   struct sdis_camera* cam,
   const int is_master_process)
{
  struct sdis_solve_camera_args args = SDIS_SOLVE_CAMERA_ARGS_DEFAULT;
  struct sdis_estimator_buffer* img0 = NULL;
  struct sdis_estimator_buffer* img1 = NULL;
  struct ssp_rng* rng = NULL;

  args.cam = cam;
  args.time_range[0] = INF;
  args.time_range[1] = INF;
  args.image_definition[0] = IMG_WIDTH;
  args.image_definition[1] = IMG_HEIGHT;
  args.spp = SPP;

  OK(sdis_solve_camera(scn, &args, &img0));

  if(!is_master_process) {
    CHK(img0 == NULL);
  } else {
    check_image(img0);
    write_image(stdout, img0);
  }

  /* Provide a RNG state and check that the results, although compatible, are
   * not the same */
  OK(ssp_rng_create(NULL, SSP_RNG_THREEFRY, &rng));
  OK(ssp_rng_discard(rng, 3141592653589)); /* Move the RNG state  */
  args.rng_state = rng;
  args.rng_type = SSP_RNG_TYPE_NULL;
  OK(sdis_solve_camera(scn, &args, &img1));
  if(is_master_process) {
    check_image_difference(img0, img1);
    OK(sdis_estimator_buffer_ref_put(img1));
  }
  OK(ssp_rng_ref_put(rng));

  /* Change the RNG type and check that the results, although compatible, are
   * not the same */
  args.rng_state = NULL;
  args.rng_type = SDIS_SOLVE_CAMERA_ARGS_DEFAULT.rng_type == SSP_RNG_THREEFRY
    ? SSP_RNG_MT19937_64 : SSP_RNG_THREEFRY;
  OK(sdis_solve_camera(scn, &args, &img1));
  if(is_master_process) {
    check_image_difference(img0, img1);
    OK(sdis_estimator_buffer_ref_put(img1));
  }

  if(is_master_process) OK(sdis_estimator_buffer_ref_put(img0));
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct geometry geom = GEOMETRY_NULL;
  struct sdis_camera* cam = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid0 = NULL;
  struct sdis_medium* fluid1 = NULL;
  struct sdis_medium* fluid2 = NULL;
  struct sdis_interface* interf0 = NULL;
  struct sdis_interface* interf1 = NULL;
  struct sdis_interface* interf2 = NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_scene* scn = NULL;
  struct fluid fluid_args = FLUID_NULL;
  struct solid solid_args = SOLID_NULL;
  struct interf interface_args = INTERF_NULL;
  int is_master_process;
  (void)argc, (void)argv;

  create_default_device(&argc, &argv, &is_master_process, &dev);

  radenv = create_radenv(dev, 300, 300);

  /* Create the fluid0 */
  fluid_args.temperature = 350;
  fluid_args.rho = 0;
  fluid_args.cp = 0;
  fluid0 = create_fluid(dev, &fluid_args);

  /* Create the fluid1 */
  fluid_args.temperature = 300;
  fluid_args.rho = 0;
  fluid_args.cp = 0;
  fluid1 = create_fluid(dev, &fluid_args);

  /* Create the fluid2 */
  fluid_args.temperature = 280;
  fluid_args.rho = 0;
  fluid_args.cp = 0;
  fluid2 = create_fluid(dev, &fluid_args);

  /* Create the solid medium */
  solid_args.cp = 1.0;
  solid_args.lambda = 0.1;
  solid_args.rho = 1.0;
  solid_args.delta = 1.0/20.0;
  solid_args.temperature = SDIS_TEMPERATURE_NONE;
  solid = create_solid(dev, &solid_args);

  /* Create the fluid0/solid interface */
  interface_args.hc = 1;
  interface_args.epsilon = 0;
  interface_args.specular_fraction = 0;
  interface_args.temperature = SDIS_TEMPERATURE_NONE;
  interf0 = create_interface(dev, solid, fluid0, &interface_args);

  /* Create the fluid1/solid interface */
  interface_args.hc = 0.1;
  interface_args.epsilon = 1;
  interface_args.specular_fraction = 1;
  interface_args.temperature = SDIS_TEMPERATURE_NONE;
  interface_args.reference_temperature = 300;
  interf1 = create_interface(dev, fluid1, solid, &interface_args);

  /* Create the fluid2/ground interface */
  interface_args.hc = 0.2;
  interface_args.epsilon = 1;
  interface_args.specular_fraction = 1;
  interface_args.temperature = SDIS_TEMPERATURE_NONE;
  interface_args.reference_temperature = 300;
  interf2 = create_interface(dev, fluid2, solid, &interface_args);

  add_cube(&geom, interf1);
  add_sphere(&geom, interf0);
  add_ground(&geom, interf2);

#if 0
  dump_mesh(stdout, geom.positions, sa_size(geom.positions)/3, geom.indices,
    sa_size(geom.indices)/3);
#endif

  scn = create_scene(dev, radenv, &geom);
  cam = create_camera(dev);

  invalid_draw(scn, cam);
  draw(scn, cam, is_master_process);

  /* Release memory */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid0));
  OK(sdis_medium_ref_put(fluid1));
  OK(sdis_medium_ref_put(fluid2));
  OK(sdis_radiative_env_ref_put(radenv));
  OK(sdis_scene_ref_put(scn));
  OK(sdis_camera_ref_put(cam));
  OK(sdis_interface_ref_put(interf0));
  OK(sdis_interface_ref_put(interf1));
  OK(sdis_interface_ref_put(interf2));
  free_default_device(dev);
  geometry_release(&geom);

  CHK(mem_allocated_size() == 0);
  return 0;
}
