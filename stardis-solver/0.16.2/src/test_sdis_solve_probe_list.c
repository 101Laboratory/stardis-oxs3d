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

#include <rsys/float3.h>

#include <star/s3d.h>
#include <star/s3dut.h>
#include <star/ssp.h>

#ifdef SDIS_ENABLE_MPI
  #include <mpi.h>
#endif

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
 *      T(z)             /\ <-- T(x,y,z)
 *       |  T(y)     ___/  \___
 *       |/          \  . T=? /
 *       o--- T(x)   /_  __  _\
 *                     \/  \/
 *
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

  return (4.0*V/S)/30.0;
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
SOLID_PROP(temperature, SDIS_TEMPERATURE_NONE) /* [K] */
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

  OK(sdis_data_ref_put(data));
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
check_probe_list_api
  (struct sdis_scene* scn,
   const struct sdis_solve_probe_list_args* in_args)
{
  struct sdis_solve_probe_list_args args = *in_args;
  struct sdis_estimator_buffer* estim_buf = NULL;

  /* Check API */
  BA(sdis_solve_probe_list(NULL, &args, &estim_buf));
  BA(sdis_solve_probe_list(scn, NULL, &estim_buf));
  BA(sdis_solve_probe_list(scn, &args, NULL));
  args.nprobes = 0;
  BA(sdis_solve_probe_list(scn, &args, &estim_buf));
  args.nprobes = in_args->nprobes;
  args.probes = NULL;
  BA(sdis_solve_probe_list(scn, &args, &estim_buf));
}

/* Check the estimators against the analytical solution */
static void
check_estimator_buffer
  (const struct sdis_estimator_buffer* estim_buf,
   const struct sdis_solve_probe_list_args* args)
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
    double probe_sum = 0;
    double probe_sum2 = 0;
    double ref = 0;

    /* Fetch result */
    OK(sdis_estimator_buffer_at(estim_buf, iprobe, 0, &estimator));
    OK(sdis_estimator_get_temperature(estimator, &T));
    OK(sdis_estimator_get_realisation_count(estimator, &probe_nrealisations));
    OK(sdis_estimator_get_failure_count(estimator, &probe_nfailures));

    /* Check probe estimation */
    ref = trilinear_profile(args->probes[iprobe].position);
    printf("T(%g, %g, %g) = %g ~ %g +/- %g\n",
      SPLIT3(args->probes[iprobe].position), ref, T.E, T.SE);
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

/* Check that the buffers store statistically independent estimates */
static void
check_estimator_buffer_combination
  (const struct sdis_estimator_buffer* estim_buf1,
   const struct sdis_estimator_buffer* estim_buf2,
   const struct sdis_solve_probe_list_args* args)
{
  size_t iprobe;

  /* Check that the 2 series of estimates are compatible but not identical */
  FOR_EACH(iprobe, 0, args->nprobes) {
    const struct sdis_estimator* estimator1 = NULL;
    const struct sdis_estimator* estimator2 = NULL;
    size_t nrealisations1 = 0;
    size_t nrealisations2 = 0;
    struct sdis_mc T1 = SDIS_MC_NULL;
    struct sdis_mc T2 = SDIS_MC_NULL;
    double sum = 0; /* Sum of weights */
    double sum2 = 0; /* Sum of squared weights */
    double E = 0; /* Expected value */
    double V = 0; /* Variance */
    double SE = 0; /* Standard Error */
    double N = 0; /* Number of realisations */
    double ref = 0; /* Analytical solution */

    /* Retrieve the estimation results */
    OK(sdis_estimator_buffer_at(estim_buf1, iprobe, 0, &estimator1));
    OK(sdis_estimator_buffer_at(estim_buf2, iprobe, 0, &estimator2));
    OK(sdis_estimator_get_temperature(estimator1, &T1));
    OK(sdis_estimator_get_temperature(estimator2, &T2));

    /* Estimates are not identical... */
    CHK(T1.E != T2.E);
    CHK(T1.SE != T2.SE);

    /* ... but are compatible ... */
    check_intersection(T1.E, 3*T1.SE, T2.E, 3*T2.SE);

    /* We can combine the 2 estimates since they must be numerically
     * independent. We can therefore verify that their combination is valid with
     * respect to the analytical solution */
    OK(sdis_estimator_get_realisation_count(estimator1, &nrealisations1));
    OK(sdis_estimator_get_realisation_count(estimator2, &nrealisations2));

    sum =
      T1.E*(double)nrealisations1
    + T2.E*(double)nrealisations2;
    sum2 =
      (T1.V + T1.E*T1.E)*(double)nrealisations1
    + (T2.V + T2.E*T2.E)*(double)nrealisations2;
    N = (double)(nrealisations1 + nrealisations2);
    E = sum / N;
    V = sum2 / N - E*E;
    SE = sqrt(V/N);

    ref = trilinear_profile(args->probes[iprobe].position);
    printf("T(%g, %g, %g) = %g ~ %g +/- %g\n",
      SPLIT3(args->probes[iprobe].position), ref, E, SE);
    CHK(eq_eps(ref, E, 3*SE));
  }
}

static void
check_probe_list
  (struct sdis_scene* scn,
   struct s3d_scene_view* view,
   const int is_master_process)
{
  #define NPROBES 10

  /* RNG state */
  const char* rng_state_filename = "rng_state";
  struct ssp_rng* rng = NULL;
  enum ssp_rng_type rng_type = SSP_RNG_TYPE_NULL;

  /* Probe variables */
  struct sdis_solve_probe_args probes[NPROBES];
  struct sdis_solve_probe_list_args args = SDIS_SOLVE_PROBE_LIST_ARGS_DEFAULT;
  size_t iprobe = 0;

  /* Estimations */
  struct sdis_estimator_buffer* estim_buf = NULL;
  struct sdis_estimator_buffer* estim_buf2 = NULL;

  /* Misc */
  double delta = 0;
  FILE* fp = NULL;

  delta = view_compute_delta(view);

  /* Setup the list of probes to calculate */
  args.probes = probes;
  args.nprobes = NPROBES;
  FOR_EACH(iprobe, 0, NPROBES) {
    probes[iprobe] = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
    probes[iprobe].nrealisations = 10000;
    view_sample_position(view, delta, probes[iprobe].position);
  }

  check_probe_list_api(scn, &args);

  /* Solve the probes */
  OK(sdis_solve_probe_list(scn, &args, &estim_buf));

  if(!is_master_process) {
    CHK(estim_buf == NULL);
  } else {
    check_estimator_buffer(estim_buf, &args);

    /* Check the use of a non-default rng_state. Run the calculations using the
     * final RNG state from the previous estimate. Thus, the estimates are
     * statically independent and can be combined. To do this, write the RNG
     * state to a file so that it is read by all processes for the next test. */
    OK(sdis_estimator_buffer_get_rng_state(estim_buf, &rng));
    OK(ssp_rng_get_type(rng, &rng_type));
    CHK(fp = fopen(rng_state_filename, "w"));
    CHK(fwrite(&rng_type, sizeof(rng_type), 1, fp) == 1);
    OK(ssp_rng_write(rng, fp));
    CHK(fclose(fp) == 0);
  }

#ifdef SDIS_ENABLE_MPI
  /* Synchronize processes. Wait for the master process to write RNG state to
   * be used */
  {
    struct sdis_device* sdis = NULL;
    int is_mpi_used = 0;
    OK(sdis_scene_get_device(scn, &sdis));
    OK(sdis_device_is_mpi_used(sdis, &is_mpi_used));
    if(is_mpi_used) {
      CHK(MPI_Barrier(MPI_COMM_WORLD) == MPI_SUCCESS);
    }
  }
#endif

  /* Read the RNG state */
  CHK(fp = fopen(rng_state_filename, "r"));
  CHK(fread(&rng_type, sizeof(rng_type), 1, fp) == 1);
  OK(ssp_rng_create(NULL, rng_type, &rng));
  OK(ssp_rng_read(rng, fp));
  CHK(fclose(fp) == 0);

  /* Run a new calculation using the read RNG state */
  args.rng_state = rng;
  OK(sdis_solve_probe_list(scn, &args, &estim_buf2));
  OK(ssp_rng_ref_put(rng));

  if(!is_master_process) {
    CHK(estim_buf2 == NULL);
  } else {
    check_estimator_buffer_combination(estim_buf, estim_buf2, &args);
    OK(sdis_estimator_buffer_ref_put(estim_buf));
    OK(sdis_estimator_buffer_ref_put(estim_buf2));
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

  check_probe_list(scn, view, is_master_process);

  OK(s3dut_mesh_ref_put(super_shape));
  OK(s3d_scene_view_ref_put(view));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_scene_ref_put(scn));

  free_default_device(sdis);

  CHK(mem_allocated_size() == 0);

  return 0;
}
