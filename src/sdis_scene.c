/* Copyright (C) |Meso|Star> 2016-2017 (contact@meso-star.com)
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
#include "sdis_device_c.h"
#include "sdis_interface_c.h"

#include <rsys/dynamic_array.h>
#include <rsys/mem_allocator.h>
#include <star/s3d.h>

#include <limits.h>

/* Context used to wrap the user geometry to Star-3D */
struct geometry_context {
  void (*indices)(const size_t itri, size_t ids[3], void*);
  void (*position)(const size_t ivert, double pos[3], void*);
  void* data;
};

static INLINE void
interface_init
  (struct mem_allocator* allocator,
   struct sdis_interface** interface)
{
  (void)allocator;
  *interface = NULL;
}

/* Declare the array of interfaces */
#define DARRAY_NAME interface
#define DARRAY_DATA struct sdis_interface*
#define DARRAY_FUNCTOR_INIT interface_init
#include <rsys/dynamic_array.h>

struct sdis_scene {
  struct darray_interface interfaces; /* List of interfaces own by the scene */
  struct darray_interface prim_interfaces; /* Per primitive interface */
  struct s3d_scene_view* s3d_view;

  ref_T ref;
  struct sdis_device* dev;
};

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static void
get_indices(const unsigned itri, unsigned out_ids[3], void* data)
{
  struct geometry_context* ctx = data;
  size_t ids[3];
  ASSERT(ctx);
  ctx->indices(itri, ids, ctx->data);
  out_ids[0] = (unsigned)ids[0];
  out_ids[1] = (unsigned)ids[1];
  out_ids[2] = (unsigned)ids[2];
}

static void
get_position(const unsigned ivert, float out_pos[3], void* data)
{
  struct geometry_context* ctx = data;
  double pos[3];
  ASSERT(ctx);
  ctx->position(ivert, pos, ctx->data);
  out_pos[0] = (float)pos[0];
  out_pos[1] = (float)pos[1];
  out_pos[2] = (float)pos[2];
}

static void
clear_interfaces(struct sdis_scene* scn)
{
  size_t i;
  ASSERT(scn);
  FOR_EACH(i, 0, darray_interface_size_get(&scn->interfaces)) {
    if(!darray_interface_cdata_get(&scn->interfaces)[i]) continue;
    SDIS(interface_ref_put(darray_interface_data_get(&scn->interfaces)[i]));
  }
  darray_interface_clear(&scn->interfaces);
  darray_interface_clear(&scn->prim_interfaces);
}

static res_T
setup_interfaces
  (struct sdis_scene* scn,
   const size_t ntris, /* #triangles */
   void (*interface)(const size_t itri, struct sdis_interface**, void*),
   void* ctx)
{
  size_t itri;
  res_T res = RES_OK;
  ASSERT(ntris && interface);

  clear_interfaces(scn);

  FOR_EACH(itri, 0, ntris) {
    struct sdis_interface* itface;
    size_t ninterfaces;
    unsigned id;

    /* Retrieve the interface of the primitive */
    interface(itri, &itface, ctx);
    id = interface_get_id(itface);

    /* Check that the interface is already registered against the scene */
    ninterfaces = darray_interface_size_get(&scn->interfaces);
    if(id >= ninterfaces) {
      res = darray_interface_resize(&scn->interfaces, ninterfaces + 1);
      if(res != RES_OK) goto error;
    }
    if(darray_interface_cdata_get(&scn->interfaces)[id]) {
      ASSERT(darray_interface_cdata_get(&scn->interfaces)[id] == itface);
    } else {
      SDIS(interface_ref_get(itface));
      darray_interface_data_get(&scn->interfaces)[id] = itface;
    }

    /* Register the primitive interface */
    res = darray_interface_push_back(&scn->prim_interfaces, &itface);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  clear_interfaces(scn);
  goto exit;
}

static res_T
setup_geometry
  (struct sdis_scene* scn,
   const size_t ntris, /* #triangles */
   void (*indices)(const size_t itri, size_t ids[3], void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[3], void* ctx),
   void* ctx)
{
  struct geometry_context context;
  struct s3d_shape* s3d_msh = NULL;
  struct s3d_scene* s3d_scn = NULL;
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  res_T res = RES_OK;
  ASSERT(scn && ntris && indices && nverts && position);

  /* Setup the intermediary geometry context */
  context.indices = indices;
  context.position = position;
  context.data = ctx;

  /* Setup the vertex data */
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = get_position;

  /* Create the Star-3D geometry */
  res = s3d_scene_create(scn->dev->s3d, &s3d_scn);
  if(res != RES_OK) goto error;
  res = s3d_shape_create_mesh(scn->dev->s3d, &s3d_msh);
  if(res != RES_OK) goto error;
  res = s3d_scene_attach_shape(s3d_scn, s3d_msh);
  if(res != RES_OK) goto error;
  res = s3d_mesh_setup_indexed_vertices(s3d_msh, (unsigned)ntris, get_indices,
    (unsigned)nverts, &vdata, 1, &context);
  if(res != RES_OK) goto error;
  res = s3d_scene_view_create(s3d_scn, S3D_SAMPLE|S3D_TRACE, &scn->s3d_view);
  if(res != RES_OK) goto error;

exit:
  if(s3d_msh) S3D(shape_ref_put(s3d_msh));
  if(s3d_scn) S3D(scene_ref_put(s3d_scn));
  return res;
error:
  if(scn->s3d_view) S3D(scene_view_ref_put(scn->s3d_view));
  goto exit;
}

static void
scene_release(ref_T * ref)
{
  struct sdis_device* dev = NULL;
  struct sdis_scene* scn = NULL;
  ASSERT(ref);
  scn = CONTAINER_OF(ref, struct sdis_scene, ref);
  dev = scn->dev;
  clear_interfaces(scn);
  darray_interface_release(&scn->interfaces);
  darray_interface_release(&scn->prim_interfaces);
  MEM_RM(dev->allocator, scn);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_scene_create
  (struct sdis_device* dev,
   const size_t ntris, /* #triangles */
   void (*indices)(const size_t itri, size_t ids[3], void*),
   void (*interface)(const size_t itri, struct sdis_interface** bound, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[3], void* ctx),
   void* ctx,
   struct sdis_scene** out_scn)
{
  struct sdis_scene* scn = NULL;
  res_T res = RES_OK;

  if(!dev || !out_scn || !scn || !ntris || !indices || !interface || nverts
  || !position || ntris > UINT_MAX || nverts > UINT_MAX) {
    res = RES_BAD_ARG;
    goto error;
  }

  scn = MEM_CALLOC(dev->allocator, 1, sizeof(struct sdis_scene));
  if(!scn) {
    log_err(dev, "%s: could not allocate the Stardis scene.\n", FUNC_NAME);
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&scn->ref);
  SDIS(device_ref_get(dev));
  scn->dev = dev;
  darray_interface_init(dev->allocator, &scn->interfaces);
  darray_interface_init(dev->allocator, &scn->prim_interfaces);

  res = setup_interfaces(scn, ntris, interface, ctx);
  if(res != RES_OK) {
    log_err(dev, "%s: could not setup the scene interfaces.\n", FUNC_NAME);
    goto error;
  }

  res = setup_geometry(scn, ntris, indices, nverts, position, ctx);
  if(res != RES_OK) {
    log_err(dev, "%s: could not setup the scene geometry.\n", FUNC_NAME);
    goto error;
  }

exit:
  if(out_scn) *out_scn = scn;
  return res;
error:
  if(scn) {
    SDIS(scene_ref_put(scn));
    scn = NULL;
  }
  goto exit;
}

res_T
sdis_scene_ref_get(struct sdis_scene* scn)
{
  if(scn) return RES_BAD_ARG;
  ref_get(&scn->ref);
  return RES_OK;
}

res_T
sdis_scene_ref_put(struct sdis_scene* scn)
{
  if(scn) return RES_BAD_ARG;
  ref_put(&scn->ref, scene_release);
  return RES_OK;
}

