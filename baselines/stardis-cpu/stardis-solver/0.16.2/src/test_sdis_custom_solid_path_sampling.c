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

#include <star/s3d.h>
#include <star/s3dut.h>
#include <star/ssp.h>

#include <rsys/double3.h>

/*
 * The system is a trilinear profile of the temperature at steady state, i.e. at
 * each point of the system we can calculate the temperature analytically. Two
 * forms are immersed in this temperature field: a super shape and a sphere
 * included in the super shape. On the Monte Carlo side, the temperature is
 * unknown everywhere  except on the surface of the super shape whose
 * temperature is defined from the aformentionned trilinear profile.
 *
 * We will estimate the temperature at the position of a probe in solids by
 * providing a user-side function to sample the conductive path in the sphere.
 * We should find the temperature of the trilinear profile at the probe position
 * by Monte Carlo, independently of this coupling with an external path sampling
 * routine.
 *
 *
 *                       /\ <-- T(x,y,z)
 *                   ___/  \___
 *      T(z)         \   __   /
 *       |  T(y)     T=?/. \ /
 *       |/           / \__/ \
 *       o--- T(x)   /_  __  _\
 *                     \/  \/
 */

#define NREALISATIONS 10000
#define SPHERE_RADIUS 1

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
  size_t ntri;
};
#define SHAPE_NULL__ {NULL, NULL, 0, 0}
static const struct shape SHAPE_NULL = SHAPE_NULL__;

static void
get_position(const unsigned ivert, float pos[3], void* ctx)
{
  const struct shape* shape = ctx;
  CHK(shape && pos && ivert < shape->npos);
  f3_set_d3(pos, shape->pos + ivert*3);
}

static void
get_indices(const unsigned itri, unsigned ids[3], void* ctx)
{
  const struct shape* shape = ctx;
  CHK(shape && ids && itri < shape->ntri);
  ids[0] = (unsigned)shape->ids[itri*3+0];
  ids[1] = (unsigned)shape->ids[itri*3+1];
  ids[2] = (unsigned)shape->ids[itri*3+2];
}

static struct s3d_scene_view*
create_view(struct shape* shape_data)
{
  /* Star3D */
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  struct s3d_device* dev = NULL;
  struct s3d_scene* scn = NULL;
  struct s3d_shape* shape = NULL;
  struct s3d_scene_view* view = NULL;

  OK(s3d_device_create(NULL, NULL, 0, &dev));

  /* Shape */
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = get_position;
  OK(s3d_shape_create_mesh(dev, &shape));
  OK(s3d_mesh_setup_indexed_vertices(shape, (unsigned)shape_data->ntri,
    get_indices, (unsigned)shape_data->npos, &vdata, 1, shape_data));

  /* Scene view */
  OK(s3d_scene_create(dev, &scn));
  OK(s3d_scene_attach_shape(scn, shape));
  OK(s3d_scene_view_create(scn, S3D_TRACE|S3D_GET_PRIMITIVE, &view));

  /* Clean up */
  OK(s3d_device_ref_put(dev));
  OK(s3d_scene_ref_put(scn));
  OK(s3d_shape_ref_put(shape));

  return view;
}

/*******************************************************************************
 * Mesh, i.e. supershape and sphere
 ******************************************************************************/
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
  const double radius = SPHERE_RADIUS;
  const unsigned nslices = 128;

  OK(s3dut_create_sphere(NULL, radius, nslices, nslices/2, &sphere));
  OK(s3dut_mesh_get_data(sphere, &sphere_data));
  mesh_append(mesh, sphere_data.positions, sphere_data.nvertices,
    sphere_data.indices, sphere_data.nprimitives, NULL);
  OK(s3dut_mesh_ref_put(sphere));
}

/*******************************************************************************
 * Custom conductive path
 ******************************************************************************/
struct custom_solid {
  struct s3d_scene_view* view; /* Star-3D view of the shape */
  const struct shape* shape; /* Raw data */
};

static void
setup_solver_primitive
  (struct sdis_scene* scn,
   const struct shape* shape,
   const struct s3d_hit* user_hit,
   struct s3d_primitive* prim)
{
  struct sdis_primkey key = SDIS_PRIMKEY_NULL;
  const double *v0, *v1, *v2;
  float v0f[3], v1f[3], v2f[3];
  struct s3d_attrib attr0, attr1, attr2;

  v0 = shape->pos + shape->ids[user_hit->prim.prim_id*3+0]*3;
  v1 = shape->pos + shape->ids[user_hit->prim.prim_id*3+1]*3;
  v2 = shape->pos + shape->ids[user_hit->prim.prim_id*3+2]*3;
  sdis_primkey_setup(&key, v0, v1, v2);
  OK(sdis_scene_get_s3d_primitive(scn, &key, prim));

  /* Check that the primitive on the solver side is the same as that on the
   * user side. On the solver side, vertices are stored in simple precision in
   * Star-3D view. We therefore need to take care of this conversion to check
   * that the vertices are the same */
  OK(s3d_triangle_get_vertex_attrib(prim, 0, S3D_POSITION, &attr0));
  OK(s3d_triangle_get_vertex_attrib(prim, 1, S3D_POSITION, &attr1));
  OK(s3d_triangle_get_vertex_attrib(prim, 2, S3D_POSITION, &attr2));
  f3_set_d3(v0f, v0);
  f3_set_d3(v1f, v1);
  f3_set_d3(v2f, v2);

  /* The vertices have been inverted on the user's side to reverse the normal
   * orientation. Below it is taken into account */
  CHK(f3_eq(v0f, attr0.value));
  CHK(f3_eq(v1f, attr2.value));
  CHK(f3_eq(v2f, attr1.value));
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
  struct s3d_hit hit = S3D_HIT_NULL;

  double pos[3];
  const double epsilon = SPHERE_RADIUS * 1.e-6; /* Epsilon shell */

  CHK(scn && rng && path && data);

  solid = sdis_data_get(data);

  d3_set(pos, path->vtx.P);

  do {
    /* Distance from the geometry center to the current position */
    const double dst = d3_len(pos);

    double dir[3] = {0,0,0};
    double r = 0; /* Radius */

    r = SPHERE_RADIUS - dst;
    CHK(dst > 0);

    if(r > epsilon) {
      /* Uniformly sample a new position on the surrounding sphere */
      ssp_ran_sphere_uniform(rng, dir, NULL);

      /* Move to the new position */
      d3_muld(dir, dir, r);
      d3_add(pos, pos, dir);

    /* The current position is in the epsilon shell:
     * move it to the nearest interface position */
    } else {
      float posf[3];

      d3_set(dir, pos);
      d3_normalize(dir, dir);
      d3_muld(pos, dir, SPHERE_RADIUS);

      /* Map the position to the sphere geometry */
      f3_set_d3(posf, pos);
      OK(s3d_scene_view_closest_point(solid->view, posf, (float)INF, NULL, &hit));
    }

  /* The calculation is performed in steady state, so the path necessarily stops
   * at a boundary */
  } while(S3D_HIT_NONE(&hit));

  /* Setup the path state */
  d3_set(path->vtx.P, pos);
  path->weight = 0;
  path->at_limit = 0;
  path->prim_2d = S2D_PRIMITIVE_NULL;
  setup_solver_primitive(scn, solid->shape, &hit, &path->prim_3d);

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
   struct s3d_scene_view* view,
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
check_probe(struct sdis_scene* scn, const int is_master_process)
{
  struct sdis_solve_probe_args args = SDIS_SOLVE_PROBE_ARGS_DEFAULT;
  struct sdis_mc T = SDIS_MC_NULL;
  struct sdis_estimator* estimator = NULL;
  double ref = 0;

  args.position[0] = SPHERE_RADIUS*0.125;
  args.position[1] = SPHERE_RADIUS*0.250;
  args.position[2] = SPHERE_RADIUS*0.375;
  args.nrealisations = NREALISATIONS;

  OK(sdis_solve_probe(scn, &args, &estimator));

  if(!is_master_process) return;

  OK(sdis_estimator_get_temperature(estimator, &T));

  ref = trilinear_profile(args.position);

  printf("T(%g, %g, %g) = %g ~ %g +/- %g\n",
    SPLIT3(args.position), ref, T.E, T.SE);

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

  /* Star3D */
  struct shape shape = SHAPE_NULL;
  struct s3d_scene_view* sphere_view = NULL;

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
  sshape_end_id = mesh_ntriangles(&mesh);
  mesh_add_sphere(&mesh);

  /* Create a view of the sphere's geometry. This will be used to couple custom
   * solid path sampling to the solver */
  shape.pos = mesh.positions;
  shape.ids = mesh.indices + sshape_end_id*3;
  shape.npos = mesh_nvertices(&mesh);
  shape.ntri = mesh_ntriangles(&mesh) - sshape_end_id/* #sshape triangles*/;
  sphere_view = create_view(&shape);

  /* Physical properties */
  dummy = create_dummy(dev);
  solid = create_solid(dev);
  custom = create_custom(dev, sphere_view, &shape);
  solid_dummy = create_interface(dev, solid, dummy);
  custom_solid = create_interface(dev, custom, solid);

  /* Scene */
  ctx.mesh = &mesh;
  ctx.sshape_end_id = sshape_end_id;
  ctx.sshape = solid_dummy;
  ctx.sphere = custom_solid;
  scn = create_scene(dev, &ctx);

  check_probe(scn, is_master_process);

  mesh_release(&mesh);

  OK(s3d_scene_view_ref_put(sphere_view));
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
