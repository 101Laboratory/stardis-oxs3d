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
 * The scene is a solid cube whose temperature is fixed on its faces. The test
 * calculates its temperature at a given position at different observation times
 * and validates the result against the calculated values by analytically
 * solving the green functions.
 *
 *          T3  (0.1,0.1,0.1)
 *       +-------+
 *      /'  T4  /|
 *     +-------+ | T1
 *  T0 | +.....|.+
 *     |, T5   |/
 *     +-------+
 * (0,0,0) T2
 */

#define T_INIT 280 /* [K] */
#define T0 310 /* [K] */
#define T1 320 /* [K] */
#define T2 330 /* [K] */
#define T3 310 /* [K] */
#define T4 320 /* [K] */
#define T5 300 /* [K] */

#define NREALISATIONS 10000
#define FP_TO_METER 0.1

struct reference {
  double pos[3]; /* [m/FP_TO_METER] */
  double time; /* [s] */
  double temp; /* [K] */
};

static const struct reference references[] = {
  {{0.3, 0.4, 0.6}, 1000.0000, 281.33455593977152},
  {{0.3, 0.4, 0.6}, 2000.0000, 286.90151817350699},
  {{0.3, 0.4, 0.6}, 3000.0000, 292.84330866161531},
  {{0.3, 0.4, 0.6}, 4000.0000, 297.81444160746452},
  {{0.3, 0.4, 0.6}, 5000.0000, 301.70787295764546},
  {{0.3, 0.4, 0.6}, 10000.000, 310.78920179442139},
  {{0.3, 0.4, 0.6}, 20000.000, 313.37629443163121},
  {{0.3, 0.4, 0.6}, 30000.000, 313.51064004438581},
  {{0.3, 0.4, 0.6}, 1000000.0, 313.51797642855502}
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
  return SDIS_TEMPERATURE_NONE;
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
 * Interface: its temperature is known
 ******************************************************************************/
static double /* [K] */
interface_get_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  (void)data;
  ASSERT(frag);

       if(frag->Ng[0] ==  1) return T0;
  else if(frag->Ng[0] == -1) return T1;
  else if(frag->Ng[1] ==  1) return T2;
  else if(frag->Ng[1] == -1) return T3;
  else if(frag->Ng[2] ==  1) return T4;
  else if(frag->Ng[2] == -1) return T5;
  else FATAL("Unreachable code\n");
}

static struct sdis_interface*
create_interface
  (struct sdis_device* sdis,
   struct sdis_medium* front,
   struct sdis_medium* back)
{
  struct sdis_interface* interf = NULL;
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;

  shader.front.temperature = interface_get_temperature;
  shader.back.temperature = interface_get_temperature;
  OK(sdis_interface_create(sdis, front, back, &shader, NULL, &interf));
  return interf;
}

/*******************************************************************************
 * The scene
 ******************************************************************************/
static void
get_interface(const size_t itri, struct sdis_interface** interf, void* ctx)
{
  (void)itri;
  ASSERT(interf && ctx);
  *interf = ctx;
}

static struct sdis_scene*
create_scene
  (struct sdis_device* sdis,
   struct sdis_interface* interf)
{
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_scene* scn = NULL;

  scn_args.get_indices = box_get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = box_get_position;
  scn_args.nprimitives = box_ntriangles;
  scn_args.nvertices = box_nvertices;
  scn_args.context = interf;
  scn_args.fp_to_meter = FP_TO_METER;
  OK(sdis_scene_create(sdis, &scn_args, &scn));

  return scn;
}

/*******************************************************************************
 * Validations
 ******************************************************************************/
static void
check_probe
  (struct sdis_scene* scn,
   const struct reference* ref,
   const enum sdis_diffusion_algorithm diff_algo)
{
  struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  CHK(ref);

  args.position[0] = ref->pos[0];
  args.position[1] = ref->pos[1];
  args.position[2] = ref->pos[2];
  args.time_range[0] =
  args.time_range[1] = ref->time; /* [s] */
  args.diff_algo = diff_algo;
  args.nrealisations = NREALISATIONS;
  OK(sdis_solve_probe(scn, &args, &estimator));
  OK(sdis_estimator_get_temperature(estimator, &T));

  printf("T(%g, %g, %g) at %g s = %g ~ %g +/- %g\n",
    ref->pos[0]*FP_TO_METER, ref->pos[1]*FP_TO_METER, ref->pos[2]*FP_TO_METER,
    ref->time, ref->temp, T.E, T.SE);
  CHK(eq_eps(ref->temp, T.E, 3*T.SE));

  OK(sdis_estimator_ref_put(estimator));
}

static void
check
  (struct sdis_scene* scn,
   const enum sdis_diffusion_algorithm diff_algo)
{
  const char* str_algo = NULL;
  size_t i = 0;

  switch(diff_algo) {
    case SDIS_DIFFUSION_WOS: str_algo = "Walk on Sphere"; break;
    case SDIS_DIFFUSION_DELTA_SPHERE: str_algo = "Delta sphere"; break;
    default: FATAL("Unreachable code.\n"); break;
  }

  printf("\n\x1b[1m\x1b[35m%s\x1b[0m\n", str_algo);
  FOR_EACH(i, 0, nreferences) {
    check_probe(scn, references + i, diff_algo);
  }
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct sdis_device* sdis = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* dummy = NULL;
  struct sdis_scene* scene = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &sdis));

  solid = create_solid(sdis);
  dummy = create_dummy(sdis);
  interf = create_interface(sdis, solid, dummy);
  scene = create_scene(sdis, interf);

  check(scene, SDIS_DIFFUSION_DELTA_SPHERE);
  check(scene, SDIS_DIFFUSION_WOS);

  OK(sdis_interface_ref_put(interf));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_scene_ref_put(scene));
  OK(sdis_device_ref_put(sdis));

  CHK(mem_allocated_size() == 0);
  return 0;
}
