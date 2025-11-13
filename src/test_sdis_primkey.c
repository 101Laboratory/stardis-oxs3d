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

#include <star/s3dut.h>
#include <rsys/float3.h>

/*
 * The system is a solid supershape whose boundary temperature is set to a
 * constant. The temperature of the solid is therefore this same temperature.
 * This simplistic test case is not used to verify a Monte Carlo estimate, but
 * to ensure that the caller can recover the internal representation of the
 * geometric primitives from his own data.
 *
 *             /\
 *         ___/  \___
 *         \        /
 *         /_  __  _\
 *           \/  \/
 *
 */

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
SOLID_PROP(calorific_capacity, 1) /* [J/K/Kg] */
SOLID_PROP(thermal_conductivity, 1) /* [W/m/K] */
SOLID_PROP(volumic_mass, 1) /* [kg/m^3] */
SOLID_PROP(temperature, SDIS_TEMPERATURE_NONE) /* [K] */
SOLID_PROP(delta, 1.0/20.0) /* [m/fp_to_meter] */
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
 * Interface
 ******************************************************************************/
static double
interface_get_temperature
  (const struct sdis_interface_fragment* frag,
   struct sdis_data* data)
{
  (void)frag, (void)data; /* Avoid the "unused variable" warning */
  return 300; /* [K] */
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
 * Validation
 ******************************************************************************/
static void
check(struct sdis_scene* scn, const struct s3dut_mesh* mesh)
{
  struct s3d_primitive prim = S3D_PRIMITIVE_NULL;
  struct s3dut_mesh_data mesh_data;
  struct sdis_primkey key = SDIS_PRIMKEY_NULL;
  size_t iprim = 0;

  OK(s3dut_mesh_get_data(mesh, &mesh_data));

  BA(sdis_scene_get_s3d_primitive(NULL, &key, &prim));
  BA(sdis_scene_get_s3d_primitive(scn, NULL, &prim));
  BA(sdis_scene_get_s3d_primitive(scn, &key, NULL));
  BA(sdis_scene_get_s3d_primitive(scn, &key, &prim));

  FOR_EACH(iprim, 0, mesh_data.nprimitives) {
    const double *v0, *v1, *v2;
    float v0f[3], v1f[3], v2f[3];
    struct s3d_attrib attr0, attr1, attr2;

    /* Check that a primitive can be obtained from the key constructed on the
     * user side */
    v0 = mesh_data.positions + mesh_data.indices[iprim*3 + 0]*3;
    v1 = mesh_data.positions + mesh_data.indices[iprim*3 + 1]*3;
    v2 = mesh_data.positions + mesh_data.indices[iprim*3 + 2]*3;
    sdis_primkey_setup(&key, v0, v1, v2);
    OK(sdis_scene_get_s3d_primitive(scn, &key, &prim));

    /* Check that the primitive on the solver side is the same as that on the
     * user side. On the solver side, vertices are stored in simple precision in
     * Star-3D view. We therefore need to take care of this conversion to check
     * that the vertices are the same */
    OK(s3d_triangle_get_vertex_attrib(&prim, 0, S3D_POSITION, &attr0));
    OK(s3d_triangle_get_vertex_attrib(&prim, 1, S3D_POSITION, &attr1));
    OK(s3d_triangle_get_vertex_attrib(&prim, 2, S3D_POSITION, &attr2));
    f3_set_d3(v0f, v0);
    f3_set_d3(v1f, v1);
    f3_set_d3(v2f, v2);

    /* The vertices have been inverted on the user's side to reverse the normal
     * orientation. Below it is taken into account */
    CHK(f3_eq(v0f, attr0.value));
    CHK(f3_eq(v1f, attr2.value));
    CHK(f3_eq(v2f, attr1.value));
  }
}

/*******************************************************************************
 * The test
 ******************************************************************************/
int
main(int argc, char** argv)
{
  /* Stardis variables */
  struct sdis_device* sdis = NULL;
  struct sdis_interface* interf = NULL;
  struct sdis_medium* solid = NULL;
  struct sdis_medium* dummy = NULL;
  struct sdis_scene* scn = NULL;

  struct s3dut_mesh* super_shape = NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &sdis));

  super_shape = create_super_shape();
  solid = create_solid(sdis);
  dummy = create_dummy(sdis);
  interf = create_interface(sdis, solid, dummy);
  scn = create_scene(sdis, super_shape, interf);

  check(scn, super_shape);

  OK(s3dut_mesh_ref_put(super_shape));
  OK(sdis_device_ref_put(sdis));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_scene_ref_put(scn));

  CHK(mem_allocated_size() == 0);
  return 0;
}
