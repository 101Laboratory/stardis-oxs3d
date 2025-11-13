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

/*
 * The configuration is a rectangle whose the conductivity is lambda and its
 * temperature is unknown. Its top and bottom boundaries rectangle are
 * adiabatics while its left and right ones have a fixed fluxes of phi1 and
 * phi2 respectively. The left boundary has also a convective exchange with the
 * surrounding fluid whose temperature is Text. At stationnary, the
 * temperature at a given position into the solid rectangle is:
 *
 *    T(x) = phi2/lambda*x + (Text + (phi1 + phi2)/h)
 *
 * with h the convective coefficient on the left boundary
 *
 *
 *      Text   ///// (0.2,0.5)
 *            +---------+
 *            |         |
 *   h _\     |-->   <--|
 *    / /   phi1->   <-phi2
 *    \__/    |-->   <--|
 *            |         |
 *            +---------+
 *          (0,0) ///////
 */

#define LAMBDA 25.0
#define RHO 7500.0
#define CP 500.0
#define DELTA 0.01
#define Text 373.15
#define PHI1 1000.0
#define PHI2 5000.0
#define H 10

#define N 10000 /* #realisations */

/*******************************************************************************
 * Geometry
 ******************************************************************************/
static void
get_position(const size_t ivert, double pos[2], void* ctx)
{
  square_get_position(ivert, pos, ctx);
  pos[0] *= 0.2;
  pos[1] *= 0.5;
}

/*******************************************************************************
 * Media
 ******************************************************************************/
static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return CP;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return LAMBDA;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return RHO;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return DELTA;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return SDIS_TEMPERATURE_NONE;
}

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data, (void)vtx;
  return Text;
}

static struct sdis_medium*
create_solid(struct sdis_device* dev)
{
  struct sdis_solid_shader shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_medium* solid = NULL;

  /* Create the solid_medium */
  shader.calorific_capacity = solid_get_calorific_capacity;
  shader.thermal_conductivity = solid_get_thermal_conductivity;
  shader.volumic_mass = solid_get_volumic_mass;
  shader.delta = solid_get_delta;
  shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(dev, &shader, NULL, &solid));
  return solid;
}

static struct sdis_medium*
create_fluid(struct sdis_device* dev)
{
  struct sdis_fluid_shader shader = DUMMY_FLUID_SHADER;
  struct sdis_medium* fluid = NULL;

  /* Create the solid_medium */
  shader.temperature = fluid_get_temperature;
  OK(sdis_fluid_create(dev, &shader, NULL, &fluid));
  return fluid;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double h;
  double phi;
};

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  (void)frag;
  return interf->h;
}

static double
interface_get_flux
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct interf* interf = sdis_data_cget(data);
  (void)frag;
  return interf->phi;
}

static struct sdis_interface*
interface_create
  (struct sdis_device* sdis,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const double h,
   const double phi)
{
  struct sdis_interface* interf = NULL;
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_data* data = NULL;
  struct interf* props = NULL;

  shader.front.flux = interface_get_flux;
  shader.convection_coef = interface_get_convection_coef;
  shader.convection_coef_upper_bound = h;

  OK(sdis_data_create(sdis, sizeof(struct interf), 16, NULL, &data));
  props = sdis_data_get(data);
  props->h = h;
  props->phi = phi;
  OK(sdis_interface_create(sdis, front, back, &shader, data, &interf));
  OK(sdis_data_ref_put(data));
  return interf;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* dev = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_interface* interf_left = NULL;
  struct sdis_interface* interf_right = NULL;
  struct sdis_interface* interf_adiab = NULL;
  struct sdis_interface* interfaces[4];
  struct sdis_medium* solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_mc mc = SDIS_MC_NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_solve_probe_args probe_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_solve_boundary_flux_args flux_args =
    SDIS_SOLVE_BOUNDARY_FLUX_ARGS_DEFAULT;
  struct sdis_solve_probe_boundary_flux_args probe_flux_args =
    SDIS_SOLVE_PROBE_BOUNDARY_FLUX_ARGS_DEFAULT;
  double Tref = 0;
  size_t prim = 0;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  solid = create_solid(dev);
  fluid = create_fluid(dev);

  interf_left = interface_create(dev, solid, fluid, H, PHI1);
  interf_right = interface_create(dev, solid, fluid, 0, PHI2);
  interf_adiab = interface_create(dev, solid, fluid, 0, SDIS_FLUX_NONE);
  interfaces[0] = interf_adiab; /* Bottom */
  interfaces[1] = interf_left;
  interfaces[2] = interf_adiab; /* Top */
  interfaces[3] = interf_right;

  scn_args.get_indices = square_get_indices;
  scn_args.get_interface = square_get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = square_nsegments;
  scn_args.nvertices = square_nvertices;
  scn_args.context = interfaces;
  OK(sdis_scene_2d_create(dev, &scn_args, &scn));

  prim = 1; /* Left */
  flux_args.nrealisations = N;
  flux_args.nprimitives = 1;
  flux_args.primitives = &prim;
  OK(sdis_solve_boundary_flux(scn, &flux_args, &estimator));
  OK(sdis_estimator_get_convective_flux(estimator, &mc));
  printf("Convective flux (left side) ~ %g W/m² +/- %g\n", mc.E, mc.SE);
  OK(sdis_estimator_get_imposed_flux(estimator, &mc));
  printf("Imposed flux (left side) ~ %g W/m² +/- %g\n", mc.E, mc.SE);
  OK(sdis_estimator_get_total_flux(estimator, &mc));
  printf("Total flux (left side) ~ %g W/m² +/- %g\n", mc.E, mc.SE);
  CHK(eq_eps(-PHI2, mc.E, 3*mc.SE));
  OK(sdis_estimator_ref_put(estimator));

  probe_flux_args.nrealisations = N;
  probe_flux_args.iprim = 1; /* Left */
  probe_flux_args.uv[0] = 0.5;
  OK(sdis_solve_probe_boundary_flux(scn, &probe_flux_args, &estimator));
  OK(sdis_estimator_get_convective_flux(estimator, &mc));
  printf("Convective flux (probe on left side) ~ %g W/m² +/- %g\n", mc.E, mc.SE);
  OK(sdis_estimator_get_imposed_flux(estimator, &mc));
  printf("Imposed flux (probe on left side) ~ %g W/m² +/- %g\n", mc.E, mc.SE);
  OK(sdis_estimator_get_total_flux(estimator, &mc));
  printf("Total flux (probe on left side) ~ %g W/m² +/- %g\n", mc.E, mc.SE);
  CHK(eq_eps(-PHI2, mc.E, 3*mc.SE));
  OK(sdis_estimator_ref_put(estimator));

  probe_args.nrealisations = N;
  probe_args.position[0] = 0.2;
  probe_args.position[1] = 0.25;
  OK(sdis_solve_probe(scn, &probe_args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &mc));
  OK(sdis_estimator_ref_put(estimator));

  Tref = PHI2/LAMBDA*probe_args.position[0]  + (Text + (PHI1 + PHI2)/H);
  printf("Temperature at %g = %g ~ %g +/- %g\n",
    probe_args.position[0], Tref, mc.E, mc.SE);
  CHK(eq_eps(mc.E, Tref, 3*mc.SE));

  OK(sdis_device_ref_put(dev));
  OK(sdis_interface_ref_put(interf_left));
  OK(sdis_interface_ref_put(interf_right));
  OK(sdis_interface_ref_put(interf_adiab));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_scene_ref_put(scn));
  CHK(mem_allocated_size() == 0);
  return 0;
}
