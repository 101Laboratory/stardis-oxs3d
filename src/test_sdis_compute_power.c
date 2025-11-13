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

#include <rsys/stretchy_array.h>
#include <star/s3dut.h>

#define N 100000ul /* #realisations */
#define POWER0 10
#define POWER1 5

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
 * Geometry
 ******************************************************************************/
struct context {
  struct s3dut_mesh_data msh0;
  struct s3dut_mesh_data msh1;
  struct sdis_interface* interf0;
  struct sdis_interface* interf1;
};

static void
get_indices(const size_t itri, size_t ids[3], void* context)
{
  const struct context* ctx = context;
  /* Note that we swap the indices to ensure that the triangle normals point
   * inward the super shape */
  if(itri < ctx->msh0.nprimitives) {
    ids[0] = ctx->msh0.indices[itri*3+0];
    ids[2] = ctx->msh0.indices[itri*3+1];
    ids[1] = ctx->msh0.indices[itri*3+2];
  } else {
    const size_t itri2 = itri - ctx->msh0.nprimitives;
    ids[0] = ctx->msh1.indices[itri2*3+0] + ctx->msh0.nvertices;
    ids[2] = ctx->msh1.indices[itri2*3+1] + ctx->msh0.nvertices;
    ids[1] = ctx->msh1.indices[itri2*3+2] + ctx->msh0.nvertices;
  }
}

static void
get_position(const size_t ivert, double pos[3], void* context)
{
  const struct context* ctx = context;
  if(ivert < ctx->msh0.nvertices) {
    pos[0] = ctx->msh0.positions[ivert*3+0] - 2.0;
    pos[1] = ctx->msh0.positions[ivert*3+1];
    pos[2] = ctx->msh0.positions[ivert*3+2];
  } else {
    const size_t ivert2 = ivert - ctx->msh0.nvertices;
    pos[0] = ctx->msh1.positions[ivert2*3+0] + 2.0;
    pos[1] = ctx->msh1.positions[ivert2*3+1];
    pos[2] = ctx->msh1.positions[ivert2*3+2];
  }
}

static void
get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  const struct context* ctx = context;
  *bound = itri < ctx->msh0.nprimitives ? ctx->interf0 : ctx->interf1;
}

/*******************************************************************************
 * Interface
 ******************************************************************************/
static double
interface_get_convection_coef
  (const struct sdis_interface_fragment* frag, struct sdis_data* data)
{
  CHK(frag != NULL); (void)data;
  return 1.0;
}

/*******************************************************************************
 * Fluid medium
 ******************************************************************************/
static double
fluid_get_temperature
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx == NULL); (void)data;
  return 300;
}

/*******************************************************************************
 * Solid medium
 ******************************************************************************/
static double
solid_get_calorific_capacity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return 1.0;
}

static double
solid_get_thermal_conductivity
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return 1.0;
}

static double
solid_get_volumic_mass
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return 1.0;
}

static double
solid_get_delta
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return 1.0 / 20.0;
}

static double
solid_get_volumic_power
  (const struct sdis_rwalk_vertex* vtx, struct sdis_data* data)
{
  CHK(vtx != NULL); (void)data;
  return vtx->P[0] < 0 ? POWER0 : POWER1;
}

/*******************************************************************************
 * Test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct context ctx;
  struct s3dut_mesh* sphere = NULL;
  struct s3dut_mesh* cylinder = NULL;
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  struct sdis_estimator* estimator = NULL;
  struct sdis_medium* fluid = NULL;
  struct sdis_medium* solid0 = NULL;
  struct sdis_medium* solid1 = NULL;
  struct sdis_interface* interf0 = NULL;
  struct sdis_interface* interf1 = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_mc mpow = SDIS_MC_NULL;
  struct sdis_mc time = SDIS_MC_NULL;
  struct sdis_scene_create_args scn_args = SDIS_SCENE_CREATE_ARGS_DEFAULT;
  struct sdis_fluid_shader fluid_shader = DUMMY_FLUID_SHADER;
  struct sdis_solid_shader solid_shader = DUMMY_SOLID_SHADER;
  struct sdis_interface_shader interf_shader = SDIS_INTERFACE_SHADER_NULL;
  struct sdis_compute_power_args args = SDIS_COMPUTE_POWER_ARGS_DEFAULT;
  size_t nverts = 0;
  size_t ntris = 0;
  double ref = 0;
  int is_master_process;
  (void)argc, (void) argv;

  create_default_device(&argc, &argv, &is_master_process, &dev);

  /* Setup the interface shader */
  interf_shader.convection_coef = interface_get_convection_coef;

  /* Setup the fluid shader */
  fluid_shader.temperature = fluid_get_temperature;

  /* Setup the solid shader */
  solid_shader.calorific_capacity = solid_get_calorific_capacity;
  solid_shader.thermal_conductivity = solid_get_thermal_conductivity;
  solid_shader.volumic_mass = solid_get_volumic_mass;
  solid_shader.delta = solid_get_delta;
  solid_shader.volumic_power = solid_get_volumic_power;

  /* Create the fluid */
  OK(sdis_fluid_create(dev, &fluid_shader, NULL, &fluid));

  /* Create the solids */
  OK(sdis_solid_create(dev, &solid_shader, data, &solid0));
  OK(sdis_solid_create(dev, &solid_shader, data, &solid1));

  /* Create the interface0 */
  OK(sdis_interface_create(dev, solid0, fluid, &interf_shader, NULL, &interf0));
  OK(sdis_interface_create(dev, solid1, fluid, &interf_shader, NULL, &interf1));
  ctx.interf0 = interf0;
  ctx.interf1 = interf1;

  /* Create the geometry */
  OK(s3dut_create_sphere(NULL, 1, 512, 256, &sphere));
  OK(s3dut_create_cylinder(NULL, 1, 10, 512, 8, &cylinder));
  OK(s3dut_mesh_get_data(sphere, &ctx.msh0));
  OK(s3dut_mesh_get_data(cylinder, &ctx.msh1));

  /* Create the scene */
  ntris = ctx.msh0.nprimitives + ctx.msh1.nprimitives;
  nverts = ctx.msh0.nvertices + ctx.msh1.nvertices;
  scn_args.get_indices = get_indices;
  scn_args.get_interface = get_interface;
  scn_args.get_position = get_position;
  scn_args.nprimitives = ntris;
  scn_args.nvertices = nverts;
  scn_args.context = &ctx;
  OK(sdis_scene_create(dev, &scn_args, &scn));

  /* Test sdis_compute_power function */
  args.nrealisations = N;
  args.medium = solid0;
  args.time_range[0] = INF;
  args.time_range[1] = INF;
  BA(sdis_compute_power(NULL, &args, &estimator));
  BA(sdis_compute_power(scn, NULL, &estimator));
  BA(sdis_compute_power(scn, &args, NULL));
  args.nrealisations = 0;
  BA(sdis_compute_power(scn, &args, &estimator));
  args.nrealisations = N;
  args.medium = NULL;
  BA(sdis_compute_power(scn, &args, &estimator));
  args.medium = solid0;
  args.time_range[0] = args.time_range[1] = -1;
  BA(sdis_compute_power(scn, &args, &estimator));
  args.time_range[0] = 1;
  BA(sdis_compute_power(scn, &args, &estimator));
  args.time_range[1] = 0;
  BA(sdis_compute_power(scn, &args, &estimator));
  args.time_range[0] = args.time_range[1] = INF;
  OK(sdis_compute_power(scn, &args, &estimator));

  if(!is_master_process) {
    CHK(estimator == NULL);
  } else {
    BA(sdis_estimator_get_power(NULL, &mpow));
    BA(sdis_estimator_get_power(estimator, NULL));
    OK(sdis_estimator_get_power(estimator, &mpow));
    OK(sdis_estimator_get_realisation_time(estimator, &time));

    /* Check results for solid 0 */
    ref = 4.0/3.0 * PI * POWER0;
    printf("Mean power of the solid0 = %g W ~ %g W +/- %g\n",
      ref, mpow.E, mpow.SE);
    check_intersection(ref, 1.e-3*ref, mpow.E, 3*mpow.SE);
    OK(sdis_estimator_ref_put(estimator));
  }

  args.medium = solid1;
  OK(sdis_compute_power(scn, &args, &estimator));

  if(is_master_process) {
    /* Check results for solid 1 */
    OK(sdis_estimator_get_power(estimator, &mpow));
    ref = PI * 10 * POWER1;
    printf("Mean power of the solid1 = %g W ~ %g W +/- %g\n",
      ref, mpow.E, mpow.SE);
    check_intersection(ref, 1.e-3*ref, mpow.E, 3*mpow.SE);
    OK(sdis_estimator_ref_put(estimator));
  }

  args.time_range[0] = 0;
  args.time_range[1] = 10;
  OK(sdis_compute_power(scn, &args, &estimator));

  if(is_master_process) {
    /* Check for a not null time range */
    OK(sdis_estimator_get_power(estimator, &mpow));
    ref = PI * 10 * POWER1;
    printf("Mean power of the solid1 in [0, 10] s = %g W ~ %g W +/- %g\n",
      ref, mpow.E, mpow.SE);
    check_intersection(ref, 1.e-3*ref, mpow.E, 3*mpow.SE);
    OK(sdis_estimator_ref_put(estimator));
  }

  /* Reset the scene with only one solid medium */
  OK(sdis_scene_ref_put(scn));
  ctx.interf0 = interf0;
  ctx.interf1 = interf0;
  OK(sdis_scene_create(dev, &scn_args, &scn));

  /* Check invalid medium */
  args.time_range[0] = args.time_range[1] = 1;
  args.medium = solid1;
  BA(sdis_compute_power(scn, &args, &estimator));

  /* Check non constant volumic power */
  args.medium = solid0;
  OK(sdis_compute_power(scn, &args, &estimator));
  if(is_master_process) {
    OK(sdis_estimator_get_power(estimator, &mpow));
    ref = 4.0/3.0*PI*POWER0 + PI*10*POWER1;
    printf("Mean power of the sphere+cylinder = %g W ~ %g W +/- %g\n",
      ref, mpow.E, mpow.SE);
    check_intersection(ref, 1e-2*ref, mpow.E, 3*mpow.SE);
    OK(sdis_estimator_ref_put(estimator));
  }

#if 0
  {
    double* vertices = NULL;
    size_t* indices = NULL;
    size_t i;
    CHK(vertices = MEM_CALLOC(&allocator, nverts*3, sizeof(*vertices)));
    CHK(indices = MEM_CALLOC(&allocator, ntris*3, sizeof(*indices)));
    FOR_EACH(i, 0, ntris) get_indices(i, indices + i*3, &ctx);
    FOR_EACH(i, 0, nverts) get_position(i, vertices + i*3, &ctx);
    dump_mesh(stdout, vertices, nverts, indices, ntris);
    MEM_RM(&allocator, vertices);
    MEM_RM(&allocator, indices);
  }
#endif

  /* Clean up memory */
  OK(sdis_medium_ref_put(fluid));
  OK(sdis_medium_ref_put(solid0));
  OK(sdis_medium_ref_put(solid1));
  OK(sdis_interface_ref_put(interf0));
  OK(sdis_interface_ref_put(interf1));
  OK(sdis_scene_ref_put(scn));
  OK(s3dut_mesh_ref_put(sphere));
  OK(s3dut_mesh_ref_put(cylinder));

  free_default_device(dev);

  CHK(mem_allocated_size() == 0);
  return 0;
}
