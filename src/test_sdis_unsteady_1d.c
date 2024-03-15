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

#include "sdis.h"
#include "test_sdis_utils.h"

/*
 * The scene is a solid 1d slab simulated by square whose temperature is fixed
 * on two of its faces, the two others are adiabatic. The test calculates its
 * temperature at a given position at different observation times and validates
 * the result against the calculated values by analytically solving the green
 * functions.
 *
 *     ///////(0.1,0.1)
 *     +-------+
 *     |       | T1
 *     |       |
 *  T0 |       |
 *     +-------+
 *  (0,0)///////
 */

#define T_INIT 280 /* [K] */
#define T0 310 /* [K] */
#define T1 320 /* [K] */

#define NREALISATIONS 10000
#define FP_TO_METER 0.1

struct reference {
  double pos[2]; /* [m/FP_TO_METER] */
  double time; /* [s] */
  double temp; /* [K] */
};

static const struct reference references[] = {
  {{0.5, 0.5}, 1000.0000, 280.02848664122115},
  {{0.5, 0.5}, 2000.0000, 280.86935314560424},
  {{0.5, 0.5}, 3000.0000, 282.88587826961236},
  {{0.5, 0.5}, 4000.0000, 285.39698306113996},
  {{0.5, 0.5}, 5000.0000, 287.96909375994932},
  {{0.5, 0.5}, 10000.000, 298.39293888670881},
  {{0.5, 0.5}, 20000.000, 308.80965010883347},
  {{0.5, 0.5}, 30000.000, 312.69280796373141},
  {{0.5, 0.5}, 1000000.0, 315.00000000000000}
};
static const size_t nreferences = sizeof(references)/sizeof(*references);

/*******************************************************************************
 * Solid, i.e. medium of the cube
 ******************************************************************************/
#define SOLID_PROP(Prop, Val)                                                  \
  static double                                                                \
  solid_get_##Prop                                                             \
    (const struct sdis_rwalk_vertex* vtx,                                      \
     struct sdis_data* data)                                                   \
  {                                                                            \
    (void)vtx, (void)data; /* Avoid the "unused variable" warning */           \
    return Val;                                                                \
  }
SOLID_PROP(calorific_capacity, 2000.0) /* [J/K/kg] */
SOLID_PROP(thermal_conductivity, 0.5) /* [W/m/K] */
SOLID_PROP(volumic_mass, 2500.0) /* [kg/m^3] */
SOLID_PROP(delta, 1.0/60.0)
#undef SOLID_PROP

static double /* [K] */
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  (void)data;
  ASSERT(vtx);
  if(vtx->time <= 0) return T_INIT; /* Initial temperature [K] */
  return -1; /* Unknown temperature */
}

static struct sdis_medium*
create_solid(struct sdis_device* sdis)
{
  struct sdis_solid_shader shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_medium* solid = NULL;

  shader.calorific_capacity = solid_get_calorific_capacity;
  shader.thermal_conductivity = solid_get_thermal_conductivity;
  shader.volumic_mass = solid_get_volumic_mass;
  shader.delta = solid_get_delta;
  shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(sdis, &shader, NULL, &solid));
  return solid;
}

/*******************************************************************************
 * Dummy environment, i.e. environment surrounding the cube. It is defined only
 * for Stardis compliance: in Stardis, an interface must divide 2 media.
 ******************************************************************************/
static struct sdis_medium*
create_dummy(struct sdis_device* sdis)
{
  struct sdis_fluid_shader shader = SDIS_FLUID_SHADER_NULL;
  struct sdis_medium* dummy = NULL;

  shader.calorific_capacity = dummy_medium_getter;
  shader.volumic_mass = dummy_medium_getter;
  shader.temperature = dummy_medium_getter;
  OK(sdis_fluid_create(sdis, &shader, NULL, &dummy));
  return dummy;
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* sdis = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* dummy = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &sdis));

  solid = create_solid(sdis);
  dummy = create_dummy(sdis);

  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_device_ref_put(sdis));

  CHK(mem_allocated_size() == 0);
  return 0;
}
