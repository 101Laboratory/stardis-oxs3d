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

#include <star/ssp.h>
#include <rsys_math.h>

 /*
  * The scene is composed of a solid cube/square whose temperature is unknown.
  * The convection coefficient with the surrounding fluid is null excepted for
  * the X faces whose value is 'H'. The Temperature T of the -X face is fixed
  * to Tb. The ambient radiative temperature is 0 excepted for the X faces
  * whose value is 'Trad'.
  * This test computes temperature and fluxes on the X faces and check that
  * they are equal to:
  *
  *    T(+X) = (H*Tf + Hrad*Trad + LAMBDA/A * Tb) / (H+Hrad+LAMBDA/A)
  *         with Hrad = 4 * BOLTZMANN_CONSTANT * Tref^3 * epsilon
  *    T(-X) = Tb
  *
  *    CF = H * (Tf - T)
  *    RF = Hrad * (Trad - T)
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

#define N 100000 /* #realisations */

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
  return 1.0 / 40.0;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void) data;
  CHK(vtx != NULL);
  return SDIS_TEMPERATURE_NONE;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double temperature;
  double emissivity;
  double hc;
  double reference_temperature;
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
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  (void)source_id;
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

static double
interface_get_reference_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && data);
  return interf->reference_temperature;
}

/*******************************************************************************
 * Radiative environment
 ******************************************************************************/
static double
radenv_get_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray, (void)data;
  return Trad;
}

static double
radenv_get_reference_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray, (void)data;
  return Trad;
}

static struct sdis_radiative_env*
create_radenv(struct sdis_device* dev)
{
  struct sdis_radiative_env_shader shader = SDIS_RADIATIVE_ENV_SHADER_NULL;
  struct sdis_radiative_env* radenv = NULL;

  shader.temperature = radenv_get_temperature;
  shader.reference_temperature = radenv_get_reference_temperature;
  OK(sdis_radiative_env_create(dev, &shader, NULL, &radenv));
  return radenv;
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

  OK(sdis_estimator_get_temperature(estimator, &V));
  OK(sdis_estimator_get_realisation_count(estimator, &nreals));
  OK(sdis_estimator_get_failure_count(estimator, &nfails));
  printf("T = %g ~ %g +/- %g\n", T, V.E, V.SE);
  CHK(eq_eps(V.E, T, 3 * (V.SE ? V.SE : FLT_EPSILON)));
  OK(sdis_estimator_get_type(estimator, &type));
  if(type == SDIS_ESTIMATOR_FLUX) {
    OK(sdis_estimator_get_convective_flux(estimator, &V));
    printf("Convective flux = %g ~ %g +/- %g\n", CF, V.E, V.SE);
    CHK(eq_eps(V.E, CF, 3 * (V.SE ? V.SE : FLT_EPSILON)));
    OK(sdis_estimator_get_radiative_flux(estimator, &V));
    printf("Radiative flux = %g ~ %g +/- %g\n", RF, V.E, V.SE);
    CHK(eq_eps(V.E, RF, 3 * (V.SE ? V.SE : FLT_EPSILON)));
    OK(sdis_estimator_get_total_flux(estimator, &V));
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
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid1 = NULL;
  struct sdis_medium* fluid2 = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_interface* interf_adiabatic = NULL;
  struct sdis_interface* interf_Tb = NULL;
  struct sdis_interface* interf_H = NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_scene* square_scn = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_estimator* estimator2 = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* box_interfaces[12 /*#triangles*/];
  struct sdis_interface* square_interfaces[4/*#segments*/];
  struct sdis_solve_probe_boundary_flux_args probe_args =
    SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT;
  struct sdis_solve_boundary_flux_args bound_args =
    SDIS_SOLVE_BOUNDARY_FLUX_ARGS_DEFAULT;
  struct interf* interf_props = NULL;
  struct fluid* fluid_args = NULL;
  struct ssp_rng* rng = NULL;
  enum sdis_estimator_type type;
  double pos[3];
  double analyticT, analyticCF, analyticRF, analyticTF;
  size_t prims[2];
  int is_master_process;
  (void)argc, (void)argv;

  create_default_device(&argc, &argv, &is_master_process, &dev);
  radenv = create_radenv(dev);

  /* Create the fluid medium. In fact, create two fluid media, even if they are
   * identical, to check that Robin's boundary conditions can be defined with
   * several media, without compromising path sampling. Convective paths cannot
   * be sampled in enclosures with several media, but radiative paths can be,
   * and it should be possible to calculate the boundary flux without any
   * problem */
  OK(sdis_data_create
    (dev, sizeof(struct fluid), ALIGNOF(struct fluid), NULL, &data));
  fluid_args = sdis_data_get(data);
  fluid_args->temperature = Tf;
  fluid_shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid1));
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid2));
  OK(sdis_data_ref_put(data));

  /* Create the solid_medium */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(dev, &solid_shader, NULL, &solid));

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;
  interf_shader.front.temperature = interface_get_temperature;
  interf_shader.front.specular_fraction = NULL;
  interf_shader.back = SDIS_INTERFACE_SIDE_SHADER_NULL;

  /* Create the adiabatic interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->hc = 0;
  interf_props->temperature = SDIS_TEMPERATURE_NONE;
  interf_props->emissivity = 0;
  OK(sdis_interface_create
    (dev, solid, fluid1, &interf_shader, data, &interf_adiabatic));
  OK(sdis_data_ref_put(data));

  /* Create the Tb interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->hc = H;
  interf_props->temperature = Tb;
  interf_props->emissivity = EPSILON;
  interf_props->reference_temperature = Tb;
  interf_shader.back.emissivity = interface_get_emissivity;
  interf_shader.back.reference_temperature = interface_get_reference_temperature;
  OK(sdis_interface_create
    (dev, solid, fluid1, &interf_shader, data, &interf_Tb));
  interf_shader.back.emissivity = NULL;
  OK(sdis_data_ref_put(data));

  /* Create the H interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->hc = H;
  interf_props->temperature = SDIS_TEMPERATURE_NONE;
  interf_props->emissivity = EPSILON;
  interf_props->reference_temperature = Tref;
  interf_shader.back.emissivity = interface_get_emissivity;
  interf_shader.back.reference_temperature = interface_get_reference_temperature;
  OK(sdis_interface_create
    (dev, solid, fluid2, &interf_shader, data, &interf_H));
  interf_shader.back.emissivity = NULL;
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid1));
  OK(sdis_medium_ref_put(fluid2));

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
  scn_args.get_indices = box_get_indices;
  scn_args.get_interface = box_get_interface;
  scn_args.get_position = box_get_position;
  scn_args.nprimitives = box_ntriangles;
  scn_args.nvertices = box_nvertices;
  scn_args.t_range[0] = MMIN(MMIN(Tf, Trad), Tb);
  scn_args.t_range[1] = MMAX(MMAX(Tf, Trad), Tb);
  scn_args.radenv = radenv;
  scn_args.context = box_interfaces;
  OK(sdis_scene_create(dev, &scn_args, &box_scn));

  /* Create the square scene */
  scn_args.get_indices = square_get_indices;
  scn_args.get_interface = square_get_interface;
  scn_args.get_position = square_get_position;
  scn_args.nprimitives = square_nsegments;
  scn_args.nvertices = square_nvertices;
  scn_args.t_range[0] = MMIN(MMIN(Tf, Trad), Tb);
  scn_args.t_range[1] = MMAX(MMAX(Tf, Trad), Tb);
  scn_args.radenv = radenv;
  scn_args.context = square_interfaces;
  OK(sdis_scene_2d_create(dev, &scn_args, &square_scn));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_adiabatic));
  OK(sdis_interface_ref_put(interf_Tb));
  OK(sdis_interface_ref_put(interf_H));

  analyticT = (H*Tf + Hrad*Trad + LAMBDA * Tb) / (H + Hrad + LAMBDA);
  analyticCF = H * (Tf - analyticT);
  analyticRF = Hrad * (Trad - analyticT);
  analyticTF = analyticCF + analyticRF;

  #define SOLVE sdis_solve_probe_boundary_flux
  probe_args.nrealisations = N;
  probe_args.iprim = 6;
  probe_args.uv[0] = 0.3;
  probe_args.uv[1] = 0.3;
  probe_args.time_range[0] = INF;
  probe_args.time_range[1] = INF;
  BA(SOLVE(NULL, &probe_args, &estimator));
  BA(SOLVE(box_scn, NULL, &estimator));
  BA(SOLVE(box_scn, &probe_args, NULL));
  probe_args.nrealisations = 0;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.nrealisations = N;
  probe_args.iprim = 12;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.iprim = 6;
  probe_args.uv[0] = probe_args.uv[1] = 1;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.uv[0] = probe_args.uv[1] = 0.3;
  probe_args.time_range[0] = probe_args.time_range[1] = -1;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.time_range[0] = 1;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.time_range[1] = 0;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.time_range[1] = INF;
  BA(SOLVE(box_scn, &probe_args, &estimator));
  probe_args.time_range[0] = INF;
  OK(SOLVE(box_scn, &probe_args, &estimator));

  if(!is_master_process) {
    CHK(estimator == NULL);
  } else {
    OK(sdis_estimator_get_type(estimator, &type));
    CHK(type == SDIS_ESTIMATOR_FLUX);

    OK(sdis_scene_get_boundary_position
      (box_scn, probe_args.iprim, probe_args.uv, pos));
    printf("Boundary values of the box at (%g %g %g) = ", SPLIT3(pos));
    check_estimator(estimator, N, analyticT, analyticCF, analyticRF, analyticTF);
  }

  /* Check the RNG type */
  probe_args.rng_state = NULL;
  probe_args.rng_type = SSP_RNG_TYPE_NULL;
  BA(SOLVE(box_scn, &probe_args, &estimator2));
  probe_args.rng_type =
    SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT.rng_type == SSP_RNG_THREEFRY
    ? SSP_RNG_MT19937_64 : SSP_RNG_THREEFRY;
  OK(SOLVE(box_scn, &probe_args, &estimator2));
  if(is_master_process) {
    struct sdis_mc T, T2;
    check_estimator(estimator2, N, analyticT, analyticCF, analyticRF, analyticTF);
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Check RNG state */
  OK(ssp_rng_create(NULL, SSP_RNG_THREEFRY, &rng));
  OK(ssp_rng_discard(rng, 31415926535)); /* Move the RNG state  */
  probe_args.rng_state = rng;
  probe_args.rng_type = SSP_RNG_TYPE_NULL;
  OK(SOLVE(box_scn, &probe_args, &estimator2));
  OK(ssp_rng_ref_put(rng));
  if(is_master_process) {
    struct sdis_mc T, T2;
    check_estimator(estimator2, N, analyticT, analyticCF, analyticRF, analyticTF);
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  if(estimator) OK(sdis_estimator_ref_put(estimator));

  /* Restore arguments */
  probe_args.rng_state = SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT.rng_state;
  probe_args.rng_type = SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT.rng_type;

  probe_args.uv[0] = 0.5;
  probe_args.iprim = 4;
  BA(SOLVE(square_scn, &probe_args, &estimator));
  probe_args.iprim = 3;
  OK(SOLVE(square_scn, &probe_args, &estimator));
  if(is_master_process) {
    OK(sdis_scene_get_boundary_position
      (square_scn, probe_args.iprim, probe_args.uv, pos));
    printf("Boundary values of the square at (%g %g) = ", SPLIT2(pos));
    check_estimator(estimator, N, analyticT, analyticCF, analyticRF, analyticTF);
    OK(sdis_estimator_ref_put(estimator));
  }

  #undef F
  #undef SOLVE

  #define SOLVE sdis_solve_boundary_flux
  prims[0] = 6;
  prims[1] = 7;
  bound_args.nrealisations = N;
  bound_args.primitives = prims;
  bound_args.nprimitives = 2;
  bound_args.time_range[0] = INF;
  bound_args.time_range[1] = INF;

  BA(SOLVE(NULL, &bound_args, &estimator));
  BA(SOLVE(box_scn, NULL, &estimator));
  BA(SOLVE(box_scn, &bound_args, NULL));
  bound_args.primitives = NULL;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.primitives = prims;
  bound_args.nprimitives = 0;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.nprimitives = 2;
  bound_args.time_range[0] = bound_args.time_range[1] = -1;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.time_range[0] = 1;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.time_range[1] = 0;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.time_range[1] = INF;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  bound_args.time_range[0] = INF;
  prims[0] = 12;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  prims[0] = 6;
  OK(SOLVE(box_scn, &bound_args, &estimator));

  if(!is_master_process) {
    CHK(estimator == NULL);
  } else {
    /* Average temperature on the right side of the box */
    printf("Average values of the right side of the box = ");
    check_estimator(estimator, N, analyticT, analyticCF, analyticRF, analyticTF);
  }

  /* Check the RNG type */
  bound_args.rng_state = NULL;
  bound_args.rng_type = SSP_RNG_TYPE_NULL;
  BA(SOLVE(box_scn, &bound_args, &estimator2));
  bound_args.rng_type =
    SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT.rng_type == SSP_RNG_THREEFRY
    ? SSP_RNG_MT19937_64 : SSP_RNG_THREEFRY;
  OK(SOLVE(box_scn, &bound_args, &estimator2));
  if(is_master_process) {
    struct sdis_mc T, T2;
    check_estimator(estimator2, N, analyticT, analyticCF, analyticRF, analyticTF);
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  /* Check RNG state */
  OK(ssp_rng_create(NULL, SSP_RNG_THREEFRY, &rng));
  OK(ssp_rng_discard(rng, 31415926535)); /* Move the RNG state  */
  bound_args.rng_state = rng;
  bound_args.rng_type = SSP_RNG_TYPE_NULL;
  OK(SOLVE(box_scn, &bound_args, &estimator2));
  OK(ssp_rng_ref_put(rng));
  if(is_master_process) {
    struct sdis_mc T, T2;
    check_estimator(estimator2, N, analyticT, analyticCF, analyticRF, analyticTF);
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_temperature(estimator2, &T2));
    CHK(T2.E != T.E);
    OK(sdis_estimator_ref_put(estimator2));
  }

  if(estimator) OK(sdis_estimator_ref_put(estimator));

  /* Restore arguments */
  bound_args.rng_state = SDIS_SOLVE_BOUNDARY_FLUX_ARGS_DEFAULT.rng_state;
  bound_args.rng_type = SDIS_SOLVE_BOUNDARY_FLUX_ARGS_DEFAULT.rng_type;

  /* Average temperature on the right side of the square */
  prims[0] = 4;
  bound_args.nprimitives = 1;
  BA(SOLVE(square_scn, &bound_args, &estimator));
  prims[0] = 3;
  OK(SOLVE(square_scn, &bound_args, &estimator));
  if(is_master_process) {
    printf("Average values of the right side of the square = ");
    check_estimator(estimator, N, analyticT, analyticCF, analyticRF, analyticTF);
    OK(sdis_estimator_ref_put(estimator));
  }

  /* Flux computation on Dirichlet boundaries is not available yet.
   * Once available, the expected total flux is the same we expect on the right
   * side (as the other sides are adiabatic). */
  prims[0] = 2;
  prims[1] = 3;
  bound_args.nprimitives = 2;
  BA(SOLVE(box_scn, &bound_args, &estimator));
  prims[0] = 1;
  bound_args.nprimitives = 1;
  BA(SOLVE(square_scn, &bound_args, &estimator));
  #undef SOLVE

  OK(sdis_radiative_env_ref_put(radenv));
  OK(sdis_scene_ref_put(box_scn));
  OK(sdis_scene_ref_put(square_scn));
  free_default_device(dev);

  CHK(mem_allocated_size() == 0);
  return 0;
}
