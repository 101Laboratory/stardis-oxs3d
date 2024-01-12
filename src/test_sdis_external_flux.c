/* Copyright (C) 2016-2023 |Méso|Star> (contact@meso-star.com)
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

/*
 * The system consists of 2 parallelepipeds: a vertical one called the wall, and
 * a horizontal one representing the floor. The wall is a black body, while the
 * floor is a perfectly reflective surface. The surrounding fluid has a fixed
 * temperature and, finally, an external spherical source represents the sun.
 * This test calculates the steady temperature at a position in the wall and
 * compares it with the analytical solution given for a perfectly diffuse or
 * specular ground.
 *
 *  (-0.1,1500)
 *         +---+                                  External source
 *         |  E=1          T_FLUID                      ##
 *     Probe x |             _\                        ####
 *         |   |            / /                         ##
 *         +---+            \__/
 *            (0,500)
 *
 *   Z        (0,0)
 *   | Y       *--------E=0------------- - - -
 *   |/        |
 *   o---X     *------------------------ - - -
 *            (0,-1)
 *
 */

#define T_FLUID 300.0 /* [K] */
#define T_REF 0 /* [K] */

/*******************************************************************************
 * Geometries
 ******************************************************************************/
static const double positions[] = {
  /* Ground */
  0.0,    -1e6, -1.0,
  1.0e12, -1e6, -1.0,
  0.0,     1e6, -1.0,
  1.0e12,  1e6, -1.0,
  0.0,    -1e6,  0.0,
  1.0e12, -1e6,  0.0,
  0.0,     1e6,  0.0,
  1.0e12,  1e6,  0.0,

  /* Wall */
  -0.1, -500.0,  500.0,
   0.0, -500.0,  500.0,
  -0.1,  500.0,  500.0,
   0.0,  500.0,  500.0,
  -0.1, -500.0, 1500.0,
   0.0, -500.0, 1500.0,
  -0.1,  500.0, 1500.0,
   0.0,  500.0, 1500.0
};
const size_t nvertices = sizeof(positions) / (sizeof(double)*3);

static const size_t indices[] = {
  /* Ground */
  0, 2, 1, 1, 2, 3, /* -z */
  0, 4, 2, 2, 4, 6, /* -x */
  4, 5, 6, 6, 5, 7, /* +z */
  3, 7, 5, 5, 1, 3, /* +x */
  2, 6, 7, 7, 3, 2, /* +y */
  0, 1, 5, 5, 4, 0, /* -y */

  /* Wall */
  8,  10, 9,  9,  10, 11, /* -z */
  8,  12, 10, 10, 12, 14, /* -x */
  12, 13, 14, 14, 13, 15, /* +z */
  11, 15, 13, 13, 9,  11, /* +x */
  10, 14, 15, 15, 11, 10, /* +y */
  8,  9,  13, 13, 12, 8   /* -y */
};
const size_t ntriangles = sizeof(indices) / (sizeof(size_t)*3);

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
MEDIUM_PROP(medium, volumic_mass, 1700) /* [kj/m^3] */
MEDIUM_PROP(medium, calorific_capacity, 800) /* [J/K/Kg] */
MEDIUM_PROP(solid, thermal_conductivity, 1.15) /* [W/m/K] */
MEDIUM_PROP(solid, delta, 0.1/20.0) /* [m] */
MEDIUM_PROP(solid, temperature, -1/*<=> unknown*/) /* [K] */
MEDIUM_PROP(fluid, temperature, T_FLUID) /* [K] */
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
  shader.temperature = solid_get_temperature;
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
  shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(sdis, &shader, NULL, &fluid));
  return fluid;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interface {
  double emissivity;
  double convection_coef; /* [W/m^2/K] */
};

#define INTERF_PROP(Prop, Val)                                                 \
  static double                                                                \
  interface_get_##Prop                                                         \
    (const struct sdis_interface_fragment* frag,                               \
     struct sdis_data* data)                                                   \
  {                                                                            \
    (void)frag, (void)data; /* Avoid the "unused variable" warning */          \
    return Val;                                                                \
  }
INTERF_PROP(temperature, -1/*<=> unknown*/) /* [K] */
INTERF_PROP(reference_temperature, T_REF) /* [K] */
#undef INTERF_PROP

#define INTERF_PROP(Prop)                                                      \
  static double                                                                \
  interface_get_##Prop                                                         \
    (const struct sdis_interface_fragment* frag,                               \
     struct sdis_data* data)                                                   \
  {                                                                            \
    struct interface* interf_data = NULL;                                      \
    (void)frag; /* Avoid the "unused variable" warning */                      \
    interf_data = sdis_data_get(data);                                         \
    return interf_data->Prop;                                                  \
  }
INTERF_PROP(emissivity)
INTERF_PROP(convection_coef) /* [W/m^2/K] */
#undef INTERF_PROP

static struct sdis_interface*
create_interface
  (struct sdis_device* sdis,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const double emissivity,
   const double convection_coef)
{
  struct sdis_data* data = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;
  struct interface* interf_data = NULL;

  OK(sdis_data_create
    (sdis, sizeof(struct interface), ALIGNOF(struct interface), NULL, &data));
  interf_data = sdis_data_get(data);
  interf_data->emissivity = emissivity;
  interf_data->convection_coef = convection_coef; /* [W/m^2/K] */

  shader.front.temperature = interface_get_temperature;
  shader.back.temperature = interface_get_temperature;
  shader.back.reference_temperature = interface_get_reference_temperature;
  shader.back.emissivity = interface_get_emissivity;
  shader.convection_coef = interface_get_convection_coef;
  shader.convection_coef_upper_bound = convection_coef;
  OK(sdis_interface_create(sdis, front, back, &shader, data, &interf));
  OK(sdis_data_ref_put(data));

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
  const double elevation = MDEG2RAD(30); /* [radian] */
  const double distance = 1.5e11; /* [m] */
  (void)time, (void)data; /* Avoid the "unusued variable" warning */

  pos[0] = cos(elevation) * distance;
  pos[1] = 0;
  pos[2] = sin(elevation) * distance;
}

static struct sdis_source*
create_source(struct sdis_device* sdis)
{
  struct sdis_spherical_source_create_args args = SDIS_SPHERICAL_SOURCE_CREATE_ARGS_NULL;
  struct sdis_source* src = NULL;

  args.position = source_get_position;
  args.data = NULL;
  args.radius = 6.5991756e8; /* [m] */
  args.power = 3.845e26; /* [W] */
  OK(sdis_spherical_source_create(sdis, &args, &src));
  return src;
}

/*******************************************************************************
 * Scene
 ******************************************************************************/
struct scene_context {
  struct sdis_interface* interf_ground;
  struct sdis_interface* interf_wall;
};
static const struct scene_context SCENE_CONTEXT_NULL = {NULL, NULL};

static void
scene_get_indices(const size_t itri, size_t ids[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(ids && context && itri < ntriangles);
  ids[0] = (unsigned)indices[itri*3+0];
  ids[1] = (unsigned)indices[itri*3+1];
  ids[2] = (unsigned)indices[itri*3+2];
}

static void
scene_get_interface(const size_t itri, struct sdis_interface** interf, void* ctx)
{
  struct scene_context* context = ctx;
  CHK(interf && context && itri < ntriangles);
  if(itri < 12) {
    *interf = context->interf_ground;
  } else {
    *interf = context->interf_wall;
  }
}

static void
scene_get_position(const size_t ivert, double pos[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(pos && context && ivert < nvertices);
  pos[0] = positions[ivert*3+0];
  pos[1] = positions[ivert*3+1];
  pos[2] = positions[ivert*3+2];
}

static struct sdis_scene*
create_scene
  (struct sdis_device* sdis,
   struct sdis_interface* interf_ground,
   struct sdis_interface* interf_wall,
   struct sdis_source* source)
{
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct scene_context context = SCENE_CONTEXT_NULL;

  context.interf_ground = interf_ground;
  context.interf_wall = interf_wall;

  scn_args.get_indices = scene_get_indices;
  scn_args.get_interface = scene_get_interface;
  scn_args.get_position = scene_get_position;
  scn_args.nprimitives = ntriangles;
  scn_args.nvertices = nvertices;
  scn_args.trad.temperature = 0; /* [K] */
  scn_args.trad.reference = T_REF; /* [K] */
  scn_args.t_range[0] = 0; /* [K] */
  scn_args.t_range[1] = 0; /* [K] */
  scn_args.source = source;
  scn_args.context = &context;
  OK(sdis_scene_create(sdis, &scn_args, &scn));
  return scn;
}

/*******************************************************************************
 * Validations
 ******************************************************************************/
static void
check_diffuse(struct sdis_scene* scn)
{
  struct sdis_solve_probe_args probe_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  const double ref = 375.88; /* Analytical solution [K] */

  probe_args.nrealisations = 10000;
  probe_args.position[0] = -0.05;
  probe_args.position[1] = 0;
  probe_args.position[2] = 1000;
  OK(sdis_solve_probe(scn, &probe_args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &T));
  printf("T(%g, %g, %g) = %g ~ %g +/- %g\n",
    SPLIT3(probe_args.position), ref, T.E, T.SE);
  OK(sdis_estimator_ref_put(estimator));

  CHK(eq_eps(ref, T.E, 3*T.SE));
}

/* Pour le spéculaire : ref = 417.77 K */

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_interface* interf_ground = NULL;
  struct sdis_interface* interf_wall = NULL;
  struct sdis_source* src = NULL;
  struct sdis_scene* scn = NULL;
  (void) argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  fluid = create_fluid(dev);
  solid = create_solid(dev);
  interf_ground = create_interface(dev, solid, fluid, 0/*emissivity*/, 0/*h*/);
  interf_wall = create_interface(dev, solid, fluid, 1/*emissivity*/, 10/*h*/);
  src = create_source(dev);
  scn = create_scene(dev, interf_ground, interf_wall, src);

  check_diffuse(scn);

  OK(sdis_device_ref_put(dev));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_interface_ref_put(interf_ground));
  OK(sdis_interface_ref_put(interf_wall));
  OK(sdis_source_ref_put(src));
  OK(sdis_scene_ref_put(scn));
  CHK(mem_allocated_size() == 0);
  return 0;
}
