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

/* This test verifies multi-material enclosures, i.e. enclosures whose
 * interfaces refer to several media, whereas an enclosure should only cover
 * one. This configuration remains valid, however, if these enclosures are used
 * to define boundary conditions, such as convective exchange with several
 * fluids at known temperature. Although they are valid, it is forbidden to
 * place a probe in these enclosures.
 *
 *        +----------------------+
 *        |                      |
 *        |  +----------------+  |
 *        |  |       T2       |  |
 *        |  |                |  | T4
 *        |  |       __\      |  |   __\
 *        |  | T1   /  /   T3 |  |  /  / Text
 *        |  |      \__/      |  |  \__/
 *        |  |                |  |
 *        |  |       T0       |  |
 *        |  +----------------+  |
 * Y      |                      |
 * |      +----------------------+
 * o--X */

#define CONVECTION_COEF 10
#define DELTA 0.00625
#define NREALISATIONS 10000
#define Text 360.0

/*******************************************************************************
 * Media
 ******************************************************************************/
#define DEFINE_MEDIUM_GETTER(Func, Val)                                        \
  static double                                                                \
  Func(const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)            \
  {                                                                            \
    (void)vtx, (void)data;                                                     \
    return (Val);                                                              \
  }
DEFINE_MEDIUM_GETTER(solid_get_calorific_capacity, 1)
DEFINE_MEDIUM_GETTER(solid_get_thermal_conductivity, 1)
DEFINE_MEDIUM_GETTER(solid_get_volumic_mass, 1)
DEFINE_MEDIUM_GETTER(solid_get_delta, DELTA)
DEFINE_MEDIUM_GETTER(solid_get_temperature, SDIS_TEMPERATURE_NONE)
DEFINE_MEDIUM_GETTER(fluid_get_temperature, *((double*)sdis_data_cget(data)))
#undef DEFINE_MEDIUM_GETTER

static struct sdis_medium*
create_solid(struct sdis_device* dev)
{
  struct sdis_solid_shader shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_medium* solid = NULL;

  shader.calorific_capacity = solid_get_calorific_capacity;
  shader.thermal_conductivity = solid_get_thermal_conductivity;
  shader.volumic_mass = solid_get_volumic_mass;
  shader.delta = solid_get_delta;
  shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(dev, &shader, NULL, &solid));
  return solid;
}

static struct sdis_medium*
create_fluid(struct sdis_device* dev, const double temperature)
{
  struct sdis_fluid_shader shader = DUMMY_FLUID_SHADER;
  struct sdis_medium* fluid = NULL;
  struct sdis_data* data = NULL;

  OK(sdis_data_create
    (dev, sizeof(double), ALIGNOF(double), NULL, &data));
  shader.temperature = fluid_get_temperature;
  *((double*)sdis_data_get(data)) = temperature;
  OK(sdis_fluid_create(dev, &shader, data, &fluid));
  OK(sdis_data_ref_put(data));
  return fluid;
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  (void)frag, (void)data;
  return CONVECTION_COEF;
}

static struct sdis_interface*
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back)
{
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* interf = NULL;

  shader.convection_coef = interface_get_convection_coef;
  shader.convection_coef_upper_bound = CONVECTION_COEF;
  OK(sdis_interface_create(dev, front, back, &shader, NULL, &interf));
  return interf;
}

/*******************************************************************************
 * Scene
 ******************************************************************************/
static void
get_position(const size_t ivert, double pos[2], void* context)
{
  (void)context;
  if(ivert < square_nvertices) {
    /* Hole */
    pos[0] = square_vertices[ivert*2 + 0]*0.5 + 0.25;
    pos[1] = square_vertices[ivert*2 + 1]*0.5 + 0.25;
  } else {
    /* Border */
    pos[0] = square_vertices[(ivert-square_nvertices)*2 + 0];
    pos[1] = square_vertices[(ivert-square_nvertices)*2 + 1];
  }
}

static void
get_indices(const size_t iseg, size_t ids[2], void* context)
{
  (void)context;
  if(iseg < square_nsegments) {
    /* Hole */
    ids[0] = square_indices[iseg*2 + 0];
    ids[1] = square_indices[iseg*2 + 1];
  } else {
    /* Border */
    ids[0] = square_indices[(iseg-square_nsegments)*2 + 0] + square_nvertices;
    ids[1] = square_indices[(iseg-square_nsegments)*2 + 1] + square_nvertices;
  }
}

static void
get_interface(const size_t iseg, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfs = context;
  (void)context;
  if(iseg < square_nsegments) {
    *bound = interfs[iseg]; /* Hole */
  } else {
    *bound = interfs[4]; /* Border */
  }
}

static struct sdis_scene*
create_scene_2d(struct sdis_device* dev)
{
  struct sdis_scene_create_args args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_interface* interfaces[5] = {NULL};
  struct sdis_medium* fluid[5] = {NULL};
  struct sdis_medium* solid = NULL;
  struct sdis_scene* scn = NULL;

  fluid[0] = create_fluid(dev, 280);
  fluid[1] = create_fluid(dev, 300);
  fluid[2] = create_fluid(dev, 320);
  fluid[3] = create_fluid(dev, 340);
  fluid[4] = create_fluid(dev, Text);
  solid = create_solid(dev);

  interfaces[0] = create_interface(dev, fluid[0], solid);
  interfaces[1] = create_interface(dev, fluid[1], solid);
  interfaces[2] = create_interface(dev, fluid[2], solid);
  interfaces[3] = create_interface(dev, fluid[3], solid);
  interfaces[4] = create_interface(dev, solid, fluid[4]);

  args.get_indices = get_indices;
  args.get_interface = get_interface;
  args.get_position = get_position;
  args.nprimitives = square_nsegments*2/*border + hole*/;
  args.nvertices = square_nvertices*2/*border + hole*/;
  args.context = interfaces;
  OK(sdis_scene_2d_create(dev, &args, &scn));

  OK(sdis_interface_ref_put(interfaces[0]));
  OK(sdis_interface_ref_put(interfaces[1]));
  OK(sdis_interface_ref_put(interfaces[2]));
  OK(sdis_interface_ref_put(interfaces[3]));
  OK(sdis_interface_ref_put(interfaces[4]));
  OK(sdis_medium_ref_put(fluid[0]));
  OK(sdis_medium_ref_put(fluid[1]));
  OK(sdis_medium_ref_put(fluid[2]));
  OK(sdis_medium_ref_put(fluid[3]));
  OK(sdis_medium_ref_put(fluid[4]));
  OK(sdis_medium_ref_put(solid));
  return scn;
}

/*******************************************************************************
 * Check
 ******************************************************************************/
static void
solve_probe(struct sdis_scene* scn)
{
  struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;

  args.position[0] = 0.5;
  args.position[1] = 0.5;
  args.position[2] = 0;
  args.nrealisations = NREALISATIONS;
  CHK(sdis_solve_probe(scn, &args, &estimator) == RES_BAD_OP);

  args.position[0] = 2;
  args.position[1] = 0.5;
  args.position[2] = 0;
  args.nrealisations = NREALISATIONS;
  OK(sdis_solve_probe(scn, &args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_ref_put(estimator));
  CHK(Text == T.E); /* Zero variance */

  args.position[0] = 0.1;
  args.position[1] = 0.1;
  args.position[2] = 0;
  args.nrealisations = NREALISATIONS;
  OK(sdis_solve_probe(scn, &args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &T));
  OK(sdis_estimator_ref_put(estimator));
  printf("T(%g, %g) ~ %g +/- %g\n", SPLIT2(args.position), T.E, T.SE);
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* dev = NULL;
  struct sdis_scene* scn_2d = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));
  scn_2d = create_scene_2d(dev);

  solve_probe(scn_2d);

  OK(sdis_device_ref_put(dev));
  OK(sdis_scene_ref_put(scn_2d));

  CHK(mem_allocated_size() == 0);
  return 0;
}
