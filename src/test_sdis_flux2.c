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

/* This test consists in solving the temperature profile in a solid slab
 * surrounded by two different convective and radiative temperatures. The
 * conductivity of the solid material is known, as well as its thickness. A net
 * flux is fixed on the left side of the slab.
 *
 *
 *    Y                   ////(0.1,1,1) 280K
 *    |                   +----------+-------+ (1.1,1,1)
 *    o--- X             /##########/'E=1   /|
 *   /                  +----------+-------+ |
 *  Z           /_      |##########|*' _\  | |
 *      --->    \ \  E=1|##########|*'/ /  |280K
 *    320K->   \__/     |##########|*'\__/ | |
 *      --->   330K     |##########|*'290K | |
 *                   --\|##########|*+.... |.+
 *       10000W.m^-2 --/|##########|/      |/
 *  (-1,-1,-1)          +----------+-------+
 *                  (0,-1,-1)/////// 280K
 *
 */

enum interface_type {
  ADIABATIC,
  FIXED_TEMPERATURE,
  SOLID_FLUID_WITH_FLUX,
  SOLID_FLUID,
  INTERFACES_COUNT__
};

struct probe {
  double x;
  double time;
  double ref; /* Analytically computed temperature */
};

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct geometry {
  const double* positions;
  const size_t* indices;
  struct sdis_interface** interfaces;
};

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
get_indices_3d(const size_t itri, size_t ids[3], void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  ids[0] = geom->indices[itri*3+0];
  ids[1] = geom->indices[itri*3+1];
  ids[2] = geom->indices[itri*3+2];
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
 * Solid media
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
  return 0.005;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  const struct solid* solid = sdis_data_cget(data);
  CHK(vtx && solid);

  if(vtx->time > 0) {
    return SDIS_TEMPERATURE_NONE;
  } else {
    /* The initial temperature is a linear profile between T1 and T2, where T1
     * and T2 are the temperature on the left and right slab boundary,
     * respectively. */
    const double T1 = 306.334;
    const double T2 = 294.941;
    const double u = CLAMP(vtx->P[0] / 0.1, 0.0, 1.0);
    return u*(T2 - T1) + T1;
  }
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
  OK(sdis_solid_create(dev, &shader, data, solid));
  OK(sdis_data_ref_put(data));
}

/*******************************************************************************
 * Fluid media
 ******************************************************************************/
struct fluid {
  double temperature;
};

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  const struct fluid* fluid = sdis_data_cget(data);
  CHK(vtx && fluid);
  return fluid->temperature;
}

static void
create_fluid
  (struct sdis_device* dev,
   const struct fluid* fluid_props,
   struct sdis_medium** fluid)
{
  struct sdis_data* data = NULL;
  struct sdis_fluid_shader shader = DUMMY_FLUID_SHADER;
  CHK(dev && fluid_props && fluid);

  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  memcpy(sdis_data_get(data), fluid_props, sizeof(struct fluid));
  shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &shader, data, fluid));
  OK(sdis_data_ref_put(data));
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
struct interf {
  double h;
  double emissivity;
  double phi;
  double temperature;
  double Tref;
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && data);
  return interf->temperature;
}

static double
interface_get_reference_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && interf);
  return interf->Tref;
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
  (void)source_id;
  CHK(frag && data);
  return 0; /* Unused */
}

static double
interface_get_flux
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && interf);
  return interf->phi;
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
  shader.front.flux = interface_get_flux;
  shader.back.flux = interface_get_flux;

  if(sdis_medium_get_type(front) != sdis_medium_get_type(back)) {
    shader.convection_coef = interface_get_convection_coef;
  }
  if(sdis_medium_get_type(front) == SDIS_FLUID) {
    shader.front.emissivity = interface_get_emissivity;
    shader.front.specular_fraction = interface_get_specular_fraction;
    shader.front.reference_temperature = interface_get_reference_temperature;
  }
  if(sdis_medium_get_type(back) == SDIS_FLUID) {
    shader.back.emissivity = interface_get_emissivity;
    shader.back.specular_fraction = interface_get_specular_fraction;
    shader.back.reference_temperature = interface_get_reference_temperature;
  }
  shader.convection_coef_upper_bound = MMAX(0, interf->h);

  OK(sdis_data_create
    (dev, sizeof(struct interf), ALIGNOF(struct interf), NULL, &data));
  memcpy(sdis_data_get(data), interf, sizeof(*interf));

  OK(sdis_interface_create(dev, front, back, &shader, data, out_interf));
  OK(sdis_data_ref_put(data));
}

/*******************************************************************************
 * Create the radiative environment
 ******************************************************************************/
static double
radenv_get_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray, (void)data;
  return 320; /* [K] */
}

static double
radenv_get_reference_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray, (void)data;
  return 300; /* [K] */
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
 * Create scene
 ******************************************************************************/
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
  prim_interfaces[2] = prim_interfaces[3] = interfaces[SOLID_FLUID_WITH_FLUX];
  prim_interfaces[4] = prim_interfaces[5] = interfaces[ADIABATIC];
  prim_interfaces[6] = prim_interfaces[7] = interfaces[SOLID_FLUID];
  prim_interfaces[8] = prim_interfaces[9] = interfaces[ADIABATIC];
  prim_interfaces[10] = prim_interfaces[11] = interfaces[ADIABATIC];

  /* Setup the per primitive interface for the right fluid */
  prim_interfaces[12] = prim_interfaces[13] = interfaces[FIXED_TEMPERATURE];
  prim_interfaces[14] = prim_interfaces[15] = interfaces[FIXED_TEMPERATURE];
  prim_interfaces[16] = prim_interfaces[17] = interfaces[FIXED_TEMPERATURE];
  prim_interfaces[18] = prim_interfaces[19] = interfaces[FIXED_TEMPERATURE];
  prim_interfaces[20] = prim_interfaces[21] = interfaces[FIXED_TEMPERATURE];

  /* Create the scene */
  geom.positions = vertices_3d;
  geom.indices = indices_3d;
  geom.interfaces = prim_interfaces;
  scn_args.get_indices = get_indices_3d;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position_3d;
  scn_args.nprimitives = nprimitives_3d;
  scn_args.nvertices = nvertices_3d;
  scn_args.t_range[0] = 300;
  scn_args.t_range[1] = 300;
  scn_args.context = &geom;
  scn_args.radenv = radenv;
  OK(sdis_scene_create(dev, &scn_args, scn));
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
check(struct sdis_scene* scn, const struct probe* probe)
{
  struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_estimator* estimator = NULL;
  struct sdis_mc T = SDIS_MC_NULL;

  CHK(scn && probe);

  args.nrealisations = 10000;
  args.picard_order = 1;
  args.position[0] = probe->x;
  args.position[1] = 0;
  args.position[2] = 0;
  args.time_range[0] = probe->time;
  args.time_range[1] = probe->time;

  OK(sdis_solve_probe(scn, &args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_ref_put(estimator));

  printf("Temperature at x=%g, t=%g: %g ~ %g +/- %g\n",
    probe->x, probe->time, probe->ref, T.E, T.SE);

  CHK(eq_eps(probe->ref, T.E, T.SE*3));
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* dev = NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_scene* scn_3d = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* dummy = NULL;
  struct sdis_medium* fluid1 = NULL;
  struct sdis_medium* fluid2 = NULL;
  struct sdis_interface* interfaces[INTERFACES_COUNT__];

  struct solid solid_props;
  struct fluid fluid_props;
  struct interf interf_props;

  const struct probe probes[] = {
    {0.01, 1000.0, 481.72005748728628},
    {0.05, 1000.0, 335.19469995601020},
    {0.09, 1000.0, 299.94436943411478},
    {0.01, 2000.0, 563.21759568607558},
    {0.05, 2000.0, 392.79827670626440},
    {0.09, 2000.0, 324.89742556243448},
    {0.01, 3000.0, 620.25242712533577},
    {0.05, 3000.0, 444.73414407361213},
    {0.09, 3000.0, 359.44045704073852},
    {0.01, 4000.0, 665.65935222224493},
    {0.05, 4000.0, 490.32470982110840},
    {0.09, 4000.0, 393.89924931902408},
    {0.01, 10000.0, 830.44439052891505},
    {0.05, 10000.0, 664.82771620162805},
    {0.09, 10000.0, 533.92442748613928}
  };
  const size_t nprobes = sizeof(probes)/sizeof(*probes);
  size_t iprobe;

  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  radenv = create_radenv(dev);

  /* Solid medium */
  solid_props.lambda = 1.15;
  solid_props.rho = 1700;
  solid_props.cp = 800;
  create_solid(dev, &solid_props, &solid);

  /* Dummy solid medium */
  solid_props.lambda = 0;
  solid_props.rho = 1700;
  solid_props.cp = 800;
  create_solid(dev, &solid_props, &dummy);

  /* Fluid media */
  fluid_props.temperature = 330;
  create_fluid(dev, &fluid_props, &fluid1);
  fluid_props.temperature = 290;
  create_fluid(dev, &fluid_props, &fluid2);

  /* Adiabatic interfaces */
  interf_props.h = 0;
  interf_props.emissivity = 0;
  interf_props.phi = SDIS_FLUX_NONE;
  interf_props.temperature = SDIS_TEMPERATURE_NONE;
  interf_props.Tref = SDIS_TEMPERATURE_NONE;
  create_interface(dev, solid, dummy, &interf_props, &interfaces[ADIABATIC]);

  /* Interfaces with a fixed temperature */
  interf_props.h = 1;
  interf_props.emissivity = 1;
  interf_props.phi = SDIS_FLUX_NONE;
  interf_props.temperature = 280;
  interf_props.Tref = 300;
  create_interface
    (dev, fluid2, dummy, &interf_props, &interfaces[FIXED_TEMPERATURE]);

  /* Interfaces with a fixed flux */
  interf_props.h = 2;
  interf_props.emissivity = 1;
  interf_props.phi = 10000;
  interf_props.temperature = SDIS_TEMPERATURE_NONE;
  interf_props.Tref = 300;
  create_interface
    (dev, solid, fluid1, &interf_props, &interfaces[SOLID_FLUID_WITH_FLUX]);

  interf_props.h = 8;
  interf_props.emissivity = 1;
  interf_props.phi = SDIS_FLUX_NONE;
  interf_props.temperature = SDIS_TEMPERATURE_NONE;
  interf_props.Tref = 300;
  create_interface
    (dev, solid, fluid2, &interf_props, &interfaces[SOLID_FLUID]);

  create_scene_3d(dev, interfaces, radenv, &scn_3d);

  FOR_EACH(iprobe, 0, nprobes) {
    check(scn_3d, &probes[iprobe]);
  }

  /* Release memory */
  OK(sdis_radiative_env_ref_put(radenv));
  OK(sdis_scene_ref_put(scn_3d));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_medium_ref_put(fluid1));
  OK(sdis_medium_ref_put(fluid2));
  OK(sdis_interface_ref_put(interfaces[ADIABATIC]));
  OK(sdis_interface_ref_put(interfaces[FIXED_TEMPERATURE]));
  OK(sdis_interface_ref_put(interfaces[SOLID_FLUID_WITH_FLUX]));
  OK(sdis_interface_ref_put(interfaces[SOLID_FLUID]));
  OK(sdis_device_ref_put(dev));

  CHK(mem_allocated_size() == 0);
  return 0;
}
