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

#include <rsys/float3.h>

#include <star/s3d.h>
#include <star/s3dut.h>

/*
 * The system is a trilinear profile of steady state temperature, i.e. at each
 * point of the system we can analytically calculate the temperature. We immerse
 * a super shape in this temperature field which represents a solid in which we
 * want to evaluate by Monte Carlo the temperature at several positions. On the
 * Monte Carlo side, the temperature of the super shape is unknown. Only the
 * boundary temperature is set to the temperature of the aforementioned
 * trilinear profile. Thus, we should find by Monte Carlo the temperature
 * defined by the trilinear profile.
 *
 *                       /\ <-- T(x,y,z)
 *      T(z)         ___/  \___
 *       |           \  . T=? /
 *       o-- T(x)    /_  __  _\
 *      /              \/  \/
 *   T(y)
 */

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static double
trilinear_profile(const double pos[3])
{
  /* Range in X, Y and Z in which the trilinear profile is defined */
  const double lower = -3;
  const double upper = +3;

  /* Upper temperature limit in X, Y and Z [K]. Lower temperature limit is
   * implicitly 0 */
  const double a = 333; /* Upper temperature limit in X [K] */
  const double b = 432; /* Upper temperature limit in Y [K] */
  const double c = 579; /* Upper temperature limit in Z [K] */

  double x, y, z;

  /* Check pre-conditions */
  CHK(pos);
  CHK(lower <= pos[0] && pos[0] <= upper);
  CHK(lower <= pos[1] && pos[1] <= upper);
  CHK(lower <= pos[2] && pos[2] <= upper);

  x = (pos[0] - lower) / (upper - lower);
  y = (pos[1] - lower) / (upper - lower);
  z = (pos[2] - lower) / (upper - lower);
  return a*x + b*y + c*z;
}

static INLINE float
rand_canonic(void)
{
  return (float)rand() / (float)((unsigned)RAND_MAX+1);
}

/*******************************************************************************
 * Super shape
 ******************************************************************************/
static struct s3dut_mesh*
create_super_shape(void)
{
  struct s3dut_mesh* mesh = NULL;
  struct s3dut_super_formula f0 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_super_formula f1 = S3DUT_SUPER_FORMULA_NULL;
  const double radius = 1;
  const unsigned nslices = 256;

  f0.A = 1.5; f0.B = 1; f0.M = 11.0; f0.N0 = 1; f0.N1 = 1; f0.N2 = 2.0;
  f1.A = 1.0; f1.B = 2; f1.M =  3.6; f1.N0 = 1; f1.N1 = 2; f1.N2 = 0.7;
  OK(s3dut_create_super_shape(NULL, &f0, &f1, radius, nslices, nslices/2, &mesh));

  return mesh;
}

/*******************************************************************************
  * View, i.e. acceleration structure used to query geometry. In this test it is
  * used to calculate the delta parameter and to sample the probe positions in
  * the supershape.
 ******************************************************************************/
static void
view_get_indices(const unsigned itri, unsigned ids[3], void* ctx)
{
  struct s3dut_mesh_data* mesh_data = ctx;
  CHK(ids && mesh_data && itri < mesh_data->nprimitives);
  /* Flip the indices to ensure that the normal points into the super shape */
  ids[0] = (unsigned)mesh_data->indices[itri*3+0];
  ids[1] = (unsigned)mesh_data->indices[itri*3+2];
  ids[2] = (unsigned)mesh_data->indices[itri*3+1];
}

static void
view_get_position(const unsigned ivert, float pos[3], void* ctx)
{
  struct s3dut_mesh_data* mesh_data = ctx;
  CHK(pos && mesh_data && ivert < mesh_data->nvertices);
  pos[0] = (float)mesh_data->positions[ivert*3+0];
  pos[1] = (float)mesh_data->positions[ivert*3+1];
  pos[2] = (float)mesh_data->positions[ivert*3+2];
}

static struct s3d_scene_view*
create_view(struct s3dut_mesh* mesh)
{
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  struct s3d_device* s3d = NULL;
  struct s3d_scene* scn = NULL;
  struct s3d_shape* shape = NULL;
  struct s3d_scene_view* view = NULL;

  struct s3dut_mesh_data mesh_data;

  OK(s3dut_mesh_get_data(mesh, &mesh_data));

  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = view_get_position;
  OK(s3d_device_create(NULL, NULL, 0, &s3d));
  OK(s3d_shape_create_mesh(s3d, &shape));
  OK(s3d_mesh_setup_indexed_vertices(shape, (unsigned)mesh_data.nprimitives,
    view_get_indices, (unsigned)mesh_data.nvertices, &vdata, 1, &mesh_data));
  OK(s3d_scene_create(s3d, &scn));
  OK(s3d_scene_attach_shape(scn, shape));
  OK(s3d_scene_view_create(scn, S3D_TRACE, &view));

  OK(s3d_device_ref_put(s3d));
  OK(s3d_shape_ref_put(shape));
  OK(s3d_scene_ref_put(scn));

  return view;
}

static double
view_compute_delta(struct s3d_scene_view* view)
{
  float S = 0; /* Surface */
  float V = 0; /* Volume */

  OK(s3d_scene_view_compute_area(view, &S));
  OK(s3d_scene_view_compute_volume(view, &V));
  CHK(S > 0 && V > 0);

  return (4.0*V/S)/20.0;
}

static void
view_sample_position
  (struct s3d_scene_view* view,
   const double delta,
   double pos[3])
{
  /* Ray variables */
  const float dir[3] = {0, 1, 0};
  const float range[2] = {0, FLT_MAX};
  float org[3];

  /* View variables */
  float low[3];
  float upp[3];

  OK(s3d_scene_view_get_aabb(view, low, upp));

  /* Sample a position in the supershape by uniformly sampling a position within
   * its bounding box and rejecting it if it is not in the supershape or if it
   * is too close to its boundaries. */
  for(;;) {
    struct s3d_hit hit = S3D_HIT_NULL;
    float N[3] = {0, 0, 0}; /* Normal */

    pos[0] = low[0] + rand_canonic()*(upp[0] - low[0]);
    pos[1] = low[1] + rand_canonic()*(upp[1] - low[1]);
    pos[2] = low[2] + rand_canonic()*(upp[2] - low[2]);

    org[0] = (float)pos[0];
    org[1] = (float)pos[1];
    org[2] = (float)pos[2];
    OK(s3d_scene_view_trace_ray(view, org, dir, range, NULL, &hit));

    if(S3D_HIT_NONE(&hit)) continue;

    f3_normalize(N, hit.normal);
    if(f3_dot(N, dir) > 0) continue;

    OK(s3d_scene_view_closest_point(view, org, (float)INF, NULL, &hit));
    CHK(!S3D_HIT_NONE(&hit));

    /* Sample position is in the super shape and is not too close to its
     * boundaries */
    if(hit.distance > delta) break;
  }
}


/*******************************************************************************
 * Scene, i.e. the system to simulate
 ******************************************************************************/
struct scene_context {
  struct s3dut_mesh_data mesh_data;
  struct sdis_interface* interf;
};
static const struct scene_context SCENE_CONTEXT_NULL = {{0}, NULL};

static void
scene_get_indices(const size_t itri, size_t ids[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(ids && context && itri < context->mesh_data.nprimitives);
  /* Flip the indices to ensure that the normal points into the super shape */
  ids[0] = (unsigned)context->mesh_data.indices[itri*3+0];
  ids[1] = (unsigned)context->mesh_data.indices[itri*3+2];
  ids[2] = (unsigned)context->mesh_data.indices[itri*3+1];
}

static void
scene_get_interface(const size_t itri, struct sdis_interface** interf, void* ctx)
{
  struct scene_context* context = ctx;
  CHK(interf && context && itri < context->mesh_data.nprimitives);
  *interf = context->interf;
}

static void
scene_get_position(const size_t ivert, double pos[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(pos && context && ivert < context->mesh_data.nvertices);
  pos[0] = context->mesh_data.positions[ivert*3+0];
  pos[1] = context->mesh_data.positions[ivert*3+1];
  pos[2] = context->mesh_data.positions[ivert*3+2];
}

static struct sdis_scene*
create_scene
  (struct sdis_device* sdis,
   const struct s3dut_mesh* mesh,
   struct sdis_interface* interf)
{
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct scene_context context = SCENE_CONTEXT_NULL;

  OK(s3dut_mesh_get_data(mesh, &context.mesh_data));
  context.interf = interf;

  scn_args.get_indices = scene_get_indices;
  scn_args.get_interface = scene_get_interface;
  scn_args.get_position = scene_get_position;
  scn_args.nprimitives = context.mesh_data.nprimitives;
  scn_args.nvertices = context.mesh_data.nvertices;
  scn_args.context = &context;
  OK(sdis_scene_create(sdis, &scn_args, &scn));
  return scn;
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
SOLID_PROP(calorific_capacity, 500.0) /* [J/K/Kg] */
SOLID_PROP(thermal_conductivity, 25.0) /* [W/m/K] */
SOLID_PROP(volumic_mass, 7500.0) /* [kg/m^3] */
SOLID_PROP(temperature, -1/*<=> unknown*/) /* [K] */
#undef SOLID_PROP

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx,
   struct sdis_data* data)
{
  const double* delta = sdis_data_get(data);
  (void)vtx; /* Avoid the "unused variable" warning */
  return *delta;
}

static struct sdis_medium*
create_solid(struct sdis_device* sdis, struct s3d_scene_view* view)
{
  struct sdis_solid_shader shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_data* data = NULL;
  double* pdelta = NULL;

  OK(sdis_data_create(sdis, sizeof(double), ALIGNOF(double), NULL, &data));
  pdelta = sdis_data_get(data);
  *pdelta = view_compute_delta(view);

  shader.calorific_capacity = solid_get_calorific_capacity;
  shader.thermal_conductivity = solid_get_thermal_conductivity;
  shader.volumic_mass = solid_get_volumic_mass;
  shader.delta = solid_get_delta;
  shader.temperature = solid_get_temperature;
  OK(sdis_solid_create(sdis, &shader, data, &solid));
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
 * Interface: its temperature is fixed to the trilinear profile
 ******************************************************************************/
static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  (void)data; /* Avoid the "unused variable" warning */
  return trilinear_profile(frag->P);
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
 * Validations
 ******************************************************************************/
static void
check_probe_list(struct sdis_scene* scn, struct s3d_scene_view* view)
{

  struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;

  const size_t nprobes = 10;
  size_t iprobe = 0;
  double ref = 0; /* Analytical reference */
  double delta = 0;

  delta = view_compute_delta(view);
  args.nrealisations = 10000;

  FOR_EACH(iprobe, 0, nprobes) {
    view_sample_position(view, delta, args.position);
    OK(sdis_solve_probe(scn, &args, &estimator));
    OK(sdis_estimator_get_temperature(estimator, &T));

    ref = trilinear_profile(args.position);
    printf("T(%g, %g, %g) = %g ~ %g +/- %g\n",
      SPLIT3(args.position), ref, T.E, T.SE);
    CHK(eq_eps(ref, T.E, 3*T.SE));

    OK(sdis_estimator_ref_put(estimator));
  }
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
  struct s3d_scene_view* view = NULL;
  struct s3dut_mesh* super_shape = NULL;
  int is_master_process = 0;

  (void)argc, (void)argv; /* Avoid the "unused variable" warning */

  create_default_device(&argc, &argv, &is_master_process, &sdis);

  super_shape = create_super_shape();
  view = create_view(super_shape);

  solid = create_solid(sdis, view);
  dummy = create_dummy(sdis);
  interf = create_interface(sdis, solid, dummy);
  scn = create_scene(sdis, super_shape, interf);

  check_probe_list(scn, view);

  OK(s3dut_mesh_ref_put(super_shape));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_scene_ref_put(scn));

  free_default_device(sdis);
  return 0;
}
