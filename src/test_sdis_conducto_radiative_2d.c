/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

#define UNKNOWN_TEMPERATURE -1

/* The scene is composed of a solid square whose temperature is unknown. The
 * square segments on +/-X are in contact with a fluid and their convection
 * coefficient is null while their emissivity is 1. The left and right fluids
 * are enclosed by segments whose emissivity are null excepted for the segments
 * orthogonal to the X axis that are fully emissive and whose temperature is
 * known. The medium that surrounds the solid square and the 2 fluids is a
 * solid with a null conductivity.
 *
 *                            (1, 1)
 *            +-----+----------+-----+ (1.5,1,1)
 *            |     |##########|     |
 *            |     |##########|     |
 *       300K | E=1 |##########| E=1 | 310K
 *            |     |##########|     |
 *            |     |##########|     |
 *  (-1.5,-1) +-----+----------+-----+
 *               (-1,-1)
 */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
struct geometry {
  const double* positions;
  const size_t* indices;
  struct sdis_interface** interfaces;
};

static const double vertices[8/*#vertices*/*2/*#coords par vertex*/] = {
   1.0, -1.0,
  -1.0, -1.0,
  -1.0,  1.0,
   1.0,  1.0,
   1.5, -1.0,
  -1.5, -1.0,
  -1.5,  1.0,
   1.5,  1.0
};
static const size_t nvertices = sizeof(vertices) / sizeof(double[2]);

static const size_t indices[10/*#segments*/*2/*#indices per segment*/] = {
  0, 1, /* Solid bottom segment */
  1, 2, /* Solid left segment */
  2, 3, /* Solid top segment */
  3, 0, /* Solid right segment */

  1, 5, /* Left fluid bottom segment */
  5, 6, /* Left fluid left segment */
  6, 2, /* Left fluid top segment */

  4, 0, /* Right fluid bottom segment */
  3, 7, /* Right fluid top segment */
  7, 4 /* Right fluid right segment */
};
static const size_t nsegments = sizeof(indices) / sizeof(size_t[2]);

static void
get_indices(const size_t iseg, size_t ids[2], void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  ids[0] = geom->indices[iseg*2+0];
  ids[1] = geom->indices[iseg*2+1];
}

static void
get_position(const size_t ivert, double pos[2], void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  pos[0] = geom->positions[ivert*2+0];
  pos[1] = geom->positions[ivert*2+1];
}

static void
get_interface(const size_t iseg, struct sdis_interface** bound, void* ctx)
{
  struct geometry* geom = ctx;
  CHK(ctx != NULL);
  *bound = geom->interfaces[iseg];
}

/*******************************************************************************
 * Media
 ******************************************************************************/
struct solid {
  double lambda;
};

static double
temperature_unknown(const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return -1;
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

/*******************************************************************************
 * Interface
 ******************************************************************************/
struct interfac {
  double convection_coef;
  struct {
    double temperature;
    double emissivity;
    double specular_fraction;
  } front, back;
};

static const struct interfac INTERFACE_NULL = {
  0, {-1, -1, -1}, {-1, -1, -1}
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interfac* interf;
  double T = -1;
  CHK(data != NULL && frag != NULL);
  interf = sdis_data_cget(data);
  switch(frag->side) {
    case SDIS_FRONT: T = interf->front.temperature; break;
    case SDIS_BACK: T = interf->back.temperature; break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return T;
}

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interfac* interf;
  CHK(data != NULL && frag != NULL);
  interf = sdis_data_cget(data);
  return interf->convection_coef;
}

static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interfac* interf;
  double e = -1;
  CHK(data != NULL && frag != NULL);
  interf = sdis_data_cget(data);
  switch(frag->side) {
    case SDIS_FRONT: e = interf->front.emissivity; break;
    case SDIS_BACK: e = interf->back.emissivity; break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return e;
}

static double
interface_get_specular_fraction
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interfac* interf;
  double f = -1;
  CHK(data != NULL && frag != NULL);
  interf = sdis_data_cget(data);
  switch(frag->side) {
    case SDIS_FRONT: f = interf->front.specular_fraction; break;
    case SDIS_BACK: f = interf->back.specular_fraction; break;
    default: FATAL("Unreachable code.\n"); break;
  }
  return f;
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
  const enum sdis_medium_type type_f = sdis_medium_get_type(front);
  const enum sdis_medium_type type_b = sdis_medium_get_type(back);

  CHK(interf != NULL);

  shader.back.temperature = interface_get_temperature;
  shader.front.temperature = interface_get_temperature;

  if(type_f != type_b) {
    shader.convection_coef = interface_get_convection_coef;
  }
  if(type_f == SDIS_FLUID) {
    shader.front.emissivity = interface_get_emissivity;
    shader.front.specular_fraction = interface_get_specular_fraction;
  }
  if(type_b == SDIS_FLUID) {
    shader.back.emissivity = interface_get_emissivity;
    shader.back.specular_fraction = interface_get_specular_fraction;
  }
  shader.convection_coef_upper_bound = MMAX(0, interf->convection_coef);

  OK(sdis_data_create(dev, sizeof(struct interfac), ALIGNOF(struct interfac),
    NULL, &data));
  *((struct interfac*)sdis_data_get(data)) = *interf;

  OK(sdis_interface_create(dev, front, back, &shader, data, out_interf));
  OK(sdis_data_ref_put(data));
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
  struct ssp_rng* rng = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* solid2 = NULL;
  struct sdis_interface* interfaces[5]  = {NULL};
  struct sdis_interface* prim_interfaces[10/*#segment*/];
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  const size_t nsimuls = 4;
  size_t isimul;
  const double emissivity = 1;/* Emissivity of the side +/-X of the solid */
  const double lambda = 0.1; /* Conductivity of the solid */
  const double Tref = 300; /* Reference temperature */
  const double T0 = 300; /* Fixed temperature on the left side of the system */
  const double T1  = 310; /* Fixed temperature on the right side of the system */
  const double thickness = 2.0; /* Thickness of the solid along X */
  double Ts0, Ts1, hr, tmp;
  (void)argc, (void)argv;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(sdis_device_create(NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev));

  /* Create the fluid medium */
  fluid_shader.temperature = temperature_unknown;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Create the solid medium */
  OK(sdis_data_create
    (dev, sizeof(struct solid), ALIGNOF(struct solid), NULL, &data));
  ((struct solid*)sdis_data_get(data))->lambda = lambda;
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = temperature_unknown;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Create the surrounding solid medium */
  OK(sdis_data_create(dev, sizeof(struct solid), ALIGNOF(struct solid),
    NULL, &data));
  ((struct solid*)sdis_data_get(data))->lambda = 0;
  solid_shader.calorific_capacity = solid_get_thermal_conductivity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid2));
  OK(sdis_data_ref_put(data));

  /* Create the interface that forces to keep in conduction */
  interf = INTERFACE_NULL;
  create_interface(dev, solid, solid2, &interf, interfaces+0);

  /* Create the interface that emits radiative heat from the solid */
  interf = INTERFACE_NULL;
  interf.back.temperature = UNKNOWN_TEMPERATURE;
  interf.back.emissivity = emissivity;
  interf.back.specular_fraction = -1; /* Should not be fetched */
  create_interface(dev, solid, fluid, &interf, interfaces+1);

  /* Create the interface that forces the radiative heat to bounce */
  interf = INTERFACE_NULL;
  interf.front.temperature = UNKNOWN_TEMPERATURE;
  interf.front.emissivity = 0;
  interf.front.specular_fraction = 1;
  create_interface(dev, fluid, solid2, &interf, interfaces+2);

  /* Create the interface with a limit condition of T0 Kelvin */
  interf = INTERFACE_NULL;
  interf.front.temperature = T0;
  interf.front.emissivity = 1;
  interf.front.specular_fraction = 1;
  create_interface(dev, fluid, solid2, &interf, interfaces+3);

  /* Create the interface with a limit condition of T1 Kelvin  */
  interf = INTERFACE_NULL;
  interf.front.temperature = T1;
  interf.front.emissivity = 1;
  interf.front.specular_fraction = 1;
  create_interface(dev, fluid, solid2, &interf, interfaces+4);

  /* Setup the per primitive interface of the solid medium */
  prim_interfaces[0] = interfaces[0];
  prim_interfaces[1] = interfaces[1];
  prim_interfaces[2] = interfaces[0];
  prim_interfaces[3] = interfaces[1];

  /* Setup the per primitive interface of the fluid on the left of the medium */
  prim_interfaces[4] = interfaces[2];
  prim_interfaces[5] = interfaces[3];
  prim_interfaces[6] = interfaces[2];

  /* Setup the per primitive interface of the fluid on the right of the medium */
  prim_interfaces[7] = interfaces[2];
  prim_interfaces[8] = interfaces[2];
  prim_interfaces[9] = interfaces[4];

  /* Create the scene */
  geom.positions = vertices;
  geom.indices = indices;
  geom.interfaces = prim_interfaces;
  OK(sdis_scene_2d_create(dev, nsegments, get_indices, get_interface, nvertices,
    get_position, &geom, &scn));

  hr = 4*BOLTZMANN_CONSTANT * Tref*Tref*Tref * emissivity;
  tmp = lambda/(2*lambda + thickness*hr) * (T1 - T0);
  Ts0 = T0 + tmp;
  Ts1 = T1 - tmp;

  /* Run the simulations */
  OK(ssp_rng_create(&allocator, &ssp_rng_kiss, &rng));
  FOR_EACH(isimul, 0, nsimuls) {
    struct sdis_mc T = SDIS_MC_NULL;
    struct sdis_estimator* estimator;
    struct sdis_estimator* estimator2;
    struct sdis_green_function* green;
    double pos[2];
    double time_range[2] = { INF, INF };
    double ref, u;
    size_t nreals = 0;
    size_t nfails = 0;
    const size_t N = 10000;

    pos[0] = ssp_rng_uniform_double(rng, -0.9, 0.9);
    pos[1] = ssp_rng_uniform_double(rng, -0.9, 0.9);

    OK(sdis_solve_probe(scn, 10000, pos, time_range, 1, -1, Tref, &estimator));
    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));

    u = (pos[0] + 1) / thickness;
    ref = u * Ts1 + (1-u) * Ts0;
    printf("Temperature at (%g, %g)  = %g ~ %g +/- %g\n",
      SPLIT2(pos), ref, T.E, T.SE);
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);

    CHK(nfails + nreals == N);
    CHK(nfails < N/1000);
    CHK(eq_eps(T.E, ref, 3*T.SE) == 1);

    /* Check green function */
    OK(sdis_solve_probe_green_function(scn, 10000, pos, 1, -1, Tref, &green));
    OK(sdis_green_function_solve(green, time_range, &estimator2));
    check_estimator_eq(estimator, estimator2);

    OK(sdis_estimator_ref_put(estimator));
    OK(sdis_estimator_ref_put(estimator2));
    OK(sdis_green_function_ref_put(green));
  }

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
  OK(ssp_rng_ref_put(rng));
  OK(sdis_device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return 0;
}

