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

#include <rsys/clock_time.h>
#include <rsys/mem_allocator.h>
#include <rsys/double3.h>
#include <rsys_math.h>
#include <star/ssp.h>

/*
 * The physical configuration is the following: a slab of fluid with known
 * thermophysical properties but unknown temperature is located between a
 * "ground" and a slab of solid, with also a unknown temperature profile. On
 * the other side of the solid slab, is an "atmosphere" with known temperature,
 * and known radiative temperature.
 *
 * Solving the system means: finding the temperature of the ground, of the
 * fluid, of the boundaries, and also the temperature inside the solid, at
 * various locations (the 1D slab is discretized in order to obtain the
 * reference)
 *
 * The reference for this system comes from a numerical method and is not
 * analytic. Thus the compliance test MC VS reference is not the usual |MC -
 * ref| <= 3*sigma but is |MC -ref| <= (Tmax -Tmin) * 0.01.
 *
 *          3D                                      2D
 *
 *     ///////////////                         ///////////////
 *     +-----------+-+                         +-----------+-+
 *    /'          / /|                         |           | |
 *   +-----------+-+ | HA  _\  <---- TR        |     _\    | | HA  _\  <---- TR
 *   | |    _\   | | |    / /  <---- TR      TG| HG / / HC | |    / /  <---- TR
 *   | |HG / / HC| | | TA \__/ <---- TR        |    \__/   | | TA \__/ <---- TR
 * TG| |   \__/  | | |                         |           | |
 *   | +.........|.|.+                         +-----------+-+
 *   |,          |/|/                          /////H///////E/
 *   +-----------+-+
 *   //////H//////E/
 */

#define XH 3
#define XHpE 3.2
#define XE (XHpE - XH)

#define T0_SOLID 300
#define T0_FLUID 300

#define N 10000 /* #realisations */

#define TG 310
#define HG 400

#define HC 400

#define TA 290
#define HA 400
#define TR 260

#define TMAX (MMAX(MMAX(MMAX(T0_FLUID, T0_SOLID), MMAX(TG, TA)), TR))
#define TMIN (MMIN(MMIN(MMIN(T0_FLUID, T0_SOLID), MMIN(TG, TA)), TR))
#define EPS ((TMAX-TMIN)*0.01)

/* hr = 4.0 * BOLTZMANN_CONSTANT * Tref * Tref * Tref * epsilon
 * Tref = (hr / (4 * 5.6696e-8 * epsilon)) ^ 1/3, hr = 6 */
#define TREF 297.974852286

#define RHO_F 1.3
#define CP_F 1005
#define RHO_S 2400
#define CP_S 800
#define LAMBDA 0.6

#define X_PROBE (XH + 0.2 * XE)

#define DELTA (XE/40.0)

/*******************************************************************************
 * Box geometry
 ******************************************************************************/
static const double model3d_vertices[12/*#vertices*/*3/*#coords per vertex*/] = {
  0, 0, 0,
  XH, 0, 0,
  XHpE, 0, 0,
  0, XHpE, 0,
  XH, XHpE, 0,
  XHpE, XHpE, 0,
  0, 0, XHpE,
  XH, 0, XHpE,
  XHpE, 0, XHpE,
  0, XHpE, XHpE,
  XH, XHpE, XHpE,
  XHpE, XHpE, XHpE
};
static const size_t model3d_nvertices = sizeof(model3d_vertices)/(sizeof(double)*3);

/* The following array lists the indices toward the 3D vertices of each
 * triangle.
 *        ,3---,4---,5          ,3----4----5        ,4
 *      ,' | ,' | ,'/|        ,'/| \  | \  |      ,'/|
 *    9----10---11 / |      9' / |  \ |  \ |    10 / |       Y
 *    |',  |',  | / ,2      | / ,0---,1---,2    | / ,1       |
 *    |  ',|  ',|/,'        |/,' | ,' | ,'      |/,'         o--X
 *    6----7----8'          6----7'---8'        7           /
 *  Front, right         Back, left and       Internal     Z
 * and Top faces          bottom faces         face */
static const size_t model3d_indices[22/*#triangles*/*3/*#indices per triangle*/] = {
  0, 3, 1, 1, 3, 4,     1, 4, 2, 2, 4, 5,    /* -Z */
  0, 6, 3, 3, 6, 9,                          /* -X */
  6, 7, 9, 9, 7, 10,    7, 8, 10, 10, 8, 11, /* +Z */
  5, 11, 8, 8, 2, 5,                         /* +X */
  3, 9, 10, 10, 4, 3,   4, 10, 11, 11, 5, 4, /* +Y */
  0, 1, 7, 7, 6, 0,     1, 2, 8, 8, 7, 1,    /* -Y */
  4, 10, 7, 7, 1, 4                          /* Inside */
};
static const size_t model3d_ntriangles = sizeof(model3d_indices)/(sizeof(size_t)*3);

static INLINE void
model3d_get_indices(const size_t itri, size_t ids[3], void* context)
{
  (void)context;
  CHK(ids);
  CHK(itri < model3d_ntriangles);
  ids[0] = model3d_indices[itri * 3 + 0];
  ids[1] = model3d_indices[itri * 3 + 1];
  ids[2] = model3d_indices[itri * 3 + 2];
}

static INLINE void
model3d_get_position(const size_t ivert, double pos[3], void* context)
{
  (void)context;
  CHK(pos);
  CHK(ivert < model3d_nvertices);
  pos[0] = model3d_vertices[ivert * 3 + 0];
  pos[1] = model3d_vertices[ivert * 3 + 1];
  pos[2] = model3d_vertices[ivert * 3 + 2];
}

static INLINE void
model3d_get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  CHK(itri < model3d_ntriangles);
  *bound = interfaces[itri];
}

/*******************************************************************************
 * Square geometry
 ******************************************************************************/
static const double model2d_vertices[6/*#vertices*/ * 2/*#coords per vertex*/] = {
  XHpE, 0,
  XH, 0,
  0, 0,
  0, XHpE,
  XH, XHpE,
  XHpE, XHpE
};
static const size_t model2d_nvertices = sizeof(model2d_vertices)/(sizeof(double)*2);

static const size_t model2d_indices[7/*#segments*/ * 2/*#indices per segment*/] = {
  0, 1, 1, 2, /* Bottom */
  2, 3,       /* Left */
  3, 4, 4, 5, /* Top */
  5, 0,       /* Right */
  4, 1        /* Inside */
};
static const size_t model2d_nsegments = sizeof(model2d_indices)/(sizeof(size_t)*2);

static INLINE void
model2d_get_indices(const size_t iseg, size_t ids[2], void* context)
{
  (void)context;
  CHK(ids);
  CHK(iseg < model2d_nsegments);
  ids[0] = model2d_indices[iseg * 2 + 0];
  ids[1] = model2d_indices[iseg * 2 + 1];
}

static INLINE void
model2d_get_position(const size_t ivert, double pos[2], void* context)
{
  (void)context;
  CHK(pos);
  CHK(ivert < model2d_nvertices);
  pos[0] = model2d_vertices[ivert * 2 + 0];
  pos[1] = model2d_vertices[ivert * 2 + 1];
}

static INLINE void
model2d_get_interface
(const size_t iseg, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  CHK(iseg < model2d_nsegments);
  *bound = interfaces[iseg];
}

/*******************************************************************************
 * Media
 ******************************************************************************/
struct solid {
  double lambda;
  double rho;
  double cp;
  double delta;
  double temperature;
  double t0;
};

static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  struct solid* solid;
  CHK(vtx && data);
  solid = ((struct solid*)sdis_data_cget(data));
  return solid->cp;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  struct solid* solid;
  CHK(vtx && data);
  solid = ((struct solid*)sdis_data_cget(data));
  return solid->lambda;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  struct solid* solid;
  CHK(vtx && data);
  solid = ((struct solid*)sdis_data_cget(data));
  return solid->rho;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  struct solid* solid;
  CHK(vtx && data);
  solid = ((struct solid*)sdis_data_cget(data));
  return solid->delta;
}

static double
solid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  struct solid* solid;
  CHK(vtx && data);
  solid = ((struct solid*)sdis_data_cget(data));
  if(vtx->time <= solid->t0)
    return solid->temperature;
  return SDIS_TEMPERATURE_NONE;
}

struct fluid {
  double rho;
  double cp;
  double t0;
  double temperature;
};

static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  struct fluid* fluid;
  CHK(vtx && data);
  fluid = ((struct fluid*)sdis_data_cget(data));
  if(vtx->time <= fluid->t0)
    return fluid->temperature;
  return SDIS_TEMPERATURE_NONE;
}

static double
fluid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  struct fluid* fluid;
  CHK(vtx && data);
  fluid = ((struct fluid*)sdis_data_cget(data));
  return fluid->rho;
}

static double
fluid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  struct fluid* fluid;
  CHK(vtx && data);
  fluid = ((struct fluid*)sdis_data_cget(data));
  return fluid->cp;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/
struct interf {
  double temperature;
  double emissivity;
  double h;
  double Tref;
};

static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf;
  CHK(frag && data);
  interf = sdis_data_cget(data);
  return interf->temperature;
}

static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  const struct interf* interf;
  CHK(frag && data);
  interf = sdis_data_cget(data);
  return interf->h;
}

static double
interface_get_emissivity
  (const struct sdis_interface_fragment* frag,
   const unsigned source_id,
   struct sdis_data* data)
{
  const struct interf* interf;
  (void)source_id;
  CHK(frag && data);
  interf = sdis_data_cget(data);
  return interf->emissivity;
}

static double
interface_get_Tref
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  const struct interf* interf;
  CHK(frag && data);
  interf = sdis_data_cget(data);
  return interf->Tref;
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
  return TR; /* [K] */
}

static double
radenv_get_reference_temperature
  (const struct sdis_radiative_ray* ray,
   struct sdis_data* data)
{
  (void)ray, (void)data;
  return TR; /* [K] */
}

static struct sdis_radiative_env*
create_radenv(struct sdis_device* sdis)
{
  struct sdis_radiative_env_shader shader = SDIS_RADIATIVE_ENV_SHADER_NULL;
  struct sdis_radiative_env* radenv = NULL;

  shader.temperature = radenv_get_temperature;
  shader.reference_temperature = radenv_get_reference_temperature;
  OK(sdis_radiative_env_create(sdis, &shader, NULL, &radenv));
  return radenv;
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
create_interface
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const struct interf* interf,
   struct sdis_interface** out_interf)
{
  struct sdis_interface_shader shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_data* data = NULL;

  CHK(interf != NULL);

  shader.front.temperature = interface_get_temperature;
  shader.back.temperature = interface_get_temperature;
  if(sdis_medium_get_type(front) != sdis_medium_get_type(back)) {
    shader.convection_coef = interface_get_convection_coef;
    shader.convection_coef_upper_bound = interf->h;
  }
  if(sdis_medium_get_type(front) == SDIS_FLUID) {
    shader.front.emissivity = interface_get_emissivity;
    shader.front.reference_temperature = interface_get_Tref;
  }
  if(sdis_medium_get_type(back) == SDIS_FLUID) {
    shader.back.emissivity = interface_get_emissivity;
    shader.back.reference_temperature = interface_get_Tref;
  }

  OK(sdis_data_create(dev, sizeof(struct interf), ALIGNOF(struct interf),
    NULL, &data));
  *((struct interf*)sdis_data_get(data)) = *interf;

  OK(sdis_interface_create(dev, front, back, &shader, data, out_interf));
  OK(sdis_data_ref_put(data));
}

static void
solve_tbound1
  (struct sdis_scene* scn,
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
  enum sdis_scene_dimension dim;
  const double t[] = { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
  const double ref[sizeof(t) / sizeof(*t)] = {
    290.046375, 289.903935, 289.840490, 289.802690, 289.777215, 289.759034,
    289.745710, 289.735826, 289.728448, 289.722921
  };
  const int nsimuls = sizeof(t) / sizeof(*t);
  int isimul;
  ASSERT(scn && rng);

  OK(sdis_scene_get_dimension(scn, &dim));

  solve_args.nrealisations = N;
  solve_args.side = SDIS_FRONT;
  FOR_EACH(isimul, 0, nsimuls) {
    solve_args.time_range[0] = solve_args.time_range[1] = t[isimul];
    if(dim == SDIS_SCENE_2D) {
      solve_args.iprim = model2d_nsegments - 1;
      solve_args.uv[0] = ssp_rng_uniform_double(rng, 0, 1);
    } else {
      double u = ssp_rng_uniform_double(rng, 0, 1);
      double v = ssp_rng_uniform_double(rng, 0, 1);
      double w = ssp_rng_uniform_double(rng, 0, 1);
      double x = 1 / (u + v + w);
      solve_args.iprim = (isimul % 2) ? 10 : 11; /* +X face */
      solve_args.uv[0] = u * x;
      solve_args.uv[1] = v * x;
    }

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
        printf("Unstationary temperature at (%lu/%g) at t=%g = %g ~ %g +/- %g\n",
          (unsigned long)solve_args.iprim, solve_args.uv[0], t[isimul],
          ref[isimul], T.E, T.SE);
        break;
      case SDIS_SCENE_3D:
        printf("Unstationary temperature at (%lu/%g,%g) at t=%g = %g ~ %g +/- %g\n",
          (unsigned long)solve_args.iprim, SPLIT2(solve_args.uv), t[isimul],
          ref[isimul], T.E, T.SE);
        break;
      default: FATAL("Unreachable code.\n"); break;
    }
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    printf("Elapsed time = %s\n", dump);
    printf("Time per realisation (in usec) = %g +/- %g\n\n", time.E, time.SE);

    CHK(eq_eps(T.E, ref[isimul], EPS));
    /*CHK(eq_eps(T.E, ref[isimul], T.SE*3));*/

    OK(sdis_estimator_ref_put(estimator));
  }
}

static void
solve_tbound2
  (struct sdis_scene* scn,
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
  enum sdis_scene_dimension dim;
  const double t[] = { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
  const double ref[sizeof(t) / sizeof(*t)] = {
    309.08032, 309.34626, 309.46525, 309.53625, 309.58408, 309.618121,
    309.642928, 309.661167, 309.674614, 309.684524
  };
  const int nsimuls = sizeof(t) / sizeof(*t);
  int isimul;
  ASSERT(scn && rng);

  OK(sdis_scene_get_dimension(scn, &dim));

  solve_args.nrealisations = N;
  solve_args.side = SDIS_FRONT;
  FOR_EACH(isimul, 0, nsimuls) {
    solve_args.time_range[0] = solve_args.time_range[1] = t[isimul];
    if(dim == SDIS_SCENE_2D) {
      solve_args.iprim = model2d_nsegments - 1;
      solve_args.uv[0] = ssp_rng_uniform_double(rng, 0, 1);
    } else {
      double u = ssp_rng_uniform_double(rng, 0, 1);
      double v = ssp_rng_uniform_double(rng, 0, 1);
      double w = ssp_rng_uniform_double(rng, 0, 1);
      double x = 1 / (u + v + w);
      solve_args.iprim = (isimul % 2) ? 20 : 21; /* Internal face */
      solve_args.uv[0] = u * x;
      solve_args.uv[1] = v * x;
    }

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
        printf("Unstationary temperature at (%lu/%g) at t=%g = %g ~ %g +/- %g\n",
          (unsigned long)solve_args.iprim, solve_args.uv[0], t[isimul],
          ref[isimul], T.E, T.SE);
        break;
      case SDIS_SCENE_3D:
        printf("Unstationary temperature at (%lu/%g,%g) at t=%g = %g ~ %g +/- %g\n",
          (unsigned long)solve_args.iprim, SPLIT2(solve_args.uv), t[isimul],
          ref[isimul], T.E, T.SE);
        break;
      default: FATAL("Unreachable code.\n"); break;
    }
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    printf("Elapsed time = %s\n", dump);
    printf("Time per realisation (in usec) = %g +/- %g\n\n", time.E, time.SE);

    CHK(nfails + nreals == N);
    CHK(nfails <= N/1000);
    CHK(eq_eps(T.E, ref[isimul], EPS));
    /*CHK(eq_eps(T.E, ref[isimul], T.SE*3));*/

    OK(sdis_estimator_ref_put(estimator));
  }
}

static void
solve_tsolid
  (struct sdis_scene* scn,
   struct ssp_rng* rng)
{
  char dump[128];
  struct time t0, t1;
  struct sdis_estimator* estimator;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  size_t nreals;
  size_t nfails;
  enum sdis_scene_dimension dim;
  const double t[] = { 0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
  const double ref[sizeof(t) / sizeof(*t)] = {
    300, 300.87408, 302.25832, 303.22164, 303.89954, 304.39030, 304.75041,
    305.01595, 305.21193, 305.35641, 305.46271
  };
  const int nsimuls = sizeof(t) / sizeof(*t);
  int isimul;
  ASSERT(scn && rng);

  OK(sdis_scene_get_dimension(scn, &dim));

  solve_args.nrealisations = N;
  FOR_EACH(isimul, 0, nsimuls) {
    solve_args.time_range[0] = solve_args.time_range[1] = t[isimul];
    solve_args.position[0] = X_PROBE;
    solve_args.position[1] = ssp_rng_uniform_double(rng, 0.1*XHpE, 0.9*XHpE);

    if(dim == SDIS_SCENE_3D)
      solve_args.position[2] = ssp_rng_uniform_double(rng, 0.1*XHpE, 0.9*XHpE);

    time_current(&t0);
    OK(sdis_solve_probe(scn, &solve_args, &estimator));
    time_sub(&t0, time_current(&t1), &t0);
    time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));

    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    switch (dim) {
    case SDIS_SCENE_2D:
      printf("Unstationary temperature at (%g,%g) at t=%g = %g ~ %g +/- %g\n",
        SPLIT2(solve_args.position), t[isimul], ref[isimul], T.E, T.SE);
      break;
    case SDIS_SCENE_3D:
      printf("Unstationary temperature at (%g,%g,%g) at t=%g = %g ~ %g +/- %g\n",
        SPLIT3(solve_args.position), t[isimul], ref[isimul], T.E, T.SE);
      break;
    default: FATAL("Unreachable code.\n"); break;
    }
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    printf("Elapsed time = %s\n", dump);
    printf("Time per realisation (in usec) = %g +/- %g\n\n", time.E, time.SE);

    CHK(nfails + nreals == N);
    CHK(nfails <= N / 1000);
    CHK(eq_eps(T.E, ref[isimul], EPS));
    /*CHK(eq_eps(T.E, ref[isimul], T.SE*3));*/

    OK(sdis_estimator_ref_put(estimator));
  }
}

static void
solve_tfluid
  (struct sdis_scene* scn)
{
  char dump[128];
  struct time t0, t1;
  struct sdis_estimator* estimator;
  struct sdis_solve_probe_args solve_args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  size_t nreals;
  size_t nfails;
  enum sdis_scene_dimension dim;
  double eps;
  const double t[] = { 0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
  const double ref[sizeof(t) / sizeof(*t)] = {
    300, 309.53905, 309.67273, 309.73241, 309.76798, 309.79194, 309.80899,
    309.82141, 309.83055, 309.83728, 309.84224
  };
  const int nsimuls = sizeof(t) / sizeof(*t);
  int isimul;
  ASSERT(scn);

  OK(sdis_scene_get_dimension(scn, &dim));

  solve_args.nrealisations = N;
  solve_args.position[0] = XH * 0.5;
  solve_args.position[1] = XH * 0.5;
  solve_args.position[2] = XH * 0.5;
  FOR_EACH(isimul, 0, nsimuls) {
    solve_args.time_range[0] = solve_args.time_range[1] = t[isimul];

    time_current(&t0);
    OK(sdis_solve_probe(scn, &solve_args, &estimator));
    time_sub(&t0, time_current(&t1), &t0);
    time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));

    OK(sdis_estimator_get_realisation_count(estimator, &nreals));
    OK(sdis_estimator_get_failure_count(estimator, &nfails));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    switch (dim) {
    case SDIS_SCENE_2D:
      printf("Unstationary fluid temperature at t=%g = %g ~ %g +/- %g\n",
        t[isimul], ref[isimul], T.E, T.SE);
      break;
    case SDIS_SCENE_3D:
      printf("Unstationary fluid temperature at t=%g = %g ~ %g +/- %g\n",
        t[isimul], ref[isimul], T.E, T.SE);
      break;
    default: FATAL("Unreachable code.\n"); break;
    }
    printf("#failures = %lu/%lu\n", (unsigned long)nfails, (unsigned long)N);
    printf("Elapsed time = %s\n", dump);
    printf("Time per realisation (in usec) = %g +/- %g\n\n", time.E, time.SE);

    CHK(nfails + nreals == N);
    CHK(nfails <= N / 1000);

    eps = EPS;
    CHK(eq_eps(T.E, ref[isimul], eps));

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
  struct sdis_medium* fluid_A = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* dummy_solid = NULL;
  struct sdis_interface* interf_adiabatic_1 = NULL;
  struct sdis_interface* interf_adiabatic_2 = NULL;
  struct sdis_interface* interf_TG = NULL;
  struct sdis_interface* interf_P = NULL;
  struct sdis_interface* interf_TA = NULL;
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_scene* box_scn = NULL;
  struct sdis_scene* square_scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface* model3d_interfaces[22 /*#triangles*/];
  struct sdis_interface* model2d_interfaces[7/*#segments*/];
  struct interf interf_props;
  struct solid* solid_props = NULL;
  struct fluid* fluid_props = NULL;
  struct ssp_rng* rng = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &dev));

  radenv = create_radenv(dev);

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.temperature = solid_get_temperature;

  /* Create the solid media */
  OK(sdis_data_create(dev, sizeof(struct solid), 16, NULL, &data));
  solid_props = sdis_data_get(data);
  solid_props->lambda = LAMBDA;
  solid_props->cp = CP_S;
  solid_props->rho = RHO_S;
  solid_props->delta = DELTA;
  solid_props->t0 = 0;
  solid_props->temperature = T0_SOLID;
  OK(sdis_solid_create(dev, &solid_shader, data, &solid));
  OK(sdis_data_ref_put(data));

  /* Create a dummy solid media to be used outside the model */
  OK(sdis_data_create(dev, sizeof(struct solid), 16, NULL, &data));
  solid_props = sdis_data_get(data);
  solid_props->lambda = 0;
  solid_props->cp = 1;
  solid_props->rho = 1;
  solid_props->delta = 1;
  solid_props->t0 = INF;
  solid_props->temperature = SDIS_TEMPERATURE_NONE;
  OK(sdis_solid_create(dev, &solid_shader, data, &dummy_solid));
  OK(sdis_data_ref_put(data));

  /* Setup the fluid shader */
  fluid_shader.calorific_capacity = fluid_get_calorific_capacity;
  fluid_shader.volumic_mass = fluid_get_volumic_mass;
  fluid_shader.temperature = fluid_get_temperature;

  /* Create the internal fluid media */
  OK(sdis_data_create(dev, sizeof(struct fluid), 16, NULL, &data));
  fluid_props = sdis_data_get(data);
  fluid_props->cp = CP_F;
  fluid_props->rho = RHO_F;
  fluid_props->t0 = 0;
  fluid_props->temperature = T0_FLUID;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid));
  OK(sdis_data_ref_put(data));

  /* Create the 'A' fluid media */
  OK(sdis_data_create(dev, sizeof(struct fluid), 16, NULL, &data));
  fluid_props = sdis_data_get(data);
  fluid_props->cp = 1;
  fluid_props->rho = 1;
  fluid_props->t0 = INF;
  fluid_props->temperature = TA;
  OK(sdis_fluid_create(dev, &fluid_shader, data, &fluid_A));
  OK(sdis_data_ref_put(data));

  /* Create the adiabatic interfaces */
  interf_props.temperature = SDIS_TEMPERATURE_NONE;
  interf_props.h = 0;
  interf_props.emissivity = 0;
  interf_props.Tref = TREF;
  create_interface(dev, fluid, dummy_solid, &interf_props, &interf_adiabatic_1);
  create_interface(dev, solid, dummy_solid, &interf_props, &interf_adiabatic_2);

  /* Create the P interface */
  interf_props.temperature = SDIS_TEMPERATURE_NONE;
  interf_props.h = HC;
  interf_props.emissivity = 1;
  interf_props.Tref = TREF;
  create_interface(dev, fluid, solid, &interf_props, &interf_P);

  /* Create the TG interface */
  interf_props.temperature = TG;
  interf_props.h = HG;
  interf_props.emissivity = 1;
  interf_props.Tref = TG;
  create_interface(dev, fluid, dummy_solid, &interf_props, &interf_TG);

  /* Create the TA interface */
  interf_props.temperature = SDIS_TEMPERATURE_NONE;
  interf_props.h = HA;
  interf_props.emissivity = 1;
  interf_props.Tref = TREF;
  create_interface(dev, solid, fluid_A, &interf_props, &interf_TA);

  /* Release the media */
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy_solid));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(fluid_A));

  /* Front */
  model3d_interfaces[0] = interf_adiabatic_1;
  model3d_interfaces[1] = interf_adiabatic_1;
  model3d_interfaces[2] = interf_adiabatic_2;
  model3d_interfaces[3] = interf_adiabatic_2;
  /* Left */
  model3d_interfaces[4] = interf_TG;
  model3d_interfaces[5] = interf_TG;
  /* Back */
  model3d_interfaces[6] = interf_adiabatic_1;
  model3d_interfaces[7] = interf_adiabatic_1;
  model3d_interfaces[8] = interf_adiabatic_2;
  model3d_interfaces[9] = interf_adiabatic_2;
  /* Right */
  model3d_interfaces[10] = interf_TA;
  model3d_interfaces[11] = interf_TA;
  /* Top */
  model3d_interfaces[12] = interf_adiabatic_1;
  model3d_interfaces[13] = interf_adiabatic_1;
  model3d_interfaces[14] = interf_adiabatic_2;
  model3d_interfaces[15] = interf_adiabatic_2;
  /* Bottom */
  model3d_interfaces[16] = interf_adiabatic_1;
  model3d_interfaces[17] = interf_adiabatic_1;
  model3d_interfaces[18] = interf_adiabatic_2;
  model3d_interfaces[19] = interf_adiabatic_2;
  /* Inside */
  model3d_interfaces[20] = interf_P;
  model3d_interfaces[21] = interf_P;

  /* Bottom */
  model2d_interfaces[0] = interf_adiabatic_2;
  model2d_interfaces[1] = interf_adiabatic_1;
  /* Left */
  model2d_interfaces[2] = interf_TG;
  /* Top */
  model2d_interfaces[3] = interf_adiabatic_1;
  model2d_interfaces[4] = interf_adiabatic_2;
  /* Right */
  model2d_interfaces[5] = interf_TA;
  /* Contact */
  model2d_interfaces[6] = interf_P;

  /* Create the box scene */
  scn_args.get_indices = model3d_get_indices;
  scn_args.get_interface = model3d_get_interface;
  scn_args.get_position = model3d_get_position;
  scn_args.nprimitives = model3d_ntriangles;
  scn_args.nvertices = model3d_nvertices;
  scn_args.context = model3d_interfaces;
  scn_args.radenv = radenv;
  scn_args.t_range[0] = MMIN(MMIN(MMIN(MMIN(T0_FLUID, T0_SOLID), TA), TG), TR);
  scn_args.t_range[1] = MMAX(MMAX(MMAX(MMAX(T0_FLUID, T0_SOLID), TA), TG), TR);
  OK(sdis_scene_create(dev, &scn_args, &box_scn));

  /* Create the square scene */
  scn_args.get_indices = model2d_get_indices;
  scn_args.get_interface = model2d_get_interface;
  scn_args.get_position = model2d_get_position;
  scn_args.nprimitives = model2d_nsegments;
  scn_args.nvertices = model2d_nvertices;
  scn_args.context = model2d_interfaces;
  scn_args.radenv = radenv;
  scn_args.t_range[0] = MMIN(MMIN(MMIN(MMIN(T0_FLUID, T0_SOLID), TA), TG), TR);
  scn_args.t_range[1] = MMAX(MMAX(MMAX(MMAX(T0_FLUID, T0_SOLID), TA), TG), TR);
  OK(sdis_scene_2d_create(dev, &scn_args, &square_scn));

  /* Release the interfaces */
  OK(sdis_interface_ref_put(interf_adiabatic_1));
  OK(sdis_interface_ref_put(interf_adiabatic_2));
  OK(sdis_interface_ref_put(interf_TG));
  OK(sdis_interface_ref_put(interf_P));
  OK(sdis_interface_ref_put(interf_TA));

  /* Solve */
  OK(ssp_rng_create(NULL, SSP_RNG_KISS, &rng));
  printf(">> Box scene\n");
  solve_tfluid(box_scn);
  solve_tbound1(box_scn, rng);
  solve_tbound2(box_scn, rng);
  solve_tsolid(box_scn, rng);
  printf("\n>> Square scene\n");
  solve_tfluid(square_scn);
  solve_tbound1(box_scn, rng);
  solve_tbound2(box_scn, rng);
  solve_tsolid(square_scn, rng);

  OK(sdis_radiative_env_ref_put(radenv));
  OK(sdis_scene_ref_put(box_scn));
  OK(sdis_scene_ref_put(square_scn));
  OK(sdis_device_ref_put(dev));
  OK(ssp_rng_ref_put(rng));

  CHK(mem_allocated_size() == 0);
  return 0;
}
