/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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

/*
 * The scene is composed of a solid cube/square whose temperature is unknown.
 * The convection coefficient with the surrounding fluid is null exepted for
 * the +X face whose value is 'H'. The Temperature of the -X face is fixed to
 * Tb. This test computes the temperature on the +X face and check that it is
 * equal to:
 *
 *    T = (H*Tf + LAMBDA/A * Tb) / (H+LAMBDA/A)
 *
 * with Tf the temperature of the surrounding fluid, lambda the conductivity of
 * the cube and A the size of the cube/square, i.e. 1.
 *
 *          3D                        2D
 *
 *       ///// (1,1,1)             ///// (1,1)
 *       +-------+                 +-------+
 *      /'      /|    _\           |       |    _\
 *     +-------+ |   / /  Tf      Tb       |   / /   Tf
 *    Tb +.....|.+   \__/          |       |   \__/
 *     |,      |/                  +-------+
 *     +-------+                 (0,0) /////
 * (0,0,0) /////
 */

#define UNKNOWN_TEMPERATURE -1
#define N 10000 /* #realisations */

#define Tf 310
#define Tb 300
#define H 0.5
#define LAMBDA 0.1

/*******************************************************************************
 * Geometry 3D
 ******************************************************************************/
static void
box_get_indices(const size_t itri, size_t ids[3], void* context)
{
  (void)context;
  CHK(ids);
  ids[0] = box_indices[itri*3+0];
  ids[1] = box_indices[itri*3+1];
  ids[2] = box_indices[itri*3+2];
}

static void
box_get_position(const size_t ivert, double pos[3], void* context)
{
  (void)context;
  CHK(pos);
  pos[0] = box_vertices[ivert*3+0];
  pos[1] = box_vertices[ivert*3+1];
  pos[2] = box_vertices[ivert*3+2];
}

static void
box_get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  *bound = interfaces[itri];
}

/*******************************************************************************
 * Geometry 2D
 ******************************************************************************/
static void
square_get_indices(const size_t iseg, size_t ids[2], void* context)
{
  (void)context;
  CHK(ids);
  ids[0] = square_indices[iseg*2+0];
  ids[1] = square_indices[iseg*2+1];
}

static void
square_get_position(const size_t ivert, double pos[2], void* context)
{
  (void)context;
  CHK(pos);
  pos[0] = square_vertices[ivert*2+0];
  pos[1] = square_vertices[ivert*2+1];
}

static void
square_get_interface
  (const size_t iseg, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  *bound = interfaces[iseg];
}


/*******************************************************************************
 * Media
 ******************************************************************************/
static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return Tf;
}

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return 2.0;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return LAMBDA;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return 25.0;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return 1.0/20.0;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx != NULL);
  return UNKNOWN_TEMPERATURE;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double temperature;
  double hc;
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
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && data);
  return interf->hc;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_interface* interf_adiabatic = NULL;
  struct sdis_interface* interf_Tb = NULL;
  struct sdis_interface* interf_H = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_scene* square_scn = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* box_interfaces[12 /*#triangles*/];
  struct sdis_interface* square_interfaces[4/*#segments*/];
  struct interf* interf_props = NULL;
  double uv[2];
  double pos[3];
  double ref;
  size_t iprim;
  size_t nreals;
  size_t nfails;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev) == RES_OK);

  /* Create the fluid medium */
  fluid_shader.temperature = fluid_get_temperature;
  CHK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid) == RES_OK);

  /* Create the solid_medium */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta_solid = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  CHK(sdis_solid_create(dev, &solid_shader, NULL, &solid) == RES_OK);

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;
  interf_shader.front.emissivity = NULL;
  interf_shader.front.specular_fraction = NULL;
  interf_shader.back = SDIS_INTERFACE_SIDE_SHADER_NULL;

  /* Create the adiabatic interface */
  CHK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data) == RES_OK);
  interf_props = sdis_data_get(data);
  interf_props->hc = 0;
  interf_props->temperature = UNKNOWN_TEMPERATURE;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_adiabatic) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the Tb interface */
  CHK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data) == RES_OK);
  interf_props = sdis_data_get(data);
  interf_props->hc = 0;
  interf_props->temperature = Tb;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_Tb) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the H interface */
  CHK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data) == RES_OK);
  interf_props = sdis_data_get(data);
  interf_props->hc = H;
  interf_props->temperature = UNKNOWN_TEMPERATURE;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_H) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Release the media */
  CHK(sdis_medium_ref_put(solid) == RES_OK);
  CHK(sdis_medium_ref_put(fluid) == RES_OK);

  /* Map the interfaces to their box triangles */
  box_interfaces[0] = box_interfaces[1] = interf_adiabatic; /* Front */
  box_interfaces[2] = box_interfaces[3] = interf_Tb;        /* Left */
  box_interfaces[4] = box_interfaces[5] = interf_adiabatic; /* Back */
  box_interfaces[6] = box_interfaces[7] = interf_H;         /* Right */
  box_interfaces[8] = box_interfaces[9] = interf_adiabatic; /* Top */
  box_interfaces[10]= box_interfaces[11]= interf_adiabatic; /* Bottom */

  /* Map the interfaces to their square segments */
  square_interfaces[0] = interf_adiabatic; /* Bottom */
  square_interfaces[1] = interf_Tb; /* Lef */
  square_interfaces[2] = interf_adiabatic; /* Top */
  square_interfaces[3] = interf_H; /* Right */

  /* Create the box scene */
  CHK(sdis_scene_create(dev, box_ntriangles, box_get_indices,
    box_get_interface, box_nvertices, box_get_position, box_interfaces,
    &box_scn) == RES_OK);

  /* Create the square scene */
  CHK(sdis_scene_2d_create(dev, square_nsegments, square_get_indices,
    square_get_interface, square_nvertices, square_get_position,
    square_interfaces, &square_scn) == RES_OK);

  /* Release the interfaces */
  CHK(sdis_interface_ref_put(interf_adiabatic) == RES_OK);
  CHK(sdis_interface_ref_put(interf_Tb) == RES_OK);
  CHK(sdis_interface_ref_put(interf_H) == RES_OK);

  uv[0] = 0.3;
  uv[1] = 0.3;
  iprim = 6;

  #define SOLVE sdis_solve_probe_boundary
  #define F SDIS_FRONT
  CHK(SOLVE(NULL, N, iprim, uv, INF, F, 1.0, 0, 0, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, 0, iprim, uv, INF, F, 1.0, 0, 0, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, 12, uv, INF, F, 1.0, 0, 0, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, iprim, NULL, INF, F, 1.0, 0, 0, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, iprim, uv, -1, F, 1.0, 0, 0, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, iprim, uv, INF, -1, 1.0, 0, 0, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, iprim, uv, INF, F, 1.0, 0, 0, NULL) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, iprim, uv, INF, F, 1.0, 0, 0, &estimator) == RES_OK);

  ref = (H*Tf + LAMBDA * Tb) / (H + LAMBDA);

  CHK(sdis_estimator_get_realisation_count(estimator, &nreals) == RES_OK);
  CHK(sdis_estimator_get_failure_count(estimator, &nfails) == RES_OK);
  CHK(sdis_estimator_get_temperature(estimator, &T) == RES_OK);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);
  CHK(sdis_scene_get_boundary_position(box_scn, iprim, uv, pos) == RES_OK);
  printf("Boundary temperature of the box at (%g %g %g) = %g ~ %g +/- %g\n",
    SPLIT3(pos), ref, T.E, T.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, ref, 3*T.SE));

  uv[0] = 0.5;
  iprim = 3;
  CHK(SOLVE(square_scn, N, iprim, uv, INF, F, 1.0, 0, 0, &estimator) == RES_OK);
  CHK(sdis_estimator_get_realisation_count(estimator, &nreals) == RES_OK);
  CHK(sdis_estimator_get_failure_count(estimator, &nfails) == RES_OK);
  CHK(sdis_estimator_get_temperature(estimator, &T) == RES_OK);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);
  CHK(sdis_scene_get_boundary_position(square_scn, iprim, uv, pos) == RES_OK);
  printf("Boundary temperature of the square at (%g %g) = %g ~ %g +/- %g\n",
    SPLIT2(pos), ref, T.E, T.SE);
  printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
  CHK(nfails + nreals == N);
  CHK(nfails < N/1000);
  CHK(eq_eps(T.E, ref, 3*T.SE));
  #undef SOLVE

  CHK(sdis_scene_ref_put(box_scn) == RES_OK);
  CHK(sdis_scene_ref_put(square_scn) == RES_OK);
  CHK(sdis_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

