/* Copyright (C) 2016-2021 |Meso|Star> (contact@meso-star.com)
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

#include <rsys/algorithm.h>
#include <rsys/image.h>
#include <rsys/double3.h>
#include <rsys/double33.h>
#include <rsys/math.h>
#include <rsys/stretchy_array.h>

#include <string.h>

#define UNKOWN_TEMPERATURE -1
#define IMG_WIDTH 640
#define IMG_HEIGHT 480
#define SPP 4 /* #Samples per pixel, i.e. #realisations per pixel */

/*
 * The scene is composed of a solid cube whose temperature is unknown. The
 * emissivity of the cube is 1 and its convection coefficient with the
 * surrounding fluid at 300K is 0.1. At the center of the cube there is a spherical
 * fluid cavity whose temperature is 350K. The convection coefficient between
 * the solid and the cavity is 1 and the emissivity of this interface is null.
 * The ambient radiative temperature of the system is 300K.
 *
 * In this test, we compute the radiative temperature that reaches a camera
 * that looks the cube and we `dump' a normalized image of the result.
 *
 *                      (1,1,1)
 *       +----------------+
 *      /'     #  #      /|
 *     +----*--------*--+ |
 *     | ' #          # | |
 *     | ' #   350K   # | |
 *     | '  #        #  | |
 *     | +.....#..#.....|.+
 *     |/               |/
 *     +----------------+
 * (-1,-1,-1)
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

/*******************************************************************************
 * Fluid medium
 ******************************************************************************/
struct fluid {
  double cp;
  double rho;
  double temperature;
};
static const struct fluid FLUID_NULL = {0, 0, UNKOWN_TEMPERATURE};

static double
fluid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct fluid*)sdis_data_cget(data))->cp;
}

static double
fluid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct fluid*)sdis_data_cget(data))->rho;
}

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
static const struct solid SOLID_NULL = {0, 0, 0, 0, UNKOWN_TEMPERATURE};

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
 * Interface
 ******************************************************************************/
struct interf {
  double hc;
  double epsilon;
  double specular_fraction;
  double temperature;
  double reference_temperature;
};
static const struct interf INTERF_NULL = {
  0, 0, 0, UNKOWN_TEMPERATURE, UNKOWN_TEMPERATURE
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

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->temperature;
}

static double
interface_get_reference_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interf*)sdis_data_cget(data))->reference_temperature;
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
create_solid
  (struct sdis_device* dev,
   const struct solid* param,
   struct sdis_medium** solid)
{
  struct sdis_data* data = NULL;
  struct solid* solid_param = NULL;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;

  CHK(param != NULL);
  CHK(solid != NULL);

  /* Copy the solid parameters into the Stardis memory space */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  solid_param = sdis_data_get(data);
  memcpy(solid_param, param, sizeof(struct solid));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid medium */
  OK(sdis_solid_create(dev, &solid_shader, data, solid));

  /* Release the ownership onto the Stardis memory space storing the solid
   * parameters */
  OK(sdis_data_ref_put(data));
}

static void
create_fluid
  (struct sdis_device* dev,
   const struct fluid* param,
   struct sdis_medium** fluid)
{
  struct sdis_data* data = NULL;
  struct fluid* fluid_param = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;

  CHK(param != NULL);
  CHK(fluid != NULL);

  /* Copy the fluid parameters into the Stardis memory space */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_param = sdis_data_get(data);
  memcpy(fluid_param, param, sizeof(struct fluid));

  /* Setup the fluid shader */
  fluid_shader.calorific_capacity = fluid_get_calorific_capacity;
  fluid_shader.volumic_mass = fluid_get_volumic_mass;
  fluid_shader.temperature = fluid_get_temperature;

  /* Create the fluid medium */
  OK(sdis_fluid_create(dev, &fluid_shader, data, fluid));

  /* Release the ownership onto the Stardis memory space storing the fluid
   * parameters */
  OK(sdis_data_ref_put(data));
}

static void
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* mdm_front,
   struct sdis_medium* mdm_back,
   const struct interf* param,
   struct sdis_interface** interf)
{
  struct sdis_data* data = NULL;
  struct interf* interface_param = NULL;
  struct sdis_interface_shader interface_shader = SDIS_INTERFACE_SHADER_NULL;

  CHK(mdm_front != NULL);
  CHK(mdm_back != NULL);
  CHK(param != NULL);
  CHK(interf != NULL);

  /* Copy the interface parameters into the Stardis memory space */
  OK(sdis_data_create
   (dev, sizeof(struct interf), ALIGNOF(struct interf), NULL, &data));
  interface_param = sdis_data_get(data);
  memcpy(interface_param, param, sizeof(struct interf));

  /* Setup the interface shader */
  interface_shader.convection_coef = interface_get_convection_coef;
  interface_shader.front.temperature = interface_get_temperature;
  interface_shader.back.temperature = interface_get_temperature;
  if(sdis_medium_get_type(mdm_front) == SDIS_FLUID) {
    interface_shader.front.emissivity = interface_get_emissivity;
    interface_shader.front.specular_fraction = interface_get_specular_fraction;
    interface_shader.front.reference_temperature = 
      interface_get_reference_temperature;
  }
  if(sdis_medium_get_type(mdm_back) == SDIS_FLUID) {
    interface_shader.back.emissivity = interface_get_emissivity;
    interface_shader.back.specular_fraction = interface_get_specular_fraction;
    interface_shader.back.reference_temperature =
      interface_get_reference_temperature;
  }
  /* Create the interface */
  OK(sdis_interface_create
    (dev, mdm_front, mdm_back, &interface_shader, data, interf));

  /* Release the ownership onto the Stardis memory space storing the interface
   * parameters */
  OK(sdis_data_ref_put(data));
}

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
dump_image(const struct sdis_estimator_buffer* buf)
{
  struct image img;
  double* temps = NULL;
  double Tmax = -DBL_MAX;
  double Tmin =  DBL_MAX;
  double norm;
  size_t definition[2];
  size_t ix, iy;

  CHK(buf != NULL);
  OK(sdis_estimator_buffer_get_definition(buf, definition));

  temps = mem_alloc(definition[0]*definition[1]*sizeof(double));
  CHK(temps != NULL);

  /* Check the results validity */
  FOR_EACH(iy, 0, definition[1]) {
    FOR_EACH(ix, 0, definition[0]) {
      const struct sdis_estimator* estimator;
      struct sdis_mc T;
      struct sdis_mc time;
      size_t nreals;
      size_t nfails;

      BA(sdis_estimator_buffer_at(NULL, ix, iy, &estimator));
      BA(sdis_estimator_buffer_at(buf, definition[0]+1, iy, &estimator));
      BA(sdis_estimator_buffer_at(buf, ix, definition[1]+1, &estimator));
      BA(sdis_estimator_buffer_at(buf, ix, iy, NULL));
      OK(sdis_estimator_buffer_at(buf, ix, iy, &estimator));

      OK(sdis_estimator_get_realisation_count(estimator, &nreals));
      OK(sdis_estimator_get_failure_count(estimator, &nfails));
      OK(sdis_estimator_get_temperature(estimator, &T));
      OK(sdis_estimator_get_realisation_time(estimator, &time));

      CHK(nreals + nfails == SPP);
      CHK(T.E > 0);
      CHK(time.E > 0);
    }
  }

  /* Compute the per pixel temperature */
  FOR_EACH(iy, 0, definition[1]) {
    double* row = temps + iy * definition[0];
    FOR_EACH(ix, 0, definition[0]) {
      const struct sdis_estimator* estimator;
      struct sdis_mc T;

      OK(sdis_estimator_buffer_at(buf, ix, iy, &estimator));
      OK(sdis_estimator_get_temperature(estimator, &T));

      row[ix] = T.E;
      Tmax = MMAX(row[ix], Tmax);
      Tmin = MMIN(row[ix], Tmin);
    }
  }
  if(Tmax != Tmin) {
    norm = Tmax - Tmin;
  } else {
    Tmin = 0;
    norm = 1;
  }

  /* Allocate the image memory space */
  OK(image_init(NULL, &img));
  OK(image_setup(&img, IMG_WIDTH, IMG_HEIGHT, IMG_WIDTH*3, IMAGE_RGB8, NULL));

  FOR_EACH(iy, 0, definition[1]) {
    const double* src_row = temps + iy*definition[0];
    char* dst_row = img.pixels + iy*img.pitch;
    FOR_EACH(ix, 0, definition[0]) {
      unsigned char* pixels = (unsigned char*)
        (dst_row + ix * sizeof_image_format(img.format));
      const unsigned char T = (unsigned char)
        ((src_row[ix] - Tmin)/ norm * 255.0);
      pixels[0] = T;
      pixels[1] = T;
      pixels[2] = T;
    }
  }
  OK(image_write_ppm_stream(&img, 0/*binary?*/, stdout));
  image_release(&img);
  mem_rm(temps);
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct geometry geom = GEOMETRY_NULL;
  struct s3dut_mesh* msh = NULL;
  struct s3dut_mesh_data msh_data;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  struct sdis_camera* cam = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_estimator_buffer* buf = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid0 = NULL;
  struct sdis_medium* fluid1 = NULL;
  struct sdis_interface* interf0 = NULL;
  struct sdis_interface* interf1 = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_solve_camera_args solve_args = SDIS_SOLVE_CAMERA_ARGS_DEFAULT;
  struct sdis_ambient_radiative_temperature trad =
    SDIS_AMBIENT_RADIATIVE_TEMPERATURE_NULL;
  struct ssp_rng* rng_state = NULL;
  struct fluid fluid_param = FLUID_NULL;
  struct solid solid_param = SOLID_NULL;
  struct interf interface_param = INTERF_NULL;
  struct fluid* pfluid_param = NULL;
  size_t ntris, npos;
  size_t nreals, nfails;
  size_t definition[2];
  double pos[3];
  double tgt[3];
  double up[3];
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  /* Create the fluid0 */
  fluid_param.temperature = 350;
  fluid_param.rho = 0;
  fluid_param.cp = 0;
  create_fluid(dev, &fluid_param, &fluid0);

  /* Create the fluid1 */
  fluid_param.temperature = 300;
  fluid_param.rho = 0;
  fluid_param.cp = 0;
  create_fluid(dev, &fluid_param, &fluid1);

  /* Create the solid medium */
  solid_param.cp = 1.0;
  solid_param.lambda = 0.1;
  solid_param.rho = 1.0;
  solid_param.delta = 1.0/20.0;
  solid_param.temperature = UNKOWN_TEMPERATURE;
  create_solid(dev, &solid_param, &solid);

  /* Create the fluid0/solid interface */
  interface_param.hc = 1;
  interface_param.epsilon = 0;
  interface_param.specular_fraction = 0;
  interface_param.temperature = UNKOWN_TEMPERATURE;
  create_interface(dev, solid, fluid0, &interface_param, &interf0);

  /* Create the fluid1/solid interface */
  interface_param.hc = 0.1;
  interface_param.epsilon = 1;
  interface_param.specular_fraction = 1;
  interface_param.temperature = UNKOWN_TEMPERATURE;
  interface_param.reference_temperature = 300;
  create_interface(dev, fluid1, solid, &interface_param, &interf1);

  /* Setup the cube geometry  */
  OK(s3dut_create_cuboid(NULL, 2, 2, 2, &msh));
  OK(s3dut_mesh_get_data(msh, &msh_data));
  geometry_add_shape(&geom, msh_data.positions, msh_data.nvertices,
    msh_data.indices, msh_data.nprimitives, NULL, interf1);
  OK(s3dut_mesh_ref_put(msh));

  /* Setup the sphere geometry */
  OK(s3dut_create_sphere(NULL, 0.5, 32, 16, &msh));
  OK(s3dut_mesh_get_data(msh, &msh_data));
  geometry_add_shape(&geom, msh_data.positions, msh_data.nvertices,
    msh_data.indices, msh_data.nprimitives, NULL, interf0);
  OK(s3dut_mesh_ref_put(msh));

  /* Setup the scene */
  ntris = sa_size(geom.indices) / 3; /* #primitives */
  npos = sa_size(geom.positions) / 3; /* #positions */
  scn_args.get_indices = geometry_get_indices;
  scn_args.get_interface = geometry_get_interface;
  scn_args.get_position = geometry_get_position;
  scn_args.nprimitives = ntris;
  scn_args.nvertices = npos;
  scn_args.trad.temperature = 300;
  scn_args.trad.reference = 300;
  scn_args.t_range[0] = 300;
  scn_args.t_range[1] = 350;
  scn_args.context = &geom;
  OK(sdis_scene_create(dev, &scn_args, &scn));

#if 0
  dump_mesh(stdout, geom.positions, sa_size(geom.positions)/3, geom.indices,
    sa_size(geom.indices)/3);
#endif

  /* Setup the camera */
  d3(pos, 3, 3, 3);
  d3(tgt, 0, 0, 0);
  d3(up,  0, 0, 1);
  OK(sdis_camera_create(dev, &cam));
  OK(sdis_camera_set_proj_ratio(cam, (double)IMG_WIDTH/(double)IMG_HEIGHT));
  OK(sdis_camera_set_fov(cam, MDEG2RAD(70)));
  OK(sdis_camera_look_at(cam, pos, tgt, up));

#if 0
  dump_mesh(stdout, geom.positions, npos, geom.indices, ntris);
  exit(0);
#endif
  solve_args.cam = cam;
  solve_args.time_range[0] = INF;
  solve_args.time_range[0] = INF;
  solve_args.image_resolution[0] = IMG_WIDTH;
  solve_args.image_resolution[1] = IMG_HEIGHT;
  solve_args.spp = SPP;

  BA(sdis_solve_camera(NULL, &solve_args, &buf));
  BA(sdis_solve_camera(scn, NULL, &buf));
  BA(sdis_solve_camera(scn, &solve_args, NULL));
  solve_args.cam = NULL;
  BA(sdis_solve_camera(scn, &solve_args, &buf));
  solve_args.cam = cam;
  OK(sdis_scene_get_ambient_radiative_temperature(scn, &trad));
  trad.temperature = -1;
  OK(sdis_scene_set_ambient_radiative_temperature(scn, &trad));
  BA(sdis_solve_camera(scn, &solve_args, &buf));
  trad.temperature = 300;
  OK(sdis_scene_set_ambient_radiative_temperature(scn, &trad));
  solve_args.time_range[0] = solve_args.time_range[1] = -1;
  BA(sdis_solve_camera(scn, &solve_args, &buf));
  solve_args.time_range[0] = 1;
  BA(sdis_solve_camera(scn, &solve_args, &buf));
  solve_args.time_range[1] = 0;
  BA(sdis_solve_camera(scn, &solve_args, &buf));
  solve_args.time_range[0] = solve_args.time_range[1] = INF;

  /* Launch the simulation */
  OK(sdis_solve_camera(scn, &solve_args, &buf));

  BA(sdis_estimator_buffer_get_realisation_count(NULL, &nreals));
  BA(sdis_estimator_buffer_get_realisation_count(buf, NULL));
  OK(sdis_estimator_buffer_get_realisation_count(buf, &nreals));

  BA(sdis_estimator_buffer_get_failure_count(NULL, &nfails));
  BA(sdis_estimator_buffer_get_failure_count(buf, NULL));
  OK(sdis_estimator_buffer_get_failure_count(buf, &nfails));

  BA(sdis_estimator_buffer_get_temperature(NULL, &T));
  BA(sdis_estimator_buffer_get_temperature(buf, NULL));
  OK(sdis_estimator_buffer_get_temperature(buf, &T));

  BA(sdis_estimator_buffer_get_realisation_time(NULL, &time));
  BA(sdis_estimator_buffer_get_realisation_time(buf, NULL));
  OK(sdis_estimator_buffer_get_realisation_time(buf, &time));

  BA(sdis_estimator_buffer_get_rng_state(NULL, &rng_state));
  BA(sdis_estimator_buffer_get_rng_state(buf, NULL));
  OK(sdis_estimator_buffer_get_rng_state(buf, &rng_state));

  CHK(nreals + nfails == IMG_WIDTH*IMG_HEIGHT*SPP);

  fprintf(stderr, "Overall temperature ~ %g +/- %g\n", T.E, T.SE);
  fprintf(stderr, "Time per realisation (in usec) ~ %g +/- %g\n", time.E, time.SE);
  fprintf(stderr, "#failures = %lu/%lu\n",
    (unsigned long)nfails, (unsigned long)(IMG_WIDTH*IMG_HEIGHT*SPP));

  BA(sdis_estimator_buffer_get_definition(NULL, definition));
  BA(sdis_estimator_buffer_get_definition(buf, NULL));
  OK(sdis_estimator_buffer_get_definition(buf, definition));
  CHK(definition[0] == IMG_WIDTH);
  CHK(definition[1] == IMG_HEIGHT);

  /* Write the image */
  dump_image(buf);
  OK(sdis_estimator_buffer_ref_put(buf));

  pfluid_param = sdis_data_get(sdis_medium_get_data(fluid1));
  pfluid_param->temperature = UNKOWN_TEMPERATURE;

  /* Check simulation error handling */
  BA(sdis_solve_camera(scn, &solve_args, &buf));
  solve_args.register_paths = SDIS_HEAT_PATH_ALL;
  BA(sdis_solve_camera(scn, &solve_args, &buf));

  /* Release memory */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid0));
  OK(sdis_medium_ref_put(fluid1));
  OK(sdis_scene_ref_put(scn));
  OK(sdis_camera_ref_put(cam));
  OK(sdis_interface_ref_put(interf0));
  OK(sdis_interface_ref_put(interf1));
  OK(sdis_device_ref_put(dev));
  geometry_release(&geom);

  CHK(mem_allocated_size() == 0);
  return 0;
}

