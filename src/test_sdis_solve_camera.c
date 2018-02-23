/* Copyright (C) |Meso|Star> 2016-2018 (contact@meso-star.com)
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
#include <rsys/double3.h>
#include <rsys/double33.h>
#include <rsys/math.h>
#include <rsys/stretchy_array.h>

#include <string.h>

#define UNKOWN_TEMPERATURE -1

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
solid_get_delta_boundary
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct solid*)sdis_data_cget(data))->delta * 2.1;
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
};
static const struct interf INTERF_NULL = {0, 0, 0, UNKOWN_TEMPERATURE};

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
  CHK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data) == RES_OK);
  solid_param = sdis_data_get(data);
  memcpy(solid_param, param, sizeof(struct solid));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.delta_boundary = solid_get_delta_boundary;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid medium */
  CHK(sdis_solid_create(dev, &solid_shader, data, solid) == RES_OK);

  /* Release the ownership onto the Stardis memory space storing the solid
   * parameters */
  CHK(sdis_data_ref_put(data) == RES_OK);
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
  CHK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data) == RES_OK);
  fluid_param = sdis_data_get(data);
  memcpy(fluid_param, param, sizeof(struct fluid));

  /* Setup the fluid shader */
  fluid_shader.calorific_capacity = fluid_get_calorific_capacity;
  fluid_shader.volumic_mass = fluid_get_volumic_mass;
  fluid_shader.temperature = fluid_get_temperature;

  /* Create the fluid medium */
  CHK(sdis_fluid_create(dev, &fluid_shader, data, fluid) == RES_OK);

  /* Release the ownership onto the Stardis memory space storing the fluid
   * parameters */
  CHK(sdis_data_ref_put(data) == RES_OK);
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
  struct sdis_interface_shader interface_shader = DUMMY_INTERFACE_SHADER;

  CHK(mdm_front != NULL);
  CHK(mdm_back != NULL);
  CHK(param != NULL);
  CHK(interf != NULL);

  /* Copy the interface parameters into the Stardis memory space */
  CHK(sdis_data_create
   (dev, sizeof(struct interf), ALIGNOF(struct interf), NULL, &data) == RES_OK);
  interface_param = sdis_data_get(data);
  memcpy(interface_param, param, sizeof(struct interf));

  /* Setup the interface shader */
  interface_shader.convection_coef = interface_get_convection_coef;
  interface_shader.temperature = interface_get_temperature;
  interface_shader.emissivity = interface_get_emissivity;
  interface_shader.specular_fraction = interface_get_specular_fraction;

  /* Create the interface */
  CHK(sdis_interface_create
    (dev, mdm_front, mdm_back, &interface_shader, data, interf) == RES_OK);

  /* Release the ownership onto the Stardis memory space storing the interface
   * parameters */
  CHK(sdis_data_ref_put(data) == RES_OK);
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

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct geometry geom = GEOMETRY_NULL;
  struct s3dut_mesh* msh = NULL;
  struct s3dut_mesh_data msh_data;
  struct sdis_device* dev = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid0 = NULL;
  struct sdis_medium* fluid1 = NULL;
  struct sdis_interface* interf0 = NULL;
  struct sdis_interface* interf1 = NULL;
  struct sdis_scene* scn = NULL;
  struct fluid fluid_param = FLUID_NULL;
  struct solid solid_param = SOLID_NULL;
  struct interf interface_param = INTERF_NULL;
  size_t ntris, npos;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev) == RES_OK);

  /* Create the fluid0 */
  fluid_param.temperature = 300;
  fluid_param.rho = 0;
  fluid_param.cp = 0;
  create_fluid(dev, &fluid_param, &fluid0);

  /* Create the fluid1 */
  fluid_param.temperature = UNKOWN_TEMPERATURE;
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
  interface_param.hc = 0;
  interface_param.epsilon = 1;
  interface_param.specular_fraction = 1;
  interface_param.temperature = UNKOWN_TEMPERATURE;
  create_interface(dev, fluid1, solid, &interface_param, &interf1);

  /* Release the ownership onto the media */
  CHK(sdis_medium_ref_put(solid) == RES_OK);
  CHK(sdis_medium_ref_put(fluid0) == RES_OK);
  CHK(sdis_medium_ref_put(fluid1) == RES_OK);

  /* Setup the cube geometry  */
  CHK(s3dut_create_cuboid(&allocator, 2, 2, 2, &msh) == RES_OK);
  CHK(s3dut_mesh_get_data(msh, &msh_data) == RES_OK);
  geometry_add_shape(&geom, msh_data.positions, msh_data.nvertices,
    msh_data.indices, msh_data.nprimitives, NULL, interf1);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  /* Setup the sphere geometry */
  CHK(s3dut_create_sphere(&allocator, 1, 32, 16, &msh) == RES_OK);
  CHK(s3dut_mesh_get_data(msh, &msh_data) == RES_OK);
  geometry_add_shape(&geom, msh_data.positions, msh_data.nvertices,
    msh_data.indices, msh_data.nprimitives, NULL, interf1);
  CHK(s3dut_mesh_ref_put(msh) == RES_OK);

  /* Setup the sphere geometry */
  ntris = sa_size(geom.indices) / 3; /* #primitives */
  npos = sa_size(geom.positions) / 3; /* #positions */
  CHK(sdis_scene_create(dev, ntris, geometry_get_indices,
    geometry_get_interface, npos, geometry_get_position,
    &geom, &scn) == RES_OK);

  /* Release memory */
  CHK(sdis_scene_ref_put(scn) == RES_OK);
  CHK(sdis_interface_ref_put(interf0) == RES_OK);
  CHK(sdis_interface_ref_put(interf1) == RES_OK);
  CHK(sdis_device_ref_put(dev) == RES_OK);
  geometry_release(&geom);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

