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

#include <star/s2d.h>

#include <rsys/float2.h>

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
static struct mesh
create_super_shape(void)
{
  struct mesh sshape = MESH_NULL;

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
  const struct mesh* sshape;
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
   const struct mesh* sshape,
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
  scn_args.nprimitives = mesh_2d_nsegments(sshape);
  scn_args.nvertices = mesh_2d_nvertices(sshape);
  scn_args.context = &context;
  OK(sdis_scene_2d_create(sdis, &scn_args, &scn));
  return scn;
}

/*******************************************************************************
 * Validation
 ******************************************************************************/
static void
check(struct sdis_scene* scn, const struct mesh* mesh)
{
  struct s2d_primitive prim = S2D_PRIMITIVE_NULL;
  struct sdis_primkey key = SDIS_PRIMKEY_NULL;
  size_t iprim = 0;
  size_t nprims = 0;

  BA(sdis_scene_get_s2d_primitive(NULL, &key, &prim));
  BA(sdis_scene_get_s2d_primitive(scn, NULL, &prim));
  BA(sdis_scene_get_s2d_primitive(scn, &key, NULL));
  BA(sdis_scene_get_s2d_primitive(scn, &key, &prim));

  nprims = mesh_2d_nsegments(mesh);
  FOR_EACH(iprim, 0, nprims) {
    const double *v0, *v1;
    float v0f[2], v1f[2];
    struct s2d_attrib attr0, attr1;

    /* Check that a primitive can be obtained from the key constructed on the
     * user side */
    v0 = mesh->positions + mesh->indices[iprim*2 + 0]*2;
    v1 = mesh->positions + mesh->indices[iprim*2 + 1]*2;
    sdis_primkey_2d_setup(&key, v0, v1);
    OK(sdis_scene_get_s2d_primitive(scn, &key, &prim));

    /* Check that the primitive on the solver side is the same as that on the
     * user side. On the solver side, vertices are stored in simple precision in
     * Star-3D view. We therefore need to take care of this conversion to check
     * that the vertices are the same */
    OK(s2d_segment_get_vertex_attrib(&prim, 0, S2D_POSITION, &attr0));
    OK(s2d_segment_get_vertex_attrib(&prim, 1, S2D_POSITION, &attr1));
    f2_set_d2(v0f, v0);
    f2_set_d2(v1f, v1);

    /* The vertices have been inverted on the user's side to reverse the normal
     * orientation. Below it is taken into account */
    CHK(f2_eq(v0f, attr1.value));
    CHK(f2_eq(v1f, attr0.value));
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
  struct mesh sshape = MESH_NULL;
  (void)argc, (void)argv;

  OK(sdis_device_create(&SDIS_DEVICE_CREATE_ARGS_DEFAULT, &sdis));

  sshape = create_super_shape();
  solid = create_solid(sdis);
  dummy = create_dummy(sdis);
  interf = create_interface(sdis, solid, dummy);
  scn = create_scene(sdis, &sshape, interf);

  check(scn, &sshape);

  mesh_release(&sshape);
  OK(sdis_device_ref_put(sdis));
  OK(sdis_interface_ref_put(interf));
  OK(sdis_medium_ref_put(solid));
  OK(sdis_medium_ref_put(dummy));
  OK(sdis_scene_ref_put(scn));

  CHK(mem_allocated_size() == 0);
  return 0;
}
