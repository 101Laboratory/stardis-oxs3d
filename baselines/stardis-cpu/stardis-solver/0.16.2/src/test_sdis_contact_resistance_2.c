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
#include "test_sdis_contact_resistance.h"

#include <rsys/clock_time.h>
#include <rsys/mem_allocator.h>
#include <rsys/double3.h>
#include <rsys_math.h>
#include <star/ssp.h>

/*
 * The scene is composed of a solid cube/square of size L whose temperature is
 * unknown. The cube is made of 2 solids that meet at x=e in ]0 L[ and the
 * thermal contact resistance is R, thus T(X0-) differs from T(X0+).
 * The faces are adiabatic exept at x=0 where T(0)=T0 and at x=L where T(L)=TL.
 * At steady state: 
 *
 *    Flux(x0) = (T(x0+) - T(x0-)) / R
 * 
 *             3D                    2D
 *
 *          /////////(L,L,L)     /////////(L,L)
 *          +-------+            +-------+
 *         /'  /   /|            |   !   |
 *        +-------+ TL          T0   r   TL
 *        | | !   | |            |   !   |
 *       T0 +.r...|.+            +-------+
 *        |,  !   |/         (0,0)///x=X0///
 *        +-------+
 * (0,0,0)///x=X0///
 */

#define N 10000 /* #realisations */

#define T0 0.0
#define LAMBDA1 0.1

#define TL 100.0
#define LAMBDA2 0.2

#define DELTA1 X0/30.0
#define DELTA2 (L-X0)/30.0

/*******************************************************************************
 * Media
 ******************************************************************************/
struct solid {
  double lambda;
  double rho;
  double cp;
  double delta;
};

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  (void)data;
  CHK(vtx);
  return SDIS_TEMPERATURE_NONE;
}

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx && data);
  return ((struct solid*)sdis_data_cget(data))->cp;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx && data);
  return ((struct solid*)sdis_data_cget(data))->lambda;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx && data);
  return ((struct solid*)sdis_data_cget(data))->rho;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx && data);
  return ((struct solid*)sdis_data_cget(data))->delta;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx  && data);
  return SDIS_TEMPERATURE_NONE;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double temperature;
  double resistance;
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
  CHK(frag && data);
  return 0;
}

static double
interface_get_contact_resistance
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  CHK(frag && data);
  return interf->resistance;
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
solve_probe
  (struct sdis_scene* scn,
   struct interf* interf_props,
   struct ssp_rng* rng)
{
  char dump[128];
  struct time t0, t1;
  struct sdis_estimator* estimator;
  struct sdis_solve_probe_boundary_args solve_args
    = SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  size_t nreals;
  size_t nfails;
  double ref_L, ref_R;
  enum sdis_scene_dimension dim;
  int nsimuls;
  int isimul;
  ASSERT(scn && interf_props && rng);

  OK(sdis_scene_get_dimension(scn, &dim));

  nsimuls = (dim == SDIS_SCENE_2D) ? 2 : 4;
  FOR_EACH(isimul, 0, nsimuls) {
    double ref;
    double r = pow(10, ssp_rng_uniform_double(rng, -2, 2));

    interf_props->resistance = r;

    ref_L = (
      T0 * (r * LAMBDA1 / X0) * (1 + r * LAMBDA2 / (L - X0))
      + TL * (r * LAMBDA2 / (L - X0))
      )
      / ((1 + r * LAMBDA1 / X0) * (1 + r * LAMBDA2 / (L - X0)) - 1);

    ref_R = ref_L * (1 + r * LAMBDA1 / X0) - T0 * r * LAMBDA1 / X0;
    
    if(dim == SDIS_SCENE_2D)
      /* last segment */
      solve_args.iprim = model2d_nsegments - 1;
    else
      /* last 2 triangles */
      solve_args.iprim = model3d_ntriangles - ((isimul % 2) ? 2 : 1);

    solve_args.uv[0] =  ssp_rng_canonical(rng);
    solve_args.uv[1] = (dim == SDIS_SCENE_2D)
      ? 0 : ssp_rng_uniform_double(rng, 0, 1 - solve_args.uv[0]);

    if(isimul < nsimuls / 2) {
      solve_args.side = SDIS_FRONT;
      ref = ref_L;
    } else {
      solve_args.side = SDIS_BACK;
      ref = ref_R;
    }

    solve_args.nrealisations = N;
    solve_args.time_range[0] = solve_args.time_range[1] = INF;

    time_current(&t0);
    OK(sdis_solve_probe_boundary(scn, &solve_args, &estimator));
    time_sub(&t0, time_current(&t1), &t0);
    time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));

    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    switch(dim) {
    case SDIS_SCENE_2D:
        printf("Steady temperature at (%lu/%s/%g) with R=%g = %g ~ %g +/- %g\n",
          (unsigned long)solve_args.iprim,
          (solve_args.side == SDIS_FRONT ? "front" : "back"),
          solve_args.uv[0],
          r, ref, T.E, T.SE);
      break;
    case SDIS_SCENE_3D:
        printf("Steady temperature at (%lu/%s/%g,%g) with R=%g = %g ~ %g +/- %g\n",
          (unsigned long)solve_args.iprim,
          (solve_args.side == SDIS_FRONT ? "front" : "back"),
          SPLIT2(solve_args.uv),
          r, ref, T.E, T.SE);
      break;
    default: FATAL("Unreachable code.\n"); break;
    }
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    printf("Elapsed time = %s\n", dump);
    printf("Time per realisation (in usec) = %g +/- %g\n\n", time.E, time.SE);

    CHK(nfails + nreals == N);
    CHK(nfails <= N/1000);
    CHK(eq_eps(T.E, ref, T.SE * 3));

    OK(sdis_estimator_ref_put(estimator));
  }
}
static void
solve
  (struct sdis_scene* scn,
   struct interf* interf_props,
   struct ssp_rng* rng)
{
  char dump[128];
  struct time t0, t1;
  struct sdis_estimator* estimator;
  struct sdis_solve_boundary_args solve_args = SDIS_SOLVE_BOUNDARY_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  const enum sdis_side all_front[] = { SDIS_FRONT, SDIS_FRONT };
  const enum sdis_side all_back[] = { SDIS_BACK, SDIS_BACK };
  size_t plist[2];
  size_t nreals;
  size_t nfails;
  double ref_L, ref_R;
  enum sdis_scene_dimension dim;
  int nsimuls;
  int isimul;
  ASSERT(scn && interf_props && rng);

  OK(sdis_scene_get_dimension(scn, &dim));

  nsimuls = (dim == SDIS_SCENE_2D) ? 2 : 4;
  FOR_EACH(isimul, 0, nsimuls) {
    double ref;
    double r = pow(10, ssp_rng_uniform_double(rng, -2, 2));

    interf_props->resistance = r;

    ref_L = (
      T0 * (r * LAMBDA1 / X0) * (1 + r * LAMBDA2 / (L - X0))
      + TL * (r * LAMBDA2 / (L - X0))
      )
      / ((1 + r * LAMBDA1 / X0) * (1 + r * LAMBDA2 / (L - X0)) - 1);

    ref_R = ref_L * (1 + r * LAMBDA1 / X0) - T0 * r * LAMBDA1 / X0;

    if(dim == SDIS_SCENE_2D) {
      /* last segment */
      solve_args.nprimitives = 1;
      plist[0] = model2d_nsegments - 1;
    } else {
      /* last 2 triangles */
      solve_args.nprimitives = 2;
      plist[0] = model3d_ntriangles - 2;
      plist[1] = model3d_ntriangles - 1;
    }
    solve_args.primitives = plist;

    if(isimul < nsimuls / 2) {
      solve_args.sides = all_front;
      ref = ref_L;
    } else {
      solve_args.sides = all_back;
      ref = ref_R;
    }

    solve_args.nrealisations = N;
    solve_args.time_range[0] = solve_args.time_range[1] = INF;

    time_current(&t0);
    OK(sdis_solve_boundary(scn, &solve_args, &estimator));
    time_sub(&t0, time_current(&t1), &t0);
    time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));

    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    printf("Steady temperature at the %s side with R=%g = %g ~ %g +/- %g\n",
      (solve_args.sides[0] == SDIS_FRONT ? "front" : "back"),
      r, ref, T.E, T.SE);
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    printf("Elapsed time = %s\n", dump);
    printf("Time per realisation (in usec) = %g +/- %g\n\n", time.E, time.SE);

    CHK(nfails + nreals == N);
    CHK(nfails <= N / 1000);
    CHK(eq_eps(T.E, ref, T.SE * 3));

    OK(sdis_estimator_ref_put(estimator));
  }
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid1 = NULL;
  struct sdis_medium* solid2 = NULL;
  struct sdis_interface* interf_adiabatic1 = NULL;
  struct sdis_interface* interf_adiabatic2 = NULL;
  struct sdis_interface* interf_T0 = NULL;
  struct sdis_interface* interf_TL = NULL;
  struct sdis_interface* interf_R = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_scene* square_scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_interface* model3d_interfaces[22 /*#triangles*/];
  struct sdis_interface* model2d_interfaces[7/*#segments*/];
  struct interf* interf_props = NULL;
  struct solid* solid_props = NULL;
  struct ssp_rng* rng = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  fluid_shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid medium #1 */
  OK(sdis_data_create(dev, sizeof(struct solid), 16, NULL, &data));
  solid_props = sdis_data_get(data);
  solid_props->lambda = LAMBDA1;
  solid_props->cp = 2;
  solid_props->rho = 25;
  solid_props->delta = DELTA1;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid1));
  OK(sdis_data_ref_put(data));

  /* Create the solid medium #2 */
  OK(sdis_data_create(dev, sizeof(struct solid), 16, NULL, &data));
  solid_props = sdis_data_get(data);
  solid_props->lambda = LAMBDA2;
  solid_props->cp = 2;
  solid_props->rho = 25;
  solid_props->delta = DELTA2;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid2));
  OK(sdis_data_ref_put(data));

  /* Setup the interface shader */
  interf_shader.front.temperature = interface_get_temperature;
  interf_shader.back.temperature = interface_get_temperature;
  interf_shader.convection_coef = interface_get_convection_coef;

  /* Create the adiabatic interfaces */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create
    (dev, solid1, fluid, &interf_shader, data, &interf_adiabatic1));
  OK(sdis_interface_create
    (dev, solid2, fluid, &interf_shader, data, &interf_adiabatic2));
  OK(sdis_data_ref_put(data));

  /* Create the T0 interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = T0;
  OK(sdis_interface_create
    (dev, solid1, fluid, &interf_shader, data, &interf_T0));
  OK(sdis_data_ref_put(data));

  /* Create the TL interface */
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = TL;
  OK(sdis_interface_create
    (dev, solid2, fluid, &interf_shader, data, &interf_TL));
  OK(sdis_data_ref_put(data));

  /* Create the solid1-solid2 interface */
  interf_shader.convection_coef = NULL;
  interf_shader.thermal_contact_resistance = interface_get_contact_resistance;
  OK(sdis_data_create(dev, sizeof(struct interf), 16, NULL, &data));
  interf_props = sdis_data_get(data);
  interf_props->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_interface_create
    (dev, solid1, solid2, &interf_shader, data, &interf_R));
  OK(sdis_data_ref_put(data));

  /* Release the media */
  OK(sdis_medium_ref_put(solid1));
  OK(sdis_medium_ref_put(solid2));
  OK(sdis_medium_ref_put(fluid));

  /* Map the interfaces to their box triangles */
  /* Front */
  model3d_interfaces[0] = interf_adiabatic1;
  model3d_interfaces[1] = interf_adiabatic1;
  model3d_interfaces[2] = interf_adiabatic2;
  model3d_interfaces[3] = interf_adiabatic2;
  /* Left */
  model3d_interfaces[4] = interf_T0;
  model3d_interfaces[5] = interf_T0;
  /* Back */
  model3d_interfaces[6] = interf_adiabatic1;
  model3d_interfaces[7] = interf_adiabatic1;
  model3d_interfaces[8] = interf_adiabatic2;
  model3d_interfaces[9] = interf_adiabatic2;
  /* Right */
  model3d_interfaces[10] = interf_TL;
  model3d_interfaces[11] = interf_TL;
  /* Top */
  model3d_interfaces[12] = interf_adiabatic1; 
  model3d_interfaces[13] = interf_adiabatic1;
  model3d_interfaces[14] = interf_adiabatic2;
  model3d_interfaces[15] = interf_adiabatic2;
  /* Bottom */
  model3d_interfaces[16] = interf_adiabatic1;
  model3d_interfaces[17] = interf_adiabatic1;
  model3d_interfaces[18] = interf_adiabatic2;
  model3d_interfaces[19] = interf_adiabatic2;
  /* Inside */
  model3d_interfaces[20] = interf_R;
  model3d_interfaces[21] = interf_R;

  /* Map the interfaces to their square segments */
   /* Bottom */
  model2d_interfaces[0] = interf_adiabatic2;
  model2d_interfaces[1] = interf_adiabatic1;
  /* Left */
  model2d_interfaces[2] = interf_T0;
  /* Top */
  model2d_interfaces[3] = interf_adiabatic1;
  model2d_interfaces[4] = interf_adiabatic2;
  /* Right */
  model2d_interfaces[5] = interf_TL;
  /* Contact */
  model2d_interfaces[6] = interf_R;

  /* Create the box scene */
  scn_args.get_indices = model3d_get_indices;
  scn_args.get_interface = model3d_get_interface;
  scn_args.get_position = model3d_get_position;
  scn_args.nprimitives = model3d_ntriangles;
  scn_args.nvertices = model3d_nvertices;
  scn_args.context = model3d_interfaces;
  OK(sdis_scene_create(dev, &scn_args, &box_scn));

  /* Create the square scene */
  scn_args.get_indices = model2d_get_indices;
  scn_args.get_interface = model2d_get_interface;
  scn_args.get_position = model2d_get_position;
  scn_args.nprimitives = model2d_nsegments;
  scn_args.nvertices = model2d_nvertices;
  scn_args.context = model2d_interfaces;
  OK(sdis_scene_2d_create(dev, &scn_args, &square_scn));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_adiabatic1));
  OK(sdis_interface_ref_put(interf_adiabatic2));
  OK(sdis_interface_ref_put(interf_T0));
  OK(sdis_interface_ref_put(interf_TL));
  OK(sdis_interface_ref_put(interf_R));

  /* Solve */
  OK(ssp_rng_create(NULL, SSP_RNG_KISS, &rng));
  printf(">> Box scene\n");
  solve_probe(box_scn, interf_props, rng);
  solve(box_scn, interf_props, rng);
  printf("\n>> Square scene\n");
  solve_probe(square_scn, interf_props, rng);
  solve(square_scn, interf_props, rng);

  OK(sdis_scene_ref_put(box_scn));
  OK(sdis_scene_ref_put(square_scn));
  OK(sdis_device_ref_put(dev));
  OK(ssp_rng_ref_put(rng));

  CHK(mem_allocated_size() == 0);
  return 0;
}

