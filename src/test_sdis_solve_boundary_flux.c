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
  * The convection coefficient with the surrounding fluid is null excepted for
  * the X faces whose value is 'H'. The Temperature T of the -X face is fixed
  * to Tb. The ambiant radiative temperature is 0 excepted for the X faces
  * whose value is 'Trad'.
  * This test computes temperature and fluxes on the X faces and check that
  * they are equal to:
  *
  *    T(+X) = (H*Tf + Hrad*Trad + LAMBDA/A * Tb) / (H+Hrad+LAMBDA/A)
  *         with Hrad = 4 * BOLTZMANN_CONSTANT * Tref^3 * epsilon
  *    T(-X) = Tb
  *
  *    CF = H * (T - Tf)
  *    RF = Hrad * (T - Trad)
  *    TF = CF + RF
  *
  * with Tf the temperature of the surrounding fluid, lambda the conductivity of
  * the cube and A the size of the cube/square, i.e. 1.
  *
  *                                    3D
  *
  *                                 ///////(1,1,1)
  *                                +-------+
  *                               /'      /|    _\       <-----
  *         ----->        _\     +-------+ |   / / H,Tf  <----- Trad
  *    Trad ----->  H,Tf / /    Tb +.....|.+   \__/      <-----
  *         ----->       \__/    |,      |/
  *                              +-------+
  *                        (0,0,0)///////
  *
  *
  *                                    2D
  *
  *                                ///////(1,1)
  *                               +-------+
  *          ----->        _\     |       |    _\       <-----
  *     Trad ----->  H,Tf / /    Tb       |   / / H,Tf  <----- Trad
  *          ----->       \__/    |       |   \__/      <-----
  *                               +-------+
  *                           (0,0)///////
  */

#define UNKNOWN_TEMPERATURE -1
#define N 10000 /* #realisations */

#define Tf 300.0
#define Tb 0.0
#define H 0.5
#define Trad 300.0
#define LAMBDA 0.1
#define EPSILON 1.0

#define Tref 300.0
#define Hrad (4 * BOLTZMANN_CONSTANT * Tref * Tref * Tref * EPSILON)

/*******************************************************************************
 * Media
 ******************************************************************************/
struct fluid {
  double temperature;
};

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(data != NULL && vtx != NULL);
  return ((const struct fluid*)sdis_data_cget(data))->temperature;
}


static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void) data;
  CHK(vtx != NULL);
  return 2.0;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void) data;
  CHK(vtx != NULL);
  return LAMBDA;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void) data;
  CHK(vtx != NULL);
  return 25.0;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void) data;
  CHK(vtx != NULL);
  return 1.0 / 20.0;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void) data;
  CHK(vtx != NULL);
  return UNKNOWN_TEMPERATURE;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double temperature;
  double emissivity;
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
interface_get_emissivity
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && data);
  return interf->emissivity;
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
 * Helper function
 ******************************************************************************/
static void
check_estimator
  (const struct sdis_estimator* estimator,
   const size_t nrealisations, /* #realisations */
   const double T,
   const double CF,
   const double RF,
   const double TF)
{
  struct sdis_mc V = SDIS_MC_NULL;
  enum sdis_estimator_type type;
  size_t nreals;
  size_t nfails;
  CHK(estimator && nrealisations);

  CHK(sdis_estimator_get_temperature(estimator, &V) == RES_OK);
  CHK(sdis_estimator_get_realisation_count(estimator, &nreals) == RES_OK);
  CHK(sdis_estimator_get_failure_count(estimator, &nfails) == RES_OK);
  printf("T = %g ~ %g +/- %g\n", T, V.E, V.SE);
  CHK(eq_eps(V.E, T, 3 * (V.SE ? V.SE : FLT_EPSILON)));
  CHK(sdis_estimator_get_type(estimator, &type) == RES_OK);
  if(type == SDIS_FLUX_ESTIMATOR) {
    CHK(sdis_estimator_get_convective_flux(estimator, &V) == RES_OK);
    printf("Convective flux = %g ~ %g +/- %g\n", CF, V.E, V.SE);
    CHK(eq_eps(V.E, CF, 3 * (V.SE ? V.SE : FLT_EPSILON)));
    CHK(sdis_estimator_get_radiative_flux(estimator, &V) == RES_OK);
    printf("Radiative flux = %g ~ %g +/- %g\n", RF, V.E, V.SE);
    CHK(eq_eps(V.E, RF, 3 * (V.SE ? V.SE : FLT_EPSILON)));
    CHK(sdis_estimator_get_total_flux(estimator, &V) == RES_OK);
    printf("Total flux = %g ~ %g +/- %g\n", TF, V.E, V.SE);
    CHK(eq_eps(V.E, TF, 3 * (V.SE ? V.SE : FLT_EPSILON)));
  }
  printf("#failures = %lu/%lu\n",
    (unsigned long) nfails, (unsigned long) nrealisations);
  CHK(nfails + nreals == nrealisations);
  CHK(nfails < N / 1000);
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
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
  struct fluid* fluid_param;
  enum sdis_estimator_type type;
  double uv[2];
  double pos[3];
  double analyticT, analyticCF, analyticRF, analyticTF;
  size_t prims[2];
  size_t iprim;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(sdis_device_create
    (NULL, &allocator, SDIS_NTHREADS_DEFAULT, 1, &dev) == RES_OK);

  /* Create the fluid medium */
  CHK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data) == RES_OK);
  fluid_param = sdis_data_get(data);
  fluid_param->temperature = Tf;
  fluid_shader.temperature = fluid_get_temperature;
  CHK(sdis_fluid_create(dev, &fluid_shader, data, &fluid) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

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
  interf_shader.front.specular_fraction = NULL;
  interf_shader.back = SDIS_INTERFACE_SIDE_SHADER_NULL;

  /* Create the adiabatic interface */
  CHK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data) == RES_OK);
  interf_props = sdis_data_get(data);
  interf_props->hc = 0;
  interf_props->temperature = UNKNOWN_TEMPERATURE;
  interf_props->emissivity = 0;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_adiabatic) == RES_OK);
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the Tb interface */
  CHK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data) == RES_OK);
  interf_props = sdis_data_get(data);
  interf_props->hc = H;
  interf_props->temperature = Tb;
  interf_props->emissivity = EPSILON;
  interf_shader.back.emissivity = interface_get_emissivity;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_Tb) == RES_OK);
  interf_shader.back.emissivity = NULL;
  CHK(sdis_data_ref_put(data) == RES_OK);

  /* Create the H interface */
  CHK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data) == RES_OK);
  interf_props = sdis_data_get(data);
  interf_props->hc = H;
  interf_props->temperature = UNKNOWN_TEMPERATURE;
  interf_props->emissivity = EPSILON;
  interf_shader.back.emissivity = interface_get_emissivity;
  CHK(sdis_interface_create
    (dev, solid, fluid, &interf_shader, data, &interf_H) == RES_OK);
  interf_shader.back.emissivity = NULL;
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
  box_interfaces[10] = box_interfaces[11] = interf_adiabatic; /* Bottom */

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

  analyticT = (H*Tf + Hrad*Trad + LAMBDA * Tb) / (H + Hrad + LAMBDA);
  analyticCF = H * (analyticT - Tf);
  analyticRF = Hrad * (analyticT - Trad);
  analyticTF = analyticCF + analyticRF;

  #define SOLVE sdis_solve_probe_boundary_flux
  uv[0] = 0.3;
  uv[1] = 0.3;
  iprim = 6;

  CHK(SOLVE(NULL, N, iprim, uv, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, 0, iprim, uv, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, 12, uv, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, iprim, NULL, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, iprim, uv, -1, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, iprim, uv, INF, 1.0, Trad, Tref, NULL) == RES_BAD_ARG);

  CHK(SOLVE(box_scn, N, iprim, uv, INF, 1.0, Trad, Tref, &estimator) == RES_OK);
  CHK(sdis_estimator_get_type(estimator, &type) == RES_OK);
  CHK(type == SDIS_FLUX_ESTIMATOR);

  CHK(sdis_scene_get_boundary_position(box_scn, iprim, uv, pos) == RES_OK);
  printf("Boundary values of the box at (%g %g %g) = ", SPLIT3(pos));
  check_estimator(estimator, N, analyticT, analyticCF, analyticRF, analyticTF);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);

  uv[0] = 0.5;
  iprim = 3;
  CHK(SOLVE(square_scn, N, 4, uv, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(square_scn, N, iprim, uv, INF, 1.0, Trad, Tref, &estimator) == RES_OK);
  CHK(sdis_scene_get_boundary_position(square_scn, iprim, uv, pos) == RES_OK);
  printf("Boundary values of the square at (%g %g) = ", SPLIT2(pos));
  check_estimator(estimator, N, analyticT, analyticCF, analyticRF, analyticTF);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);

  #undef F
  #undef SOLVE
  
  #define SOLVE sdis_solve_boundary_flux
  prims[0] = 6;
  prims[1] = 7;
  CHK(SOLVE(NULL, N, prims, 2, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, 0, prims, 2, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, NULL, 2, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, prims, 0, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, prims, 2, -1, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  CHK(SOLVE(box_scn, N, prims, 2, INF, 1.0, Trad, Tref, NULL) == RES_BAD_ARG);

  /* Average temperature on the right side of the box */
  CHK(SOLVE(box_scn, N, prims, 2, INF, 1.0, Trad, Tref, &estimator) == RES_OK);
  printf("Average values of the right side of the box = ");
  check_estimator(estimator, N, analyticT, analyticCF, analyticRF, analyticTF);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);

  /* Average temperature on the right side of the square */
  prims[0] = 3;
  CHK(SOLVE(square_scn, N, prims, 1, INF, 1.0, Trad, Tref, &estimator) == RES_OK);
  printf("Average values of the right side of the square = ");
  check_estimator(estimator, N, analyticT, analyticCF, analyticRF, analyticTF);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);

  /* Check out of bound prims */
  prims[0] = 12;
  CHK(SOLVE(box_scn, N, prims, 2, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);
  prims[0] = 4;
  CHK(SOLVE(square_scn, N, prims, 1, INF, 1.0, Trad, Tref, &estimator) == RES_BAD_ARG);

  /* Average temperature on the left side of the box */
  prims[0] = 2;
  prims[1] = 3;

  analyticT = Tb;
  analyticCF = H * (analyticT - Tf);
  analyticRF = Hrad * (analyticT - Trad);
  analyticTF = analyticCF + analyticRF;

  CHK(SOLVE(box_scn, N, prims, 2, INF, 1.0, Trad, Tref, &estimator) == RES_OK);
  printf("Average values of the left side of the box = ");
  check_estimator(estimator, N, analyticT, analyticCF, analyticRF, analyticTF);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);

  /* Average temperature on the left/right side of the square */
  prims[0] = 1;
  CHK(SOLVE(square_scn, N, prims, 1, INF, 1.0, Trad, Tref, &estimator) == RES_OK);
  printf("Average values of the left side of the square = ");
  check_estimator(estimator, N, analyticT, analyticCF, analyticRF, analyticTF);
  CHK(sdis_estimator_ref_put(estimator) == RES_OK);
  #undef SOLVE

  CHK(sdis_scene_ref_put(box_scn) == RES_OK);
  CHK(sdis_scene_ref_put(square_scn) == RES_OK);
  CHK(sdis_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

