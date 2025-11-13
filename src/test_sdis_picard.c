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

#include <string.h>

#define N 10000

/* This test consists in solving the stationary temperature profile in a solid
 * slab surrounded by two different radiative temperatures (left / right). The
 * conductivity of the solid material is known, as well as its thickness and
 * the source term (volumic power density).
 *
 * The purpose is to test the Picard radiative transfer algorithm, that can be
 * compared with analytic results. This algorithm can use a possibly
 * non-uniform reference temperature field. When the reference temperature
 * field is uniform and the picard order set to 1, results should be identical
 * to the classical Monte-Carlo algorithm (using a linearized radiative
 * transfer scheme).
 *
 *   Y
 *   |                          (0.1,1)
 *   o--- X             +----------+------+ (1.1,1)
 *                      |##########|      |
 *                      |##########|      |
 *          280K     E=1|##########| E=1  | 350K
 *                      |##########|      |
 *                      |##########|      |
 *       (-1,-1)        +----------+------+
 *                    (0,-1)
 *
 *
 *
 *    Y                          (0.1, 1, 1)
 *    |                   +----------+------+ (1.1,1,1)
 *    o--- X             /##########/'     /|
 *   /                  +----------+------+ |
 *  Z                   |##########|*'    | | 350K
 *                      |##########|*'    | |
 *          280K     E=1|##########|*'E=1 | |
 *                      |##########|*+....|.+
 *                      |##########|/     |/
 *  (-1,-1,-1)          +----------+------+
 *                  (0,-1,-1)
 *
 * lambda = 1.15 W/(m.K)
 * rho = 1000 kg.m^-3
 * cp = 800 J/(kg.K)
 * emissivity = 1
 *
 * Basic Tref = 300 K
 * probe = 0.05 0 0 m
 * (power = 1000 W.m^-3) */

enum interface_type {
  ADIABATIC,
  SOLID_FLUID_mX,
  SOLID_FLUID_pX,
  BOUNDARY_pX,
  INTERFACES_COUNT__
};

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct geometry {
  const double* positions;
  const size_t* indices;
  struct sdis_interface** interfaces;
};

static const double vertices_2d[6/*#vertices*/*2/*#coords par vertex*/] = {
   0.1, -1.0,
   0.0, -1.0,
   0.0,  1.0,
   0.1,  1.0,
   1.1, -1.0,
   1.1,  1.0
};
static const size_t nvertices_2d = sizeof(vertices_2d) / (sizeof(double)*2);

static const size_t indices_2d[7/*#segments*/*2/*#indices per segment*/] = {
  0, 1, /* Solid -Y */
  1, 2, /* Solid -X */
  2, 3, /* Solid +Y */
  3, 0, /* Solid +X */

  4, 0, /* Right fluid -Y */
  3, 5, /* Right fluid +Y */
  5, 4  /* Right fluid +X */
};
static const size_t nprimitives_2d = sizeof(indices_2d) / (sizeof(size_t)*2);

static const double vertices_3d[12/*#vertices*/*3/*#coords per vertex*/] = {
   0.0,-1.0,-1.0,
   0.1,-1.0,-1.0,
   0.0, 1.0,-1.0,
   0.1, 1.0,-1.0,
   0.0,-1.0, 1.0,
   0.1,-1.0, 1.0,
   0.0, 1.0, 1.0,
   0.1, 1.0, 1.0,
   1.1,-1.0,-1.0,
   1.1, 1.0,-1.0,
   1.1,-1.0, 1.0,
   1.1, 1.0, 1.0
};
static const size_t nvertices_3d = sizeof(vertices_3d) / (sizeof(double)*3);

static const size_t indices_3d[22/*#triangles*/*3/*#indices per triangle*/] = {
  0, 2, 1, 1, 2, 3, /* Solid -Z */
  0, 4, 2, 2, 4, 6, /* Solid -X */
  4, 5, 6, 6, 5, 7, /* Solid +Z */
  3, 7, 1, 1, 7, 5, /* Solid +X */
  2, 6, 3, 3, 6, 7, /* Solid +Y */
  0, 1, 4, 4, 1, 5, /* Solid -Y */

  1,  3, 8, 8,  3,  9, /* Right fluid -Z */
  5, 10, 7, 7, 10, 11, /* Right fluid +Z */
  9, 11, 8, 8, 11, 10, /* Right fluid +X */
  3,  7, 9, 9,  7, 11, /* Right fluid +Y */
  1,  8, 5, 5,  8, 10  /* Right fluid -Y */
};
static const size_t nprimitives_3d = sizeof(indices_3d) / (sizeof(size_t)*3);

static void
get_indices_2d(const size_t iseg, size_t ids[2], void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  ids[0] = geom->indices[iseg*2+0];
  ids[1] = geom->indices[iseg*2+1];
}

static void
get_indices_3d(const size_t itri, size_t ids[3], void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  ids[0] = geom->indices[itri*3+0];
  ids[1] = geom->indices[itri*3+1];
  ids[2] = geom->indices[itri*3+2];
}

static void
get_position_2d(const size_t ivert, double pos[2], void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  pos[0] = geom->positions[ivert*2+0];
  pos[1] = geom->positions[ivert*2+1];
}

static void
get_position_3d(const size_t ivert, double pos[3], void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  pos[0] = geom->positions[ivert*3+0];
  pos[1] = geom->positions[ivert*3+1];
  pos[2] = geom->positions[ivert*3+2];
}

static void
get_interface(const size_t iprim, struct sdis_interface** bound, void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  *bound = geom->interfaces[iprim];
}

/*******************************************************************************
 * media
 ******************************************************************************/
struct solid {
  double lambda;
  double rho;
  double cp;
  double volumic_power;
};

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  const struct solid* solid = sdis_data_cget(data);
  CHK(vtx && solid);
  return solid->cp;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  const struct solid* solid = sdis_data_cget(data);
  CHK(vtx &&  solid);
  return solid->lambda;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  const struct solid* solid = sdis_data_cget(data);
  CHK(vtx && solid);
  return solid->rho;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx && data);
  return 0.0025;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx && data);
  return SDIS_TEMPERATURE_NONE;
}

static double
solid_get_volumic_power
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  const struct solid* solid = sdis_data_cget(data);
  CHK(vtx && solid);
  return solid->volumic_power;
}

static void
create_solid
  (struct sdis_device* dev,
   const struct solid* solid_props,
   struct sdis_medium** solid)
{
  struct sdis_data* data = NULL;
  struct sdis_solid_shader shader = SDIS_SOLID_SHADER_NULL;
  CHK(dev && solid_props && solid);

  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  memcpy(sdis_data_get(data), solid_props, sizeof(struct solid));
  shader.calorific_capacity = solid_get_calorific_capacity;
  shader.thermal_conductivity = solid_get_thermal_conductivity;
  shader.volumic_mass = solid_get_volumic_mass;
  shader.delta = solid_get_delta;
  shader.temperature = solid_get_temperature;
  shader.volumic_power = solid_get_volumic_power;
  OK(sdis_solid_create(dev, &shader, data, solid));
  OK(sdis_data_ref_put(data));
}

static void
create_fluid(struct sdis_device* dev, struct sdis_medium** fluid)
{
  struct sdis_fluid_shader shader = DUMMY_FLUID_SHADER;
  OK(sdis_fluid_create(dev, &shader, NULL, fluid));
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
struct interf {
  double temperature;
  double h;
  double emissivity;
  double specular_fraction;
  double Tref;
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && interf);
  return interf->temperature;
}

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && interf);
  return interf->h;
}

static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  (void)source_id;
  CHK(frag && interf);
  return interf->emissivity;
}

static double
interface_get_specular_fraction
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  (void)source_id;
  CHK(frag && interf);
  return interf->specular_fraction;
}

static double
interface_get_Tref
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && interf);
  return interf->Tref;
}

static void
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const struct interf* interf,
   struct sdis_interface** out_interf)
{
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_data* data = NULL;

  CHK(interf != NULL);

  shader.front.temperature = interface_get_temperature;
  shader.back.temperature = interface_get_temperature;
  if(sdis_medium_get_type(front) != sdis_medium_get_type(back)) {
    shader.convection_coef = interface_get_convection_coef;
  }
  if(sdis_medium_get_type(front) == SDIS_FLUID) {
    shader.front.emissivity = interface_get_emissivity;
    shader.front.specular_fraction = interface_get_specular_fraction;
    shader.front.reference_temperature = interface_get_Tref;
  }
  if(sdis_medium_get_type(back) == SDIS_FLUID) {
    shader.back.emissivity = interface_get_emissivity;
    shader.back.specular_fraction = interface_get_specular_fraction;
    shader.back.reference_temperature = interface_get_Tref;
  }
  shader.convection_coef_upper_bound = MMAX(0, interf->h);

  OK(sdis_data_create
    (dev, sizeof(struct interf), ALIGNOF(struct interf), NULL, &data));
  memcpy(sdis_data_get(data), interf, sizeof(*interf));

  OK(sdis_interface_create(dev, front, back, &shader, data, out_interf));
  OK(sdis_data_ref_put(data));
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
create_radenv(struct sdis_device* dev)
{
  struct sdis_radiative_env_shader shader = SDIS_RADIATIVE_ENV_SHADER_NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_data* data = NULL;
  struct radenv* env = NULL;

  OK(sdis_data_create(dev, sizeof(struct radenv), ALIGNOF(radenv), NULL, &data));
  env = sdis_data_get(data);
  env->temperature = 300;
  env->reference = 300;

  shader.temperature = radenv_get_temperature;
  shader.reference_temperature = radenv_get_reference_temperature;
  OK(sdis_radiative_env_create(dev, &shader, data, &radenv));
  OK(sdis_data_ref_put(data));
  return radenv;
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
struct reference_result {
  double T; /* Temperature at the center of the solid [K] */
  double T1; /* Temperature on the left boundary of the solid [K] */
  double T2; /* Temperature on the right boundary of the solid [K] */
};
static const struct reference_result REFERENCE_RESULT_NULL = {0,0,0};

static void
test_picard
  (struct sdis_scene* scn,
   const size_t picard_order,
   const enum sdis_diffusion_algorithm algo,
   const struct reference_result* ref)
{
  struct sdis_solve_probe_args probe_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_solve_boundary_args bound_args = SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT;
  struct sdis_mc mc = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  enum sdis_scene_dimension dim;
  size_t prims[2];
  enum sdis_side sides[2];
  CHK(scn && ref && picard_order >= 1);

  OK(sdis_scene_get_dimension(scn, &dim));
  switch(dim) {
    case SDIS_SCENE_2D: printf(">>> 2D\n"); break;
    case SDIS_SCENE_3D: printf(">>> 3D\n"); break;
    default: FATAL("Unreachable code.\n"); break;
  }

  probe_args.nrealisations = N;
  probe_args.position[0] = 0.05;
  probe_args.position[1] = 0;
  probe_args.position[2] = 0;
  probe_args.picard_order = picard_order;
  probe_args.diff_algo = algo;
  OK(sdis_solve_probe(scn, &probe_args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &mc));
  printf("Temperature at `%g %g %g' = %g ~ %g +/- %g\n",
    SPLIT3(probe_args.position), ref->T, mc.E, mc.SE);
  CHK(eq_eps(ref->T, mc.E, mc.SE*3));
  OK(sdis_estimator_ref_put(estimator));

  switch(dim) {
    case SDIS_SCENE_2D:
      prims[0] = 1; sides[0] = SDIS_BACK;
      bound_args.nprimitives = 1;
      break;
    case SDIS_SCENE_3D:
      prims[0] = 2; sides[0] = SDIS_BACK;
      prims[1] = 3; sides[1] = SDIS_BACK;
      bound_args.nprimitives = 2;
      break;
    default: FATAL("Unreachable code.\n"); break;
  }
  bound_args.nrealisations = N;
  bound_args.primitives = prims;
  bound_args.sides = sides;
  bound_args.picard_order = picard_order;
  OK(sdis_solve_boundary(scn, &bound_args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &mc));
  printf("T1 = %g ~ %g +/- %g\n", ref->T1, mc.E, mc.SE);
  CHK(eq_eps(ref->T1, mc.E, mc.SE*3));
  OK(sdis_estimator_ref_put(estimator));

  switch(dim) {
    case SDIS_SCENE_2D:
      prims[0] = 3; sides[0] = SDIS_BACK;
      bound_args.nprimitives = 1;
      break;
    case SDIS_SCENE_3D:
      prims[0] = 6; sides[0] = SDIS_BACK;
      prims[1] = 7; sides[1] = SDIS_BACK;
      bound_args.nprimitives = 2;
      break;
    default: FATAL("Unreachable code.\n"); break;
  }
  bound_args.nrealisations = N;
  bound_args.primitives = prims;
  bound_args.sides = sides;
  bound_args.picard_order = picard_order;
  OK(sdis_solve_boundary(scn, &bound_args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &mc));
  printf("T2 = %g ~ %g +/- %g\n", ref->T2, mc.E, mc.SE);
  CHK(eq_eps(ref->T2, mc.E, mc.SE*3));
  OK(sdis_estimator_ref_put(estimator));
}

static void
register_heat_paths(struct sdis_scene* scn, const size_t picard_order, FILE* stream)
{
  struct sdis_solve_probe_args probe_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_estimator* estimator = NULL;
  CHK(scn && picard_order >= 1 && stream);


  probe_args.nrealisations = 10;
  probe_args.position[0] = 0.05;
  probe_args.position[1] = 0;
  probe_args.position[2] = 0;
  probe_args.picard_order = picard_order;
  probe_args.register_paths = SDIS_HEAT_PATH_ALL;
  printf("Register %lu heat paths.\n", probe_args.nrealisations);
  OK(sdis_solve_probe(scn, &probe_args, &estimator));
  dump_heat_paths(stream, estimator);
  OK(sdis_estimator_ref_put(estimator));
}

static void
create_scene_3d
  (struct sdis_device* dev,
   struct sdis_interface* interfaces[INTERFACES_COUNT__],
   struct sdis_radiative_env* radenv,
   struct sdis_scene** scn)
{
  struct geometry geom;
  struct sdis_interface* prim_interfaces[32];
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;

  CHK(dev && interfaces && radenv && scn);

  /* Setup the per primitive interface of the solid medium */
  prim_interfaces[0] = prim_interfaces[1] = interfaces[ADIABATIC];
  prim_interfaces[2] = prim_interfaces[3] = interfaces[SOLID_FLUID_mX];
  prim_interfaces[4] = prim_interfaces[5] = interfaces[ADIABATIC];
  prim_interfaces[6] = prim_interfaces[7] = interfaces[SOLID_FLUID_pX];
  prim_interfaces[8] = prim_interfaces[9] = interfaces[ADIABATIC];
  prim_interfaces[10] = prim_interfaces[11] = interfaces[ADIABATIC];

  /* Setup the per primitive interface for the right fluid */
  prim_interfaces[12] = prim_interfaces[13] = interfaces[BOUNDARY_pX];
  prim_interfaces[14] = prim_interfaces[15] = interfaces[BOUNDARY_pX];
  prim_interfaces[16] = prim_interfaces[17] = interfaces[BOUNDARY_pX];
  prim_interfaces[18] = prim_interfaces[19] = interfaces[BOUNDARY_pX];
  prim_interfaces[20] = prim_interfaces[21] = interfaces[BOUNDARY_pX];

  /* Create the scene */
  geom.positions = vertices_3d;
  geom.indices = indices_3d;
  geom.interfaces = prim_interfaces;
  scn_args.get_indices = get_indices_3d;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position_3d;
  scn_args.nprimitives = nprimitives_3d;
  scn_args.nvertices = nvertices_3d;
  scn_args.t_range[0] = 280;
  scn_args.t_range[1] = 350;
  scn_args.radenv = radenv;
  scn_args.context = &geom;
  OK(sdis_scene_create(dev, &scn_args, scn));
}

static void
create_scene_2d
  (struct sdis_device* dev,
   struct sdis_interface* interfaces[INTERFACES_COUNT__],
   struct sdis_radiative_env* radenv,
   struct sdis_scene** scn)
{
  struct geometry geom;
  struct sdis_interface* prim_interfaces[10/*#segment*/];
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;

  CHK(dev && interfaces && radenv && scn);

  /* Setup the per primitive interface of the solid medium */
  prim_interfaces[0] = interfaces[ADIABATIC];
  prim_interfaces[1] = interfaces[SOLID_FLUID_mX];
  prim_interfaces[2] = interfaces[ADIABATIC];
  prim_interfaces[3] = interfaces[SOLID_FLUID_pX];

  /* Setup the per primitive interface of the fluid on the right of the medium */
  prim_interfaces[4] = interfaces[BOUNDARY_pX];
  prim_interfaces[5] = interfaces[BOUNDARY_pX];
  prim_interfaces[6] = interfaces[BOUNDARY_pX];

  /* Create the scene */
  geom.positions = vertices_2d;
  geom.indices = indices_2d;
  geom.interfaces = prim_interfaces;
  scn_args.get_indices = get_indices_2d;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position_2d;
  scn_args.nprimitives = nprimitives_2d;
  scn_args.nvertices = nvertices_2d;
  scn_args.t_range[0] = 280;
  scn_args.t_range[1] = 350;
  scn_args.radenv = radenv;
  scn_args.context = &geom;
  OK(sdis_scene_2d_create(dev, &scn_args, scn));
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  FILE* stream = NULL;

  struct sdis_device* dev = NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_scene* scn_2d = NULL;
  struct sdis_scene* scn_3d = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* dummy = NULL;
  struct sdis_interface* interfaces[INTERFACES_COUNT__];

  struct radenv* radenv_props = NULL;
  struct solid solid_props;
  struct solid* psolid_props;
  struct reference_result ref = REFERENCE_RESULT_NULL;
  struct interf interf_props;
  struct interf* pinterf_props[INTERFACES_COUNT__];

  double t_range[2];

  size_t i;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  radenv = create_radenv(dev);
  radenv_props = sdis_data_get(sdis_radiative_env_get_data(radenv));

  /* Solid medium */
  solid_props.lambda = 1.15;
  solid_props.rho = 1000;
  solid_props.cp = 800;
  solid_props.volumic_power = SDIS_VOLUMIC_POWER_NONE;
  create_solid(dev, &solid_props, &solid);

  /* Dummy solid medium */
  solid_props.lambda = 0;
  solid_props.rho = 1000;
  solid_props.cp = 800;
  solid_props.volumic_power = SDIS_VOLUMIC_POWER_NONE;
  create_solid(dev, &solid_props, &dummy);

  /* Fluid medium */
  create_fluid(dev, &fluid);

  /* Create the adiabatic interface for the solid */
  interf_props.temperature = SDIS_TEMPERATURE_NONE;
  interf_props.h = -1;
  interf_props.emissivity = -1;
  interf_props.specular_fraction = -1;
  interf_props.Tref = SDIS_TEMPERATURE_NONE;
  create_interface(dev, solid, dummy, &interf_props, interfaces+ADIABATIC);

  /* Create the interface between the solid and the fluid */
  interf_props.temperature = SDIS_TEMPERATURE_NONE;
  interf_props.h = 0;
  interf_props.emissivity = 1;
  interf_props.specular_fraction = 0;
  interf_props.Tref = 280;
  create_interface(dev, solid, fluid, &interf_props, interfaces+SOLID_FLUID_mX);
  interf_props.Tref = 350;
  create_interface(dev, solid, fluid, &interf_props, interfaces+SOLID_FLUID_pX);

  /* Create the interface for the fluid on the right */
  interf_props.temperature = 350;
  interf_props.h = -1;
  interf_props.emissivity = 1;
  interf_props.specular_fraction = 0;
  interf_props.Tref = 350;
  create_interface(dev, fluid, dummy, &interf_props, interfaces+BOUNDARY_pX);

  /* Fetch pointers toward the solid and the interfaces */
  psolid_props = sdis_data_get(sdis_medium_get_data(solid));
  FOR_EACH(i, 0, INTERFACES_COUNT__) {
    pinterf_props[i] = sdis_data_get(sdis_interface_get_data(interfaces[i]));
  }

  create_scene_2d(dev, interfaces, radenv, &scn_2d);
  create_scene_3d(dev, interfaces, radenv, &scn_3d);

  CHK((stream = tmpfile()) != NULL);

  /* Test picard1 with a constant Tref <=> regular linearisation */
  printf("Test Picard1 with a constant Tref of 300 K\n");
  ref.T  = 314.99999999999989;
  ref.T1 = 307.64122364709766;
  ref.T2 = 322.35877635290217;
  pinterf_props[SOLID_FLUID_mX]->Tref = 300;
  pinterf_props[SOLID_FLUID_pX]->Tref = 300;
  pinterf_props[BOUNDARY_pX]->Tref = 300;
  radenv_props->temperature = 280;
  radenv_props->reference = 300;
  test_picard(scn_2d, 1/*Picard order*/, SDIS_DIFFUSION_WOS, &ref);
  test_picard(scn_3d, 1/*Picard order*/, SDIS_DIFFUSION_WOS, &ref);
  printf("\n");

  /* Test picard1 using T4 as a reference */
  printf("Test Picard1 using T4 as a reference\n");
  ref.T  = 320.37126474482994;
  ref.T1 = 312.12650299072266;
  ref.T2 = 328.61602649893723;
  pinterf_props[SOLID_FLUID_mX]->Tref = ref.T1;
  pinterf_props[SOLID_FLUID_pX]->Tref = ref.T2;
  pinterf_props[BOUNDARY_pX]->Tref = 350;
  radenv_props->temperature = 280;
  radenv_props->reference = 280;
  test_picard(scn_2d, 1/*Picard order*/, SDIS_DIFFUSION_DELTA_SPHERE, &ref);
  test_picard(scn_3d, 1/*Picard order*/, SDIS_DIFFUSION_DELTA_SPHERE, &ref);
  printf("\n");

  /* Test picard2  */
  printf("Test Picard2 with a constant Tref of 300K\n");
  ref.T  = 320.37126474482994;
  ref.T1 = 312.12650299072266;
  ref.T2 = 328.61602649893723;
  pinterf_props[SOLID_FLUID_mX]->Tref = 300;
  pinterf_props[SOLID_FLUID_pX]->Tref = 300;
  pinterf_props[BOUNDARY_pX]->Tref = 300;
  radenv_props->temperature = 280;
  radenv_props->reference = 300;
  test_picard(scn_2d, 2/*Picard order*/, SDIS_DIFFUSION_WOS, &ref);
  test_picard(scn_3d, 2/*Picard order*/, SDIS_DIFFUSION_WOS, &ref);
  printf("\n");

  t_range[0] = 200;
  t_range[1] = 500;

  OK(sdis_scene_set_temperature_range(scn_2d, t_range));
  OK(sdis_scene_set_temperature_range(scn_3d, t_range));

  /* Test picard3  */
  printf("Test Picard3 with a delta T of 300K\n");
  ref.T  = 416.4023;
  ref.T1 = 372.7557;
  ref.T2 = 460.0489;
  pinterf_props[BOUNDARY_pX]->temperature = t_range[1];
  pinterf_props[SOLID_FLUID_mX]->Tref = 350;
  pinterf_props[SOLID_FLUID_pX]->Tref = 450;
  pinterf_props[BOUNDARY_pX]->Tref = pinterf_props[BOUNDARY_pX]->temperature;
  radenv_props->temperature = t_range[0];
  radenv_props->reference = t_range[0];
  test_picard(scn_2d, 3/*Picard order*/, SDIS_DIFFUSION_WOS, &ref);
  test_picard(scn_3d, 3/*Picard order*/, SDIS_DIFFUSION_WOS, &ref);
  register_heat_paths(scn_2d, 3/*Picard order*/, stream);
  register_heat_paths(scn_3d, 3/*Picard order*/, stream);
  printf("\n");

  t_range[0] = 280;
  t_range[1] = 350;
  OK(sdis_scene_set_temperature_range(scn_2d, t_range));
  OK(sdis_scene_set_temperature_range(scn_3d, t_range));
  pinterf_props[BOUNDARY_pX]->temperature = t_range[1];

  /* Add volumic power */
  psolid_props->volumic_power = 1000;

  /* Test picard1 with a volumic power and constant Tref */
  printf("Test Picard1 with a volumic power of 1000 W/m^3 and a constant Tref "
    "of 300 K\n");
  ref.T  = 324.25266420769509;
  ref.T1 = 315.80693133305368;
  ref.T2 = 330.52448403885825;
  pinterf_props[SOLID_FLUID_mX]->Tref = 300;
  pinterf_props[SOLID_FLUID_pX]->Tref = 300;
  pinterf_props[BOUNDARY_pX]->Tref = 300;
  radenv_props->temperature = t_range[0];
  radenv_props->reference = 300;
  test_picard(scn_2d, 1/*Picard order*/, SDIS_DIFFUSION_DELTA_SPHERE, &ref);
  test_picard(scn_3d, 1/*Picard order*/, SDIS_DIFFUSION_DELTA_SPHERE, &ref);
  printf("\n");

  /* Test picard1 with a volumic power and T4 a the reference */
  printf("Test Picard1 with a volumic power of 1000 W/m^3 and T4 as a reference\n");
  ref.T  = 327.95981050850446;
  ref.T1 = 318.75148773193359;
  ref.T2 = 334.99422024159708;
  pinterf_props[SOLID_FLUID_mX]->Tref = ref.T1;
  pinterf_props[SOLID_FLUID_pX]->Tref = ref.T2;
  pinterf_props[BOUNDARY_pX]->Tref = 350;
  radenv_props->temperature = 280;
  radenv_props->reference = 280;
  test_picard(scn_2d, 1/*Picard order*/, SDIS_DIFFUSION_WOS, &ref);
  test_picard(scn_3d, 1/*Picard order*/, SDIS_DIFFUSION_WOS, &ref);
  printf("\n");

  /* Release memory */
  OK(sdis_radiative_env_ref_put(radenv));
  OK(sdis_scene_ref_put(scn_2d));
  OK(sdis_scene_ref_put(scn_3d));
  OK(sdis_interface_ref_put(interfaces[ADIABATIC]));
  OK(sdis_interface_ref_put(interfaces[SOLID_FLUID_mX]));
  OK(sdis_interface_ref_put(interfaces[SOLID_FLUID_pX]));
  OK(sdis_interface_ref_put(interfaces[BOUNDARY_pX]));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_device_ref_put(dev));
  CHK(fclose(stream) == 0);

  CHK(mem_allocated_size() == 0);
  return 0;
}
