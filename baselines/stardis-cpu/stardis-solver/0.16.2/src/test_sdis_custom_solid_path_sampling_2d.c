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
#include "test_sdis_mesh.h"
#include "test_sdis_utils.h"

#include <rsys/double2.h>
#include <rsys/float2.h>

#include <star/s2d.h>

/*
 * The system is a bilinear profile of the temperature at steady state, i.e. at
 * each point of the system we can calculate the temperature analytically. Two
 * forms are immersed in this temperature field: a super shape and a circle
 * included in the super shape. On the Monte Carlo side, the temperature is
 * unknown everywhere  except on the surface of the super shape whose
 * temperature is defined from the aformentionned bilinear profile.
 *
 * We will estimate the temperature at the position of a probe in solids by
 * providing a user-side function to sample the conductive path in the circle.
 * We should find the temperature of the bilinear profile at the probe position
 * by Monte Carlo, independently of this coupling with an external path sampling
 * routine.
 *
 *
 *                       /\ <-- T(x,y,z)
 *                   ___/  \___
 *      T(y)         \   __   /
 *       |  T(y)      \ /  \ /
 *       |/         T=? *__/ \
 *       o--- T(x)   /_  __  _\
 *                     \/  \/
 */

#define NREALISATIONS 10000
#define CIRCLE_RADIUS 1

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static double
bilinear_profile(const double pos[2])
{
  /* Range in X, Y in which the trilinear profile is defined */
  const double lower = -4;
  const double upper = +4;

  /* Upper temperature limit in X, Y and Z [K]. Lower temperature limit is
   * implicitly 0 */
  const double a = 333; /* Upper temperature limit in X [K] */
  const double b = 432; /* Upper temperature limit in Y [K] */

  double x, y;

  /* Check pre-conditions */
  CHK(pos);
  CHK(lower <= pos[0] && pos[0] <= upper);
  CHK(lower <= pos[1] && pos[1] <= upper);

  x = (pos[0] - lower) / (upper - lower);
  y = (pos[1] - lower) / (upper - lower);
  return a*x + b*y;
}

/*******************************************************************************
 * Scene view
 ******************************************************************************/
/* Parameter for get_inidices/get_vertices functions. Although its layout is the
 * same as that of the mesh data structure, we have deliberately defined a new
 * data type since mesh functions cannot be invoked. This is because the form's
 * member variables are not necessarily extensible arrays, as is the case with
 * mesh */
struct shape {
  double* pos;
  size_t* ids;
  size_t npos;
  size_t nseg;
};
#define SHAPE_NULL__ {NULL, NULL, 0, 0}
static const struct shape SHAPE_NULL = SHAPE_NULL__;

static void
get_position(const unsigned ivert, float pos[2], void* ctx)
{
  const struct shape* shape = ctx;
  CHK(shape && pos && ivert < shape->npos);
  f2_set_d2(pos, shape->pos + ivert*2);
}

static void
get_indices(const unsigned iseg, unsigned ids[2], void* ctx)
{
  const struct shape* shape = ctx;
  CHK(shape && ids && iseg < shape->nseg);
  ids[0] = (unsigned)shape->ids[iseg*2+0];
  ids[1] = (unsigned)shape->ids[iseg*2+1];
}

static struct s2d_scene_view*
create_view(struct shape* shape_data)
{
  /* Star2D */
  struct s2d_vertex_data vdata = S2D_VERTEX_DATA_NULL;
  struct s2d_device* dev = NULL;
  struct s2d_scene* scn = NULL;
  struct s2d_shape* shape = NULL;
  struct s2d_scene_view* view = NULL;

  OK(s2d_device_create(NULL, NULL, 0, &dev));

  /* Shape */
  vdata.usage = S2D_POSITION;
  vdata.type = S2D_FLOAT2;
  vdata.get = get_position;
  OK(s2d_shape_create_line_segments(dev, &shape));
  OK(s2d_line_segments_setup_indexed_vertices(shape, (unsigned)shape_data->nseg,
    get_indices, (unsigned)shape_data->npos, &vdata, 1, shape_data));

  /* Scene view */
  OK(s2d_scene_create(dev, &scn));
  OK(s2d_scene_attach_shape(scn, shape));
  OK(s2d_scene_view_create(scn, S2D_TRACE|S2D_GET_PRIMITIVE, &view));

  /* Clean up */
  OK(s2d_device_ref_put(dev));
  OK(s2d_scene_ref_put(scn));
  OK(s2d_shape_ref_put(shape));

  return view;
}

/*******************************************************************************
 * Mesh, i.e. supershape and sphere
 ******************************************************************************/
static void
mesh_add_super_shape(struct mesh* mesh)
{
  double* pos = NULL;
  size_t* ids = NULL;

  const unsigned nslices = 128;
  const double a = 1.0;
  const double b = 1.0;
  const double n1 = 1.0;
  const double n2 = 1.0;
  const double n3 = 1.0;
  const double m = 6.0;
  size_t i = 0;

  CHK(mesh);

  FOR_EACH(i, 0, nslices) {
    const double theta = (double)i * (2.0*PI / (double)nslices);
    const double tmp0 = pow(fabs(1.0/a * cos(m/4.0*theta)), n2);
    const double tmp1 = pow(fabs(1.0/b * sin(m/4.0*theta)), n3);
    const double tmp2 = pow(tmp0 + tmp1, 1.0/n1);
    const double r = 1.0 / tmp2;
    const double x = cos(theta) * r * CIRCLE_RADIUS*2;
    const double y = sin(theta) * r * CIRCLE_RADIUS*2;
    const size_t j = (i + 1) % nslices;

    sa_push(pos, x);
    sa_push(pos, y);
    sa_push(ids, i);
    sa_push(ids, j);
  }

  mesh_2d_append(mesh, pos, sa_size(pos)/2, ids, sa_size(ids)/2, NULL);

  sa_release(pos);
  sa_release(ids);
}

static void
mesh_add_circle(struct mesh* mesh)
{
  double* pos = NULL;
  size_t* ids = NULL;

  const size_t nverts = 64;
  size_t i = 0;

  CHK(mesh);

  FOR_EACH(i, 0, nverts) {
    const double theta = (double)i * (2*PI)/(double)nverts;
    const double x = cos(theta)*CIRCLE_RADIUS;
    const double y = sin(theta)*CIRCLE_RADIUS;
    const size_t j = (i+1)%nverts;

    sa_push(pos, x);
    sa_push(pos, y);
    sa_push(ids, i);
    sa_push(ids, j);
  }

  mesh_2d_append(mesh, pos, sa_size(pos)/2, ids, sa_size(ids)/2, NULL);

  sa_release(pos);
  sa_release(ids);
}

/*******************************************************************************
 * Custom conductive path
 ******************************************************************************/
struct custom_solid {
  struct s2d_scene_view* view; /* Star-2D view of the shape */
  const struct shape* shape; /* Raw data */
};

static void
setup_solver_primitive
  (struct sdis_scene* scn,
   const struct shape* shape,
   const struct s2d_hit* user_hit,
   struct s2d_primitive* prim)
{
  struct sdis_primkey key = SDIS_PRIMKEY_NULL;
  const double *v0, *v1;
  float v0f[2], v1f[2];
  struct s2d_attrib attr0, attr1;

  v0 = shape->pos + shape->ids[user_hit->prim.prim_id*2+0]*2;
  v1 = shape->pos + shape->ids[user_hit->prim.prim_id*2+1]*2;
  sdis_primkey_2d_setup(&key, v0, v1);
  OK(sdis_scene_get_s2d_primitive(scn, &key, prim));

  /* Check that the primitive on the solver side is the same as that on the
   * user side. On the solver side, vertices are stored in simple precision in
   * Star-3D view. We therefore need to take care of this conversion to check
   * that the vertices are the same */
  OK(s2d_segment_get_vertex_attrib(prim, 0, S2D_POSITION, &attr0));
  OK(s2d_segment_get_vertex_attrib(prim, 1, S2D_POSITION, &attr1));
  f2_set_d2(v0f, v0);
  f2_set_d2(v1f, v1);

  /* The vertices have been inverted on the user's side to reverse the normal
   * orientation. Below it is taken into account */
  CHK(f2_eq(v0f, attr0.value));
  CHK(f2_eq(v1f, attr1.value));
}

/* Implementation of a Walk on Sphere algorithm for an origin-centered spherical
 * geometry of radius SPHERE_RADIUS */
static res_T
sample_steady_diffusive_path
  (struct sdis_scene* scn,
   struct ssp_rng* rng,
   struct sdis_path* path,
   struct sdis_data* data)
{
  struct custom_solid* solid = NULL;
  struct s2d_hit hit = S2D_HIT_NULL;

  double pos[2];
  const double epsilon = CIRCLE_RADIUS * 1.e-6; /* Epsilon shell */

  CHK(scn && rng && path && data);

  solid = sdis_data_get(data);

  d2_set(pos, path->vtx.P);

  do {
    /* Distance from the geometry center to the current position */
    const double dst = d2_len(pos);

    double dir[3] = {0,0,0};
    double r = 0; /* Radius */

    r = CIRCLE_RADIUS - dst;
    CHK(dst > 0);

    if(r > epsilon) {
      /* Uniformly sample a new position on the surrounding sphere */
      ssp_ran_circle_uniform(rng, dir, NULL);

      /* Move to the new position */
      d2_muld(dir, dir, r);
      d2_add(pos, pos, dir);

    /* The current position is in the epsilon shell:
     * move it to the nearest interface position */
    } else {
      float posf[2];

      d2_set(dir, pos);
      d2_normalize(dir, dir);
      d2_muld(pos, dir, CIRCLE_RADIUS);

      /* Map the position to the sphere geometry */
      f2_set_d2(posf, pos);
      OK(s2d_scene_view_closest_point(solid->view, posf, (float)INF, NULL, &hit));
    }

  /* The calculation is performed in steady state, so the path necessarily stops
   * at a boundary */
  } while(S2D_HIT_NONE(&hit));

  /* Setup the path state */
  d2_set(path->vtx.P, pos);
  path->weight = 0;
  path->at_limit = 0;
  path->prim_3d = S3D_PRIMITIVE_NULL;
  setup_solver_primitive(scn, solid->shape, &hit, &path->prim_2d);

  return RES_OK;
}
/*******************************************************************************
 * The solids, i.e. media of the super shape and the sphere
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
SOLID_PROP(delta, 1.0/40.0) /* [m] */

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

static struct sdis_medium*
create_custom
  (struct sdis_device* sdis,
   struct s2d_scene_view* view,
   const struct shape* shape)
{
  /* Stardis variables */
  struct sdis_solid_shader shader = SDIS_SOLID_SHADER_NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_data* data = NULL;

  /* Mesh variables */
  struct custom_solid* custom_solid = NULL;
  const size_t sz = sizeof(struct custom_solid);
  const size_t al = ALIGNOF(struct custom_solid);

  OK(sdis_data_create(sdis, sz, al, NULL, &data));
  custom_solid = sdis_data_get(data);
  custom_solid->view = view;
  custom_solid->shape = shape;

  shader.calorific_capacity = solid_get_calorific_capacity;
  shader.thermal_conductivity = solid_get_thermal_conductivity;
  shader.volumic_mass = solid_get_volumic_mass;
  shader.delta = solid_get_delta;
  shader.temperature = solid_get_temperature;
  shader.sample_path = sample_steady_diffusive_path;

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
  return bilinear_profile(frag->P);
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
  size_t sshape_end_id; /* Last segment index of the super shape */
  struct sdis_interface* sshape;
  struct sdis_interface* sphere;
};
#define SCENE_CONTEXT_NULL__ {NULL, 0, 0, 0}
static const struct scene_context SCENE_CONTEXT_NULL = SCENE_CONTEXT_NULL__;

static void
scene_get_indices(const size_t iseg, size_t ids[2], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(ids && context && iseg < mesh_2d_nsegments(context->mesh));
  ids[0] = (unsigned)context->mesh->indices[iseg*2+0];
  ids[1] = (unsigned)context->mesh->indices[iseg*2+1];
}

static void
scene_get_interface(const size_t iseg, struct sdis_interface** interf, void* ctx)
{
  struct scene_context* context = ctx;
  CHK(interf && context && iseg < mesh_2d_nsegments(context->mesh));
  if(iseg < context->sshape_end_id) {
    *interf = context->sshape;
  } else {
    *interf = context->sphere;
  }
}

static void
scene_get_position(const size_t ivert, double pos[2], void* ctx)
{
  struct scene_context* context = ctx;
  CHK(pos && context && ivert < mesh_2d_nvertices(context->mesh));
  pos[0] = context->mesh->positions[ivert*2+0];
  pos[1] = context->mesh->positions[ivert*2+1];
}

static struct sdis_scene*
create_scene(struct sdis_device* sdis, struct scene_context* ctx)
{
  struct sdis_scene* scn = NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;

  scn_args.get_indices = scene_get_indices;
  scn_args.get_interface = scene_get_interface;
  scn_args.get_position = scene_get_position;
  scn_args.nprimitives = mesh_2d_nsegments(ctx->mesh);
  scn_args.nvertices = mesh_2d_nvertices(ctx->mesh);
  scn_args.context = ctx;
  OK(sdis_scene_2d_create(sdis, &scn_args, &scn));
  return scn;
}

/*******************************************************************************
 * Validations
 ******************************************************************************/
static void
check_probe(struct sdis_scene* scn, const int is_master_process)
{
  struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  double ref = 0;

  args.position[0] = CIRCLE_RADIUS*0.125;
  args.position[1] = CIRCLE_RADIUS*0.250;
  args.nrealisations = NREALISATIONS;

  OK(sdis_solve_probe(scn, &args, &estimator));

  if(!is_master_process) return;

  OK(sdis_estimator_get_temperature(estimator, &T));

  ref = bilinear_profile(args.position);

  printf("T(%g, %g) = %g ~ %g +/- %g\n",
    SPLIT2(args.position), ref, T.E, T.SE);

  CHK(eq_eps(ref, T.E, T.SE*3));
  OK(sdis_estimator_ref_put(estimator));
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  /* Stardis */
  struct sdis_device* dev = NULL;
  struct sdis_interface* solid_dummy = NULL;
  struct sdis_interface* custom_solid = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* custom = NULL;
  struct sdis_medium* dummy = NULL; /* Medium surrounding the solid */
  struct sdis_scene* scn = NULL;

  /* Star 2D */
  struct shape shape = SHAPE_NULL;
  struct s2d_scene_view* circle_view = NULL;

  /* Miscellaneous */
  struct scene_context ctx = SCENE_CONTEXT_NULL;
  struct mesh mesh = MESH_NULL;
  size_t sshape_end_id = 0; /* Last index of the super shape */
  int is_master_process = 1;
  (void)argc, (void)argv;

  create_default_device(&argc, &argv, &is_master_process, &dev);

  /* Mesh */
  mesh_init(&mesh);
  mesh_add_super_shape(&mesh);
  sshape_end_id = mesh_2d_nsegments(&mesh);
  mesh_add_circle(&mesh);

  /* Create a view of the circle's geometry. This will be used to couple custom
   * solid path sampling to the solver */
  shape.pos = mesh.positions;
  shape.ids = mesh.indices + sshape_end_id*2;
  shape.npos = mesh_2d_nvertices(&mesh);
  shape.nseg = mesh_2d_nsegments(&mesh) - sshape_end_id;
  circle_view = create_view(&shape);

  /* Physical properties */
  dummy = create_dummy(dev);
  solid = create_solid(dev);
  custom = create_custom(dev, circle_view, &shape);
  solid_dummy = create_interface(dev, dummy, solid);
  custom_solid = create_interface(dev, solid, custom);

  /* Scene */
  ctx.mesh = &mesh;
  ctx.sshape_end_id = sshape_end_id;
  ctx.sshape = solid_dummy;
  ctx.sphere = custom_solid;
  scn = create_scene(dev, &ctx);

  check_probe(scn, is_master_process);

  mesh_release(&mesh);

  OK(s2d_scene_view_ref_put(circle_view));
  OK(sdis_interface_ref_put(solid_dummy));
  OK(sdis_interface_ref_put(custom_solid));
  OK(sdis_medium_ref_put(custom));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_scene_ref_put(scn));

  free_default_device(dev);

  CHK(mem_allocated_size() == 0);
  return 0;
}
