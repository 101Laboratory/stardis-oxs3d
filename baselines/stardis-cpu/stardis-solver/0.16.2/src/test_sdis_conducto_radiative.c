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

#include <rsys_math.h>
#include <star/ssp.h>

/* The scene is composed of a solid cube whose temperature is unknown. The cube
 * faces on +/-X are in contact with a fluid and their convection coefficient
 * is null while their emissivity is 1. The left and right fluids are enclosed
 * by surfaces whose emissivity are null excepted for the faces orthogonal to
 * the X axis that are fully emissive and whose temperature is known. The
 * medium that surrounds the solid cube and the 2 fluids is a solid with a null
 * conductivity.
 *
 *    Y                          (1, 1, 1)
 *    |            +------+----------+------+ (1.5,1,1)
 *    o--- X      /'     /##########/'     /|
 *   /           +------+----------+------+ |
 *  Z            | '    |##########|*'    | | 310K
 *               | '    |##########|*'    | |
 *          300K | ' E=1|##########|*'E=1 | |
 *               | +....|##########|*+....|.+
 *               |/     |##########|/     |/
 *  (-1.5,-1,-1) +------+----------+------+
 *                  (-1,-1,-1)
 */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct geometry {
  const double* positions;
  const size_t* indices;
  struct sdis_interface** interfaces;
};

static const double vertices[16/*#vertices*/*3/*#coords per vertex*/] = {
  -1.0,-1.0,-1.0,
   1.0,-1.0,-1.0,
  -1.0, 1.0,-1.0,
   1.0, 1.0,-1.0,
  -1.0,-1.0, 1.0,
   1.0,-1.0, 1.0,
  -1.0, 1.0, 1.0,
   1.0, 1.0, 1.0,
  -1.5,-1.0,-1.0,
   1.5,-1.0,-1.0,
  -1.5, 1.0,-1.0,
   1.5, 1.0,-1.0,
  -1.5,-1.0, 1.0,
   1.5,-1.0, 1.0,
  -1.5, 1.0, 1.0,
   1.5, 1.0, 1.0,
};
static const size_t nvertices = sizeof(vertices) / (sizeof(double)*3);

static const size_t indices[32/*#triangles*/*3/*#indices per triangle*/] = {
  0, 2, 1, 1, 2, 3, /* Solid back face */
  0, 4, 2, 2, 4, 6, /* Solid left face*/
  4, 5, 6, 6, 5, 7, /* Solid front face */
  3, 7, 1, 1, 7, 5, /* Solid right face */
  2, 6, 3, 3, 6, 7, /* Solid top face */
  0, 1, 4, 4, 1, 5,  /* Solid bottom face */

  8, 10, 0, 0, 10, 2, /* Left fluid back face */
  8, 12, 10, 10, 12, 14, /* Left fluid left face */
  12, 4, 14, 14, 4, 6, /* Left fluid front face */
  10, 14, 2, 2, 14, 6, /* Left fluid top face */
  8, 0, 12, 12, 0, 4, /* Left fluid bottom face */

  1, 3, 9, 9, 3, 11, /* Right fluid back face */
  5, 13, 7, 7, 13, 15, /* Right fluid front face */
  11, 15, 9, 9, 15, 13, /* Right fluid right face */
  3, 7, 11, 11, 7, 15, /* Right fluid top face */
  1, 9, 5, 5, 9, 13 /* Right fluid bottom face */
};
static const size_t ntriangles = sizeof(indices) / (sizeof(size_t)*3);

static void
get_indices(const size_t itri, size_t ids[3], void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  ids[0] = geom->indices[itri*3+0];
  ids[1] = geom->indices[itri*3+1];
  ids[2] = geom->indices[itri*3+2];
}

static void
get_position(const size_t ivert, double pos[3], void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  pos[0] = geom->positions[ivert*3+0];
  pos[1] = geom->positions[ivert*3+1];
  pos[2] = geom->positions[ivert*3+2];
}

static void
get_interface(const size_t itri, struct sdis_interface** bound, void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  *bound = geom->interfaces[itri];
}

/*******************************************************************************
 * Media
 ******************************************************************************/
struct solid {
  double lambda;
  double initial_temperature;
  double t0;
};

static double
temperature_unknown(const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return SDIS_TEMPERATURE_NONE;
}

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return 1;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL);
  CHK(data != NULL);
  return ((const struct solid*)sdis_data_cget(data))->lambda;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return 1;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return 1.0/10.0;
}

static double
solid_get_temperature
(const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  double t0;
  CHK(vtx != NULL);
  CHK(data != NULL);
  t0 = ((const struct solid*)sdis_data_cget(data))->t0;
  if(vtx->time > t0) {
    return SDIS_TEMPERATURE_NONE;
  } else {
    return ((const struct solid*)sdis_data_cget(data))->initial_temperature;
  }
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
struct interfac {
  double temperature;
  double convection_coef;
  double emissivity;
  double specular_fraction;
  double Tref;
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interfac*)sdis_data_cget(data))->temperature;
}

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interfac*)sdis_data_cget(data))->convection_coef;
}

static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  (void)source_id;
  CHK(data != NULL && frag != NULL);
  return ((const struct interfac*)sdis_data_cget(data))->emissivity;
}

static double
interface_get_specular_fraction
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  (void)source_id;
  CHK(data != NULL && frag != NULL);
  return ((const struct interfac*)sdis_data_cget(data))->specular_fraction;
}

static double
interface_get_Tref
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interfac*)sdis_data_cget(data))->Tref;
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const struct interfac* interf,
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
  shader.convection_coef_upper_bound = MMAX(0, interf->convection_coef);

  OK(sdis_data_create(dev, sizeof(struct interfac), ALIGNOF(struct interfac),
    NULL, &data));
  *((struct interfac*)sdis_data_get(data)) = *interf;

  OK(sdis_interface_create(dev, front, back, &shader, data, out_interf));
  OK(sdis_data_ref_put(data));
}

/*******************************************************************************
 * Test that the evaluation of the green function failed with a picard order
 * greater than 1, i.e. when one want to handle the non-linearties of the
 * system.
 ******************************************************************************/
static void
test_invalidity_picardN_green
  (struct sdis_scene* scn,
   struct sdis_medium* solid)
{
  struct sdis_solve_probe_args probe = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_solve_boundary_args bound = SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT;
  struct sdis_solve_medium_args mdm = SDIS_SOLVE_MEDIUM_ARGS_DEFAULT;
  struct sdis_solve_probe_boundary_args probe_bound =
    SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT;

  struct sdis_green_function* green = NULL;
  CHK(scn);

  CHK(probe.picard_order == 1);
  CHK(probe_bound.picard_order == 1);
  CHK(bound.picard_order == 1);
  CHK(mdm.picard_order == 1);

  probe.position[0] = 0;
  probe.position[1] = 0;
  probe.position[2] = 0;
  probe.picard_order = 2;
  BA(sdis_solve_probe_green_function(scn, &probe, &green));

  probe_bound.iprim = 2; /* Solid left */
  probe_bound.uv[0] = 0.3;
  probe_bound.uv[1] = 0.3;
  probe_bound.side = SDIS_FRONT;
  probe_bound.picard_order = 2;
  BA(sdis_solve_probe_boundary_green_function(scn, &probe_bound, &green));

  bound.primitives = &probe_bound.iprim;
  bound.sides = &probe_bound.side;
  bound.nprimitives = 1;
  bound.picard_order = 2;
  BA(sdis_solve_boundary_green_function(scn, &bound, &green));

  mdm.medium = solid;
  mdm.picard_order = 2;
  BA(sdis_solve_medium_green_function(scn, &mdm, &green));
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct interfac interf;
  struct geometry geom;
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* solid2 = NULL;
  struct sdis_interface* interfaces[5] = {NULL};
  struct sdis_interface* prim_interfaces[32/*#triangles*/];
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_device_create_args dev_args = SDIS_DEVICE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_scene* scn = NULL;
  struct ssp_rng* rng = NULL;
  const int nsimuls = 4;
  int isimul;
  const double emissivity = 1;/* Emissivity of the side +/-X of the solid */
  const double lambda = 0.1; /* Conductivity of the solid */
  const double Tref = 300; /* Reference temperature */
  const double T0 = 300; /* Fixed temperature on the left side of the system */
  const double T1 = 310; /* Fixed temperature on the right side of the system */
  const double thickness = 2.0; /* Thickness of the solid along X */
  double t_range[2];
  double Ts0, Ts1, hr, tmp;
  struct interfac* p_intface;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  dev_args.allocator = &allocator;
  OK(sdis_device_create(&dev_args, &dev));

  /* Create the fluid medium */
  fluid_shader.temperature = temperature_unknown;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Create the solid medium */
  OK(sdis_data_create(dev, sizeof(struct solid), ALIGNOF(struct solid),
    NULL, &data));
  ((struct solid*)sdis_data_get(data))->lambda = lambda;
  ((struct solid*)sdis_data_get(data))->t0 = 0;
  ((struct solid*)sdis_data_get(data))->initial_temperature = (T0 + T1) / 2;
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Create the surrounding solid medium */
  OK(sdis_data_create(dev, sizeof(struct solid), ALIGNOF(struct solid),
    NULL, &data));
  ((struct solid*)sdis_data_get(data))->lambda = 0;
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = temperature_unknown;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid2));
  OK(sdis_data_ref_put(data));

  /* Create the interface that forces to keep in conduction */
  interf.temperature = SDIS_TEMPERATURE_NONE;
  interf.convection_coef = -1;
  interf.emissivity = -1;
  interf.specular_fraction = -1;
  interf.Tref = Tref;
  create_interface(dev, solid, solid2, &interf, interfaces+0);

  /* Create the interface that emits radiative heat from the solid */
  interf.temperature = SDIS_TEMPERATURE_NONE;
  interf.convection_coef = 0;
  interf.emissivity = emissivity;
  interf.specular_fraction = 1;
  interf.Tref = Tref;
  create_interface(dev, solid, fluid, &interf, interfaces+1);

  /* Create the interface that forces the radiative heat to bounce */
  interf.temperature = SDIS_TEMPERATURE_NONE;
  interf.convection_coef = 0;
  interf.emissivity = 0;
  interf.specular_fraction = 1;
  interf.Tref = Tref;
  create_interface(dev, fluid, solid2, &interf, interfaces+2);

  /* Create the interface with a limit condition of T0 Kelvin */
  interf.temperature = T0;
  interf.convection_coef = 0;
  interf.emissivity = 1;
  interf.specular_fraction = 1;
  interf.Tref = T0;
  create_interface(dev, fluid, solid2, &interf, interfaces+3);

  /* Create the interface with a limit condition of T1 Kelvin */
  interf.temperature = T1;
  interf.convection_coef = 0;
  interf.emissivity = 1;
  interf.specular_fraction = 1;
  interf.Tref = T1;
  create_interface(dev, fluid, solid2, &interf, interfaces+4);

  /* Setup the per primitive interface of the solid medium */
  prim_interfaces[0] = prim_interfaces[1] = interfaces[0];
  prim_interfaces[2] = prim_interfaces[3] = interfaces[1];
  prim_interfaces[4] = prim_interfaces[5] = interfaces[0];
  prim_interfaces[6] = prim_interfaces[7] = interfaces[1];
  prim_interfaces[8] = prim_interfaces[9] = interfaces[0];
  prim_interfaces[10] = prim_interfaces[11] = interfaces[0];

  /* Setup the per primitive interface of the fluid on the left of the medium */
  prim_interfaces[12] = prim_interfaces[13] = interfaces[2];
  prim_interfaces[14] = prim_interfaces[15] = interfaces[3];
  prim_interfaces[16] = prim_interfaces[17] = interfaces[2];
  prim_interfaces[18] = prim_interfaces[19] = interfaces[2];
  prim_interfaces[20] = prim_interfaces[21] = interfaces[2];

  /* Setup the per primitive interface of the fluid on the right of the medium */
  prim_interfaces[22] = prim_interfaces[23] = interfaces[2];
  prim_interfaces[24] = prim_interfaces[25] = interfaces[2];
  prim_interfaces[26] = prim_interfaces[27] = interfaces[4];
  prim_interfaces[28] = prim_interfaces[29] = interfaces[2];
  prim_interfaces[30] = prim_interfaces[31] = interfaces[2];

  /* Create the scene */
  geom.positions = vertices;
  geom.indices = indices;
  geom.interfaces = prim_interfaces;
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = ntriangles;
  scn_args.nvertices = nvertices;
  scn_args.t_range[0] = MMIN(T0, T1);
  scn_args.t_range[1] = MMAX(T0, T1);
  scn_args.context = &geom;
  OK(sdis_scene_create(dev, &scn_args, &scn));

  hr = 4.0 * BOLTZMANN_CONSTANT * Tref*Tref*Tref * emissivity;

  /* Run the simulations */
  p_intface
    = (struct interfac*)sdis_data_get(sdis_interface_get_data(interfaces[4]));
  OK(ssp_rng_create(&allocator, SSP_RNG_KISS, &rng));
  FOR_EACH(isimul, 0, nsimuls) {
    struct sdis_mc T = SDIS_MC_NULL;
    struct sdis_mc time = SDIS_MC_NULL;
    struct sdis_estimator* estimator;
    struct sdis_estimator* estimator2;
    struct sdis_green_function* green;
    struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    double ref = SDIS_TEMPERATURE_NONE;
    size_t nreals = 0;
    size_t nfails = 0;
    const size_t N = 10000;
    double T1b;
    int steady = (isimul % 2) == 0;

    /* Reset temperature */
    p_intface->temperature = T1;

    solve_args.nrealisations = N;
    if(steady) {
      solve_args.time_range[0] = solve_args.time_range[1] = INF;
    } else {
      solve_args.time_range[0] = 100 * (double)isimul;
      solve_args.time_range[1] = 4 * solve_args.time_range[0];
    }
    solve_args.position[0] = ssp_rng_uniform_double(rng, -0.9, 0.9);
    solve_args.position[1] = ssp_rng_uniform_double(rng, -0.9, 0.9);
    solve_args.position[2] = ssp_rng_uniform_double(rng, -0.9, 0.9);

    OK(sdis_solve_probe(scn, &solve_args, &estimator));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    tmp = lambda / (2 * lambda + thickness * hr) * (T1 - T0);
    Ts0 = T0 + tmp;
    Ts1 = T1 - tmp;
    if(steady) {
      double u = (solve_args.position[0] + 1) / thickness;
      ref = u * Ts1 + (1 - u) * Ts0;
      printf("Steady temperature at (%g, %g, %g) with T1=%g = %g ~ %g +/- %g\n",
        SPLIT3(solve_args.position), p_intface->temperature, ref, T.E, T.SE);
    } else {
      printf("Mean temperature at (%g, %g, %g) with t in [%g %g]"
        " and T1=%g ~ %g +/- %g\n",
        SPLIT3(solve_args.position), SPLIT2(solve_args.time_range),
        p_intface->temperature, T.E, T.SE);
    }
    printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

    CHK(nfails + nreals == N);
    CHK(nfails <= N/1000);
    if(steady) CHK(eq_eps(T.E, ref, 3*T.SE) == 1);

    /* Check green function */
    OK(sdis_solve_probe_green_function(scn, &solve_args, &green));
    OK(sdis_green_function_solve(green, &estimator2));
    check_green_function(green);
    check_estimator_eq(estimator, estimator2);
    check_green_serialization(green, scn);

    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
    printf("\n");

    /* Check same green used at a different temperature */
    p_intface->temperature = T1b = T1 + ((double)isimul + 1) * 10;
    t_range[0] = MMIN(T0, T1b);
    t_range[1] = MMAX(T0, T1b);
    OK(sdis_scene_set_temperature_range(scn, t_range));

    OK(sdis_solve_probe(scn, &solve_args, &estimator));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    tmp = lambda / (2 * lambda + thickness * hr) * (T1b - T0);
    Ts0 = T0 + tmp;
    Ts1 = T1b - tmp;

    if(steady) {
      double u = (solve_args.position[0] + 1) / thickness;
      ref = u * Ts1 + (1 - u) * Ts0;
      printf("Steady temperature at (%g, %g, %g) with T1=%g = %g ~ %g +/- %g\n",
        SPLIT3(solve_args.position), p_intface->temperature, ref, T.E, T.SE);
    } else {
      printf("Mean temperature at (%g, %g, %g) with t in [%g %g]"
        " and T1=%g ~ %g +/- %g\n",
        SPLIT3(solve_args.position), SPLIT2(solve_args.time_range),
        p_intface->temperature, T.E, T.SE);
    }
    printf("Time per realisation (in usec) = %g +/- %g\n", time.E, time.SE);
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

    CHK(nfails + nreals == N);
    CHK(nfails <= N/1000);
    if(steady) CHK(eq_eps(T.E, ref, 3*T.SE) == 1);

    OK(sdis_green_function_solve(green, &estimator2));
    check_green_function(green);
    check_estimator_eq(estimator, estimator2);

    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
    OK(sdis_green_function_ref_put(green));

    solve_args.nrealisations = 10;
    solve_args.register_paths = SDIS_HEAT_PATH_ALL;

    OK(sdis_solve_probe(scn, &solve_args, &estimator));
    OK(sdis_estimator_ref_put(estimator));

    printf("\n\n");
  }

  test_invalidity_picardN_green(scn, solid);

  /* Release memory */
  OK(sdis_scene_ref_put(scn));
  OK(sdis_interface_ref_put(interfaces[0]));
  OK(sdis_interface_ref_put(interfaces[1]));
  OK(sdis_interface_ref_put(interfaces[2]));
  OK(sdis_interface_ref_put(interfaces[3]));
  OK(sdis_interface_ref_put(interfaces[4]));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(solid2));
  OK(sdis_device_ref_put(dev));
  OK(ssp_rng_ref_put(rng));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
