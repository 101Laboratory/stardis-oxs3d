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

#include <rsys/math.h>
#include <star/ssp.h>

#define UNKNOWN_TEMPERATURE -1

/* The scene is composed of a solid cube whose temperature is unknown. The cube
 * face on +/-X are in contact with a fluid and their convection coefficient is
 * null while their emissivity is 1. The left and right fluids are enclosed by
 * surfaces whose emissivity are null excepted for the faces orthogonal to the
 * X axis that are fully emissive and whose temperature is known. The medium
 * that surrounds the solid cube and the 2 fluids is a solid with a null
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
static const size_t nvertices = sizeof(vertices) / sizeof(double[3]);

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
static const size_t ntriangles = sizeof(indices) / sizeof(size_t[3]);

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

static double
solid_get_delta_boundary
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return 2.1/10.0;
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
struct interface {
  double temperature;
  double convection_coef;
  double emissivity;
  double specular_fraction;
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interface*)sdis_data_cget(data))->temperature;
}

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interface*)sdis_data_cget(data))->convection_coef;
}

static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interface*)sdis_data_cget(data))->emissivity;
}

static double
interface_get_specular_fraction
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(data != NULL && frag != NULL);
  return ((const struct interface*)sdis_data_cget(data))->specular_fraction;
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const struct interface* interf,
   struct sdis_interface** out_interf)
{
  struct sdis_interface_shader shader = DUMMY_INTERFACE_SHADER;
  struct sdis_data* data = NULL;

  CHK(interf != NULL);

  shader.temperature = interface_get_temperature;
  shader.convection_coef = interface_get_convection_coef;
  shader.emissivity = interface_get_emissivity;
  shader.specular_fraction = interface_get_specular_fraction;

  CHK(sdis_data_create(dev, sizeof(struct interface), ALIGNOF(struct interface),
    NULL, &data) == RES_OK);
  *((struct interface*)sdis_data_get(data)) = *interf;

  CHK(sdis_interface_create(dev, front, back, &shader, data, out_interf) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct interface interf;
  struct geometry geom;
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* solid2 = NULL;
  struct sdis_interface* interfaces[5] = {NULL};
  struct sdis_interface* prim_interfaces[32/*#triangles*/];
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_scene* scn = NULL;
  struct ssp_rng* rng = NULL;
  const size_t nsimuls = 4;
  size_t isimul;
  const double emissivity = 1;/* Emissivity of the side +/-X of the solid */
  const double lambda = 0.1; /* Conductivity of the solid */
  const double Tref = 300; /* Reference temperature */
  const double T0 = 300; /* Fixed temperature on the left side of the system */
  const double T1 = 310; /* Fixed temperature on the right side of the system */
  const double thickness = 2.0; /* Thickness of the solid along X */
  double Ts0, Ts1, hr, tmp;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev) == RES_OK);

  /* Create the fluid medium */
  fluid_shader.temperature = temperature_unknown;
  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_OK);

  /* Create the solid medium */
  CHK(sdis_data_create(dev, sizeof(struct solid), ALIGNOF(struct solid),
    NULL, &data) == RES_OK);
  ((struct solid*)sdis_data_get(data))->lambda = lambda;
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.delta_boundary = solid_get_delta_boundary;
  solid_shader.temperature = temperature_unknown;
  CHK(sdis_solid_create(dev, &solid_shader, data, &solid) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the surrounding solid medium */
  CHK(sdis_data_create(dev, sizeof(struct solid), ALIGNOF(struct solid),
    NULL, &data) == RES_OK);
  ((struct solid*)sdis_data_get(data))->lambda = 0;
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.delta_boundary = solid_get_delta_boundary;
  solid_shader.temperature = temperature_unknown;
  CHK(sdis_solid_create(dev, &solid_shader, data, &solid2) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the interface that force to keep in conduction */
  interf.temperature = UNKNOWN_TEMPERATURE;
  interf.convection_coef = -1;
  interf.emissivity = -1;
  interf.specular_fraction = -1;
  create_interface(dev, solid, solid2, &interf, interfaces+0);

  /* Create the interface that emits radiative heat from the solid */
  interf.temperature = UNKNOWN_TEMPERATURE;
  interf.convection_coef = 0;
  interf.emissivity = emissivity;
  interf.specular_fraction = 1;
  create_interface(dev, solid, fluid, &interf, interfaces+1);

  /* Create the interface that forces the radiative heat to bounce */
  interf.temperature = UNKNOWN_TEMPERATURE;
  interf.convection_coef = 0;
  interf.emissivity = 0;
  interf.specular_fraction = 1;
  create_interface(dev, fluid, solid2, &interf, interfaces+2);

  /* Create a the interface with a limit condition of 300K */
  interf.temperature = T0;
  interf.convection_coef = 0;
  interf.emissivity = 1;
  interf.specular_fraction = 1;
  create_interface(dev, fluid, solid2, &interf, interfaces+3);

  /* Create a the interface with a limit condition of 310K */
  interf.temperature = T1;
  interf.convection_coef = 0;
  interf.emissivity = 1;
  interf.specular_fraction = 1;
  create_interface(dev, fluid, solid2, &interf, interfaces+4);

  /* Setup the per primitive interface of the solid medium */
  prim_interfaces[0] = prim_interfaces[1] = interfaces[0];
  prim_interfaces[2] = prim_interfaces[3] = interfaces[1];
  prim_interfaces[4] = prim_interfaces[5] = interfaces[0];
  prim_interfaces[6] = prim_interfaces[7] = interfaces[1];
  prim_interfaces[8] = prim_interfaces[9] = interfaces[0];
  prim_interfaces[10] = prim_interfaces[11] = interfaces[0];

  /* Setup the per primitive interfaces of the fluid on the left of the medium */
  prim_interfaces[12] = prim_interfaces[13] = interfaces[2];
  prim_interfaces[14] = prim_interfaces[15] = interfaces[3];
  prim_interfaces[16] = prim_interfaces[17] = interfaces[2];
  prim_interfaces[18] = prim_interfaces[19] = interfaces[2];
  prim_interfaces[20] = prim_interfaces[21] = interfaces[2];

  /* Setup the per primitive interfaces of the fluid on the right of the medium */
  prim_interfaces[22] = prim_interfaces[23] = interfaces[2];
  prim_interfaces[24] = prim_interfaces[25] = interfaces[2];
  prim_interfaces[26] = prim_interfaces[27] = interfaces[4];
  prim_interfaces[28] = prim_interfaces[29] = interfaces[2];
  prim_interfaces[30] = prim_interfaces[31] = interfaces[2];

  /* Create the scene */
  geom.positions = vertices;
  geom.indices = indices;
  geom.interfaces = prim_interfaces;
  CHK(sdis_scene_create(dev, ntriangles, get_indices, get_interface, nvertices,
    get_position, &geom, &scn) == RES_OK);

  hr = 4.0 * BOLTZMANN_CONSTANT * Tref*Tref*Tref * emissivity;
  tmp = lambda/(2*lambda + thickness*hr) * (T1 - T0);
  Ts0 = T0 + tmp;
  Ts1 = T1 - tmp;

  /* Run the simulations */
  ssp_rng_create(&allocator, &ssp_rng_kiss, &rng);
  FOR_EACH(isimul, 0, nsimuls) {
    struct sdis_mc T = SDIS_MC_NULL;
    struct sdis_estimator* estimator;
    double pos[3];
    double ref, u;
    size_t nreals = 0;
    size_t nfails = 0;

    pos[0] = ssp_rng_uniform_double(rng, -0.9, 0.9);
    pos[1] = ssp_rng_uniform_double(rng, -0.9, 0.9);
    pos[2] = ssp_rng_uniform_double(rng, -0.9, 0.9);

    CHK(sdis_solve_probe(scn, 10000, pos, INF, 1, -1, Tref, &estimator) == RES_OK);
    CHK(sdis_estimator_get_realisation_count(estimator, &nreals) == RES_OK);
    CHK(sdis_estimator_get_failure_count(estimator, &nfails) == RES_OK);
    CHK(sdis_estimator_get_temperature(estimator, &T) == RES_OK);

    u = (pos[0] + 1) / thickness;
    ref = u * Ts1 + (1-u) * Ts0;
    printf("Temperature at (%g, %g, %g)  = %g ~ %g +/- %g\n",
      SPLIT3(pos), ref, T.E, T.SE);

    CHK(eq_eps(T.E, ref, 2*T.SE) == 1);

    CHK(sdis_estimator_ref_put(estimator) == RES_OK);
  }

  /* Release memory */
  CHK(sdis_scene_ref_put(scn) == RES_OK);
  CHK(sdis_interface_ref_put(interfaces[0]) == RES_OK);
  CHK(sdis_interface_ref_put(interfaces[1]) == RES_OK);
  CHK(sdis_interface_ref_put(interfaces[2]) == RES_OK);
  CHK(sdis_interface_ref_put(interfaces[3]) == RES_OK);
  CHK(sdis_interface_ref_put(interfaces[4]) == RES_OK);
  CHK(sdis_medium_ref_put(fluid) == RES_OK);
  CHK(sdis_medium_ref_put(solid) == RES_OK);
  CHK(sdis_medium_ref_put(solid2) == RES_OK);
  CHK(sdis_device_ref_put(dev) == RES_OK);
  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
