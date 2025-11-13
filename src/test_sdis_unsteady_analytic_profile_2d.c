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

#include <rsys/mem_allocator.h>
#include <rsys/stretchy_array.h>

/*
 * The system is an unsteady-state temperature profile, meaning that at any
 * point, at any: time, we can analytically calculate the temperature. We
 * immerse in this temperature field a supershape representing a solid in which
 * we want to evaluate the temperature by Monte Carlo at a given position and
 * observation time. On the Monte Carlo side, the temperature of the supershape
 * is unknown. Only the boundary temperature is fixed at the temperature of the
 * unsteady trilinear profile mentioned above. Monte Carlo would then have to
 * find the temperature defined by the unsteady profile.
 *
 *      T(y)             /\ <-- T(x,y,t)
 *       |           ___/  \___
 *       |           \  . T=? /
 *       o--- T(x)   /_  __  _\
 *                     \/  \/
 *
 */

#define LAMBDA 0.1 /* [W/(m.K)] */
#define RHO 25.0 /* [kg/m^3] */
#define CP 2.0 /* [J/K/kg)] */
#define DELTA 1.0/20.0 /* [m/fp_to_meter] */

#define NREALISATIONS 100000

struct super_shape {
  double* positions;
  size_t* indices;
};
#define SUPER_SHAPE_NULL__ {NULL, NULL}
static const struct super_shape SUPER_SHAPE_NULL = SUPER_SHAPE_NULL__;

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
/* Unsteady-state temperature profile. Its corresponding power term is :
 * P(x, y) = A*[12*x^2*y^3 + 5*x^4*y] */
static double temperature(const double pos[2], const double time)
{
  const double kx = PI/4;
  const double ky = PI/4;
  const double alpha = LAMBDA / (RHO*CP); /* Diffusivity */

  const double A = 0; /* No termal source */
  const double B1 = 0;
  const double B2 = 1000;

  double x, y, t;
  double a, b, c;
  double temp;
  ASSERT(pos);

  x = pos[0];
  y = pos[1];
  t = time;

  a = B1*(x*x*x - 3*x*y*y);
  b = B2*sin(kx*x)*sin(ky*y)*exp(-alpha*(kx*kx + ky*ky)*t);
  c = A * x*x*x*x + y*y*y;

  temp = (a + b - c) / LAMBDA;
  return temp;
}

static void
dump_paths
  (FILE* fp,
   struct sdis_scene* scn,
   const enum sdis_diffusion_algorithm diff_algo,
   const double pos[2],
   const double time,
   const size_t npaths)
{
  struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_estimator* estimator = NULL;

  args.nrealisations = npaths;
  args.position[0] = pos[0];
  args.position[1] = pos[1];
  args.time_range[0] = time;
  args.time_range[1] = time;
  args.diff_algo = diff_algo;
  args.register_paths = SDIS_HEAT_PATH_ALL;
  OK(sdis_solve_probe(scn, &args, &estimator));

  dump_heat_paths(fp, estimator);

  OK(sdis_estimator_ref_put(estimator));
}

/*******************************************************************************
 * Solid, i.e. medium of the super shape
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
SOLID_PROP(calorific_capacity, CP) /* [J/K/kg] */
SOLID_PROP(thermal_conductivity, LAMBDA) /* [W/m/K] */
SOLID_PROP(volumic_mass, RHO) /* [kg/m^3] */
SOLID_PROP(delta, DELTA) /* [m/fp_to_meter] */
SOLID_PROP(temperature, SDIS_TEMPERATURE_NONE/*<=> unknown*/) /* [K] */
#undef SOLID_PROP

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
  shader.t0 = -INF;
  OK(sdis_solid_create(sdis, &shader, NULL, &solid));
  return solid;
}

/*******************************************************************************
 * Dummy environment, i.e. environment surrounding the super shape. It is
 * defined only for Stardis compliance: in Stardis, an interface must divide 2
 * media.
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
 * Interface: its temperature is fixed to the unsteady-state profile
 ******************************************************************************/
static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  (void)data;
  return temperature(frag->P, frag->time);
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
 * Super shape
 ******************************************************************************/
static struct super_shape
create_super_shape(void)
{
  struct super_shape sshape = SUPER_SHAPE_NULL;

  const unsigned nslices = 128;
  const double a = 1.0;
  const double b = 1.0;
  const double n1 = 1.0;
  const double n2 = 1.0;
  const double n3 = 1.0;
  const double m = 6.0;
  size_t i = 0;

  FOR_EACH(i, 0, nslices) {
    const double theta = (double)i * (2.0*PI / (double)nslices);
    const double tmp0 = pow(fabs(1.0/a * cos(m/4.0*theta)), n2);
    const double tmp1 = pow(fabs(1.0/b * sin(m/4.0*theta)), n3);
    const double tmp2 = pow(tmp0 + tmp1, 1.0/n1);
    const double r = 1.0 / tmp2;
    const double x = cos(theta) * r;
    const double y = sin(theta) * r;
    const size_t j = (i + 1) % nslices;

    sa_push(sshape.positions, x);
    sa_push(sshape.positions, y);
    sa_push(sshape.indices, i);
    sa_push(sshape.indices, j);
  }

  return sshape;
}

static void
release_super_shape(struct super_shape* sshape)
{
  CHK(sshape);
  sa_release(sshape->positions);
  sa_release(sshape->indices);
}

static INLINE size_t
super_shape_nsegments(const struct super_shape* sshape)
{
  CHK(sshape);
  return sa_size(sshape->indices) / 2/*#indices per segment*/;
}

static INLINE size_t
super_shape_nvertices(const struct super_shape* sshape)
{
  CHK(sshape);
  return sa_size(sshape->positions) / 2/*#coords per vertex*/;
}

/*******************************************************************************
 * Scene, i.e. the system to simulate
 ******************************************************************************/
struct scene_context {
  const struct super_shape* sshape;
  struct sdis_interface* interf;
};
static const struct scene_context SCENE_CONTEXT_NULL = {NULL, NULL};

static void
scene_get_indices(const size_t iseg, size_t ids[2], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(ids && context);
  /* Flip the indices to ensure that the normal points into the super shape */
  ids[0] = (unsigned)context->sshape->indices[iseg*2+1];
  ids[1] = (unsigned)context->sshape->indices[iseg*2+0];
}

static void
scene_get_interface(const size_t iseg, struct sdis_interface** interf, void* ctx)
{
  struct scene_context* context = ctx;
  (void)iseg;
  CHK(interf && context);
  *interf = context->interf;
}

static void
scene_get_position(const size_t ivert, double pos[2], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(pos && context);
  pos[0] = context->sshape->positions[ivert*2+0];
  pos[1] = context->sshape->positions[ivert*2+1];
}

static struct sdis_scene*
create_scene
  (struct sdis_device* sdis,
   const struct super_shape* sshape,
   struct sdis_interface* interf)
{
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct scene_context context = SCENE_CONTEXT_NULL;

  context.interf = interf;
  context.sshape = sshape;

  scn_args.get_indices = scene_get_indices;
  scn_args.get_interface = scene_get_interface;
  scn_args.get_position = scene_get_position;
  scn_args.nprimitives = super_shape_nsegments(sshape);
  scn_args.nvertices = super_shape_nvertices(sshape);
  scn_args.context = &context;
  OK(sdis_scene_2d_create(sdis, &scn_args, &scn));
  return scn;
}

/*******************************************************************************
 * Validations
 ******************************************************************************/
static void
check_probe
  (struct sdis_scene* scn,
   const enum sdis_diffusion_algorithm diff_algo,
   const double pos[2],
   const double time, /* [s] */
   const int green)
{
  struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  double ref = 0;

  args.nrealisations = NREALISATIONS;
  args.position[0] = pos[0];
  args.position[1] = pos[1];
  args.time_range[0] = time;
  args.time_range[1] = time;
  args.diff_algo = diff_algo;

  if(!green) {
    OK(sdis_solve_probe(scn, &args, &estimator));
  } else {
    struct sdis_green_function* greenfn = NULL;

    OK(sdis_solve_probe_green_function(scn, &args, &greenfn));
    OK(sdis_green_function_solve(greenfn, &estimator));
    OK(sdis_green_function_ref_put(greenfn));
  }
  OK(sdis_estimator_get_temperature(estimator, &T));

  ref = temperature(pos, time);
  printf("T(%g, %g, %g) = %g ~ %g +/- %g\n",
    SPLIT2(pos), time, ref, T.E, T.SE);
  CHK(eq_eps(ref, T.E, 3*T.SE));

  OK(sdis_estimator_ref_put(estimator));
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  /* Stardis */
  struct sdis_device* sdis = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* dummy = NULL; /* Medium surrounding the solid */
  struct sdis_scene* scn = NULL;

  /* Miscellaneous */
  FILE* fp = NULL;
  struct super_shape sshape = SUPER_SHAPE_NULL;
  const double pos[3] = {0.2,0.3}; /* [m/fp_to_meter] */
  const double time = 5; /* [s] */
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &sdis));

  sshape = create_super_shape();

  /* Save the super shape geometry for debug and visualisation */
  CHK(fp = fopen("super_shape_2d.obj", "w"));
  dump_segments(fp, sshape.positions, super_shape_nvertices(&sshape),
    sshape.indices, super_shape_nsegments(&sshape));
  CHK(fclose(fp) == 0);

  solid = create_solid(sdis);
  dummy = create_dummy(sdis);
  interf = create_interface(sdis, solid, dummy);
  scn = create_scene(sdis, &sshape, interf);

  check_probe(scn, SDIS_DIFFUSION_DELTA_SPHERE, pos, time, 0/*green*/);
  check_probe(scn, SDIS_DIFFUSION_WOS, pos, time, 0/*green*/);
  check_probe(scn, SDIS_DIFFUSION_WOS, pos, time, 1/*green*/);

  /* Write 10 heat paths sampled by the delta sphere algorithm */
  CHK(fp = fopen("paths_delta_sphere_2d.vtk", "w"));
  dump_paths(fp, scn, SDIS_DIFFUSION_DELTA_SPHERE, pos, time, 10);
  CHK(fclose(fp) == 0);

  /* Write 10 heat paths sampled by the WoS algorithm */
  CHK(fp = fopen("paths_wos_2d.vtk", "w"));
  dump_paths(fp, scn, SDIS_DIFFUSION_WOS, pos, time, 10);
  CHK(fclose(fp) == 0);

  release_super_shape(&sshape);
  OK(sdis_device_ref_put(sdis));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_scene_ref_put(scn));

  CHK(mem_allocated_size() == 0);
  return 0;
}
