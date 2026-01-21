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
#include "test_sdis_mesh.h"

#include <star/s3dut.h>
#include <rsys_math.h>

#include <rsys_math.h>

/*
 * The system is a trilinear profile of the temperature at steady state, i.e. at
 * each point of the system we can calculate the temperature analytically. Two
 * forms are immersed in this temperature field: a super shape and a sphere
 * included in the super shape. On the Monte Carlo side, the temperature is
 * unknown everywhere  except on the surface of the super shape whose
 * temperature is defined from the aformentionned trilinear profile. We will
 * estimate the temperature at the sphere boundary at several probe points. We
 * should find by Monte Carlo the temperature of the trilinear profile at the
 * position of the probe. It's the test.
 *
 *                       /\ <-- T(x,y,z)
 *                   ___/  \___
 *      T(z)         \   __   /
 *       |  T(y)      \ /  \ /
 *       |/         T=? *__/ \
 *       o--- T(x)   /_  __  _\
 *                     \/  \/
 */

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static double
trilinear_profile(const double pos[3])
{
  /* Range in X, Y and Z in which the trilinear profile is defined */
  const double lower = -4;
  const double upper = +4;

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

static void
sample_sphere(double pos[3])
{
  const double phi = rand_canonic() * 2 * PI;
  const double v = rand_canonic();
  const double cos_theta = 1 - 2 * v;
  const double sin_theta = 2 * sqrt(v * (1 - v));
  pos[0] = cos(phi) * sin_theta;
  pos[1] = sin(phi) * sin_theta;
  pos[2] = cos_theta;
}

static INLINE void
check_intersection
  (const double val0,
   const double eps0,
   const double val1,
   const double eps1)
{
  double interval0[2], interval1[2];
  double intersection[2];
  interval0[0] = val0 - eps0;
  interval0[1] = val0 + eps0;
  interval1[0] = val1 - eps1;
  interval1[1] = val1 + eps1;
  intersection[0] = MMAX(interval0[0], interval1[0]);
  intersection[1] = MMIN(interval0[1], interval1[1]);
  CHK(intersection[0] <= intersection[1]);
}

static void
mesh_add_super_shape(struct mesh* mesh)
{
  struct s3dut_mesh* sshape = NULL;
  struct s3dut_mesh_data sshape_data;
  struct s3dut_super_formula f0 = S3DUT_SUPER_FORMULA_NULL;
  struct s3dut_super_formula f1 = S3DUT_SUPER_FORMULA_NULL;
  const double radius = 2;
  const unsigned nslices = 256;

  f0.A = 1.5; f0.B = 1; f0.M = 11.0; f0.N0 = 1; f0.N1 = 1; f0.N2 = 2.0;
  f1.A = 1.0; f1.B = 2; f1.M =  3.6; f1.N0 = 1; f1.N1 = 2; f1.N2 = 0.7;
  OK(s3dut_create_super_shape(NULL, &f0, &f1, radius, nslices, nslices/2, &sshape));
  OK(s3dut_mesh_get_data(sshape, &sshape_data));
  mesh_append(mesh, sshape_data.positions, sshape_data.nvertices,
    sshape_data.indices, sshape_data.nprimitives, NULL);
  OK(s3dut_mesh_ref_put(sshape));
}

static void
mesh_add_sphere(struct mesh* mesh)
{
  struct s3dut_mesh* sphere = NULL;
  struct s3dut_mesh_data sphere_data;
  const double radius = 1;
  const unsigned nslices = 128;

  OK(s3dut_create_sphere(NULL, radius, nslices, nslices/2, &sphere));
  OK(s3dut_mesh_get_data(sphere, &sphere_data));
  mesh_append(mesh, sphere_data.positions, sphere_data.nvertices,
    sphere_data.indices, sphere_data.nprimitives, NULL);
  OK(s3dut_mesh_ref_put(sphere));
}

/*******************************************************************************
 * Solid, i.e. medium of super shape and sphere
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
SOLID_PROP(temperature, SDIS_TEMPERATURE_NONE/*<=> unknown*/) /* [K] */
SOLID_PROP(delta, 1.0/20.0) /* [m] */

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
 * Interfaces
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

  /* Fluid/solid interface: fix the temperature to the temperature profile  */
  if(sdis_medium_get_type(front) != sdis_medium_get_type(back)) {
    shader.front.temperature = interface_get_temperature;
    shader.back.temperature = interface_get_temperature;
  }

  OK(sdis_interface_create(sdis, front, back, &shader, NULL, &interf));
  return interf;
}

/*******************************************************************************
 * Scene, i.e. the system to simulate
 ******************************************************************************/
struct scene_context {
  const struct mesh* mesh;
  size_t sshape_end_id; /* Last triangle index of the super shape */
  struct sdis_interface* sshape;
  struct sdis_interface* sphere;
};
#define SCENE_CONTEXT_NULL__ {NULL, 0, 0, 0}
static const struct scene_context SCENE_CONTEXT_NULL = SCENE_CONTEXT_NULL__;

static void
scene_get_indices(const size_t itri, size_t ids[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(ids && context && itri < mesh_ntriangles(context->mesh));
  /* Flip the indices to ensure that the normal points into the super shape */
  ids[0] = (unsigned)context->mesh->indices[itri*3+0];
  ids[1] = (unsigned)context->mesh->indices[itri*3+2];
  ids[2] = (unsigned)context->mesh->indices[itri*3+1];
}

static void
scene_get_interface(const size_t itri, struct sdis_interface** interf, void* ctx)
{
  struct scene_context* context = ctx;
  CHK(interf && context && itri < mesh_ntriangles(context->mesh));
  if(itri < context->sshape_end_id) {
    *interf = context->sshape;
  } else {
    *interf = context->sphere;
  }
}

static void
scene_get_position(const size_t ivert, double pos[3], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(pos && context && ivert < mesh_nvertices(context->mesh));
  pos[0] = context->mesh->positions[ivert*3+0];
  pos[1] = context->mesh->positions[ivert*3+1];
  pos[2] = context->mesh->positions[ivert*3+2];
}

static struct sdis_scene*
create_scene(struct sdis_device* sdis, struct scene_context* ctx)

{
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;

  scn_args.get_indices = scene_get_indices;
  scn_args.get_interface = scene_get_interface;
  scn_args.get_position = scene_get_position;
  scn_args.nprimitives = mesh_ntriangles(ctx->mesh);
  scn_args.nvertices = mesh_nvertices(ctx->mesh);
  scn_args.context = ctx;
  OK(sdis_scene_create(sdis, &scn_args, &scn));
  return scn;
}

/*******************************************************************************
 * Validations
 ******************************************************************************/
static void
check_probe_boundary_list_api
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_list_args* in_args)
{
  struct sdis_solve_probe_boundary_list_args args = *in_args;
  struct sdis_estimator_buffer* estim_buf = NULL;

  /* Check API */
  BA(sdis_solve_probe_boundary_list(NULL, &args, &estim_buf));
  BA(sdis_solve_probe_boundary_list(scn, NULL, &estim_buf));
  BA(sdis_solve_probe_boundary_list(scn, &args, NULL));
  args.nprobes = 0;
  BA(sdis_solve_probe_boundary_list(scn, &args, &estim_buf));
  args.nprobes = in_args->nprobes;
  args.probes = NULL;
  BA(sdis_solve_probe_boundary_list(scn, &args, &estim_buf));
}

/* Check the estimators against the analytical solution */
static void
check_estimator_buffer
  (const struct sdis_estimator_buffer* estim_buf,
   struct sdis_scene* scn,
   const struct sdis_solve_probe_boundary_list_args* args)
{
  struct sdis_mc T = SDIS_MC_NULL;
  size_t iprobe = 0;

  /* Variables used to check the global estimation results */
  size_t total_nrealisations = 0;
  size_t total_nrealisations_ref = 0;
  size_t total_nfailures = 0;
  size_t total_nfailures_ref = 0;
  double sum = 0; /* Global sum of weights */
  double sum2 = 0; /* Global sum of squared weights */
  double N = 0; /* Number of (successful) realisations */
  double E = 0; /* Expected value */
  double V = 0; /* Variance */
  double SE = 0; /* Standard Error */

  /* Check the results */
  FOR_EACH(iprobe, 0, args->nprobes) {
    const struct sdis_estimator* estimator = NULL;
    size_t probe_nrealisations = 0;
    size_t probe_nfailures = 0;
    double pos[3] = {0,0,0};
    double probe_sum = 0;
    double probe_sum2 = 0;
    double ref = 0;

    OK(sdis_scene_get_boundary_position(scn, args->probes[iprobe].iprim,
      args->probes[iprobe].uv, pos));

    /* Fetch result */
    OK(sdis_estimator_buffer_at(estim_buf, iprobe, 0, &estimator));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_count(estimator, &probe_nrealisations));
    OK(sdis_estimator_get_failure_count(estimator, &probe_nfailures));

    /* Check probe estimation */
    ref = trilinear_profile(pos);
    printf("T(%g, %g, %g) = %g ~ %g +/- %g\n", SPLIT3(pos), ref, T.E, T.SE);
    CHK(eq_eps(ref, T.E, 3*T.SE));

    /* Check miscellaneous results */
    CHK(probe_nrealisations == args->probes[iprobe].nrealisations);

    /* Compute the sum of weights and sum of squared weights of the probe */
    probe_sum = T.E * (double)probe_nrealisations;
    probe_sum2 = (T.V + T.E*T.E) * (double)probe_nrealisations;

    /* Update the global estimation */
    total_nrealisations_ref += probe_nrealisations;
    total_nfailures_ref += probe_nfailures;
    sum += probe_sum;
    sum2 += probe_sum2;
  }

  /* Check the overall estimate of the estimate buffer. This estimate is not
   * really a result expected by the caller since the probes are all
   * independent. But to be consistent with the estimate buffer API, we need to
   * provide it. And so we check its validity */
  OK(sdis_estimator_buffer_get_temperature(estim_buf, &T));
  OK(sdis_estimator_buffer_get_realisation_count(estim_buf, &total_nrealisations));
  OK(sdis_estimator_buffer_get_failure_count(estim_buf, &total_nfailures));

  CHK(total_nrealisations == total_nrealisations_ref);
  CHK(total_nfailures == total_nfailures_ref);

  N = (double)total_nrealisations_ref;
  E = sum / N;
  V = sum2 / N - E*E;
  SE = sqrt(V/N);
  check_intersection(E, SE, T.E, T.SE);
}

static void
check_probe_boundary_list(struct sdis_scene* scn, const int is_master_process)
{
  #define NPROBES 10

  /* Estimations */
  struct sdis_estimator_buffer* estim_buf = NULL;

  /* Probe variables */
  struct sdis_solve_probe_boundary_args probes[NPROBES];
  struct sdis_solve_probe_boundary_list_args args =
    SDIS_SOLVE_PROBE_BOUNDARY_LIST_ARGS_DEFAULT;
  size_t iprobe;

  /* Miscellaneous */
  struct sdis_scene_find_closest_point_args closest_pt_args =
    SDIS_SCENE_FIND_CLOSEST_POINT_ARGS_NULL;

  (void)is_master_process;

  /* Setup the list of probes to calculate */
  args.probes = probes;
  args.nprobes = NPROBES;
  FOR_EACH(iprobe, 0, NPROBES) {
    sample_sphere(closest_pt_args.position);
    closest_pt_args.radius = INF;

    probes[iprobe] = SDIS_SOLVE_PROBE_BOUNDARY_ARGS_DEFAULT;
    probes[iprobe].nrealisations = 10000;
    probes[iprobe].side = SDIS_FRONT;

    OK(sdis_scene_find_closest_point
      (scn, &closest_pt_args, &probes[iprobe].iprim, probes[iprobe].uv));
  }

  check_probe_boundary_list_api(scn, &args);

  /* Solve the probes */
  OK(sdis_solve_probe_boundary_list(scn, &args, &estim_buf));
  if(!is_master_process) {
    CHK(estim_buf == NULL);
  } else {
    check_estimator_buffer(estim_buf, scn, &args);
    OK(sdis_estimator_buffer_ref_put(estim_buf));
  }

  #undef NPROBES
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  /* Stardis */
  struct sdis_device* dev = NULL;
  struct sdis_interface* solid_fluid = NULL;
  struct sdis_interface* solid_solid = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_scene* scn = NULL;

  /* Miscellaneous */
  struct scene_context ctx = SCENE_CONTEXT_NULL;
  struct mesh mesh = MESH_NULL;
  size_t sshape_end_id = 0; /* Last index of the super shape */
  int is_master_process = 1;
  (void)argc, (void)argv;

  create_default_device(&argc, &argv, &is_master_process, &dev);

  /* Setup the mesh */
  mesh_init(&mesh);
  mesh_add_super_shape(&mesh);
  sshape_end_id = mesh_ntriangles(&mesh);
  mesh_add_sphere(&mesh);

  /* Setup physical properties */
  fluid = create_dummy(dev);
  solid = create_solid(dev);
  solid_fluid = create_interface(dev, solid, fluid);
  solid_solid = create_interface(dev, solid, solid);

  /* Create the scene */
  ctx.mesh = &mesh;
  ctx.sshape_end_id = sshape_end_id;
  ctx.sshape = solid_fluid;
  ctx.sphere = solid_solid;
  scn = create_scene(dev, &ctx);

  check_probe_boundary_list(scn, is_master_process);

  mesh_release(&mesh);
  OK(sdis_interface_ref_put(solid_fluid));
  OK(sdis_interface_ref_put(solid_solid));
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_scene_ref_put(scn));

  free_default_device(dev);

  CHK(mem_allocated_size() == 0);
  return 0;
}
