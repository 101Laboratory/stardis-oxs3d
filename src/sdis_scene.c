/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/mem_allocator.h>

#include <senc.h>
#include <star/s2d.h>
#include <star/s3d.h>

#include <limits.h>

/* Context used to wrap the user geometry and interfaces to Star-Enc */
struct geometry {
  void (*indices)(const size_t iprim, size_t ids[], void*);
  void (*interf)(const size_t iprim, struct sdis_interface**, void*);
  void (*position)(const size_t ivert, double pos[], void*);
  void* data;
};

/*******************************************************************************
 * Helper function
 ******************************************************************************/
/* Check that `hit' roughly lies on a vertex. For segments, a simple but
 * approximative way is to test that its position have at least one barycentric
 * coordinate roughly equal to 0 or 1. */
static FINLINE int
hit_on_vertex(const struct s2d_hit* hit)
{
  const float on_vertex_eps = 1.e-4f;
  float v;
  ASSERT(hit && !S2D_HIT_NONE(hit));
  v = 1.f - hit->u;
  return eq_epsf(hit->u, 0.f, on_vertex_eps)
      || eq_epsf(hit->u, 1.f, on_vertex_eps)
      || eq_epsf(v, 0.f, on_vertex_eps)
      || eq_epsf(v, 1.f, on_vertex_eps);
}

/* Check that `hit' roughly lies on an edge. For triangular primitives, a
 * simple but approximative way is to test that its position have at least one
 * barycentric coordinate roughly equal to 0 or 1. */
static FINLINE int
hit_on_edge(const struct s3d_hit* hit)
{
  const float on_edge_eps = 1.e-4f;
  float w;
  ASSERT(hit && !S3D_HIT_NONE(hit));
  w = 1.f - hit->uv[0] - hit->uv[1];
  return eq_epsf(hit->uv[0], 0.f, on_edge_eps)
      || eq_epsf(hit->uv[0], 1.f, on_edge_eps)
      || eq_epsf(hit->uv[1], 0.f, on_edge_eps)
      || eq_epsf(hit->uv[1], 1.f, on_edge_eps)
      || eq_epsf(w, 0.f, on_edge_eps)
      || eq_epsf(w, 1.f, on_edge_eps);
}

static int
hit_filter_function_3d
  (const struct s3d_hit* hit,
   const float org[3],
   const float dir[3],
   void* ray_data,
   void* filter_data)
{
  const struct s3d_hit* hit_from = ray_data;
  (void)org, (void)dir, (void)filter_data;

  if(!hit_from || S3D_HIT_NONE(hit_from)) return 0; /* No filtering */

  if(S3D_PRIMITIVE_EQ(&hit_from->prim, &hit->prim)) return 1;

  if(eq_epsf(hit->distance, 0, 1.e-6f)) {
    /* If the targeted point is near of the origin, check that it lies on an
     * edge shared by the 2 primitives. */
    return hit_on_edge(hit_from) && hit_on_edge(hit);
  }

  return 0;
}

static int
hit_filter_function_2d
  (const struct s2d_hit* hit,
   const float org[2],
   const float dir[2],
   void* ray_data,
   void* filter_data)
{
  const struct s2d_hit* hit_from = ray_data;
  (void)org, (void)dir, (void)filter_data;

  if(!hit_from || S2D_HIT_NONE(hit_from)) return 0; /* No filtering */

  if(S2D_PRIMITIVE_EQ(&hit_from->prim, &hit->prim)) return 1;

  if(eq_epsf(hit->distance, 0, 1.e-6f)) {
    /* If the targeted point is near of the origin, check that it lies on a
     * vertex shared by the 2 segments. */
    return hit_on_vertex(hit_from) && hit_on_vertex(hit);
  }

  return 0;
}

static void
geometry_indices_3d(const unsigned itri, unsigned out_ids[3], void* data)
{
  struct geometry* ctx = data;
  size_t ids[3];
  ASSERT(ctx && out_ids);
  ctx->indices(itri, ids, ctx->data);
  out_ids[0] = (unsigned)ids[0];
  out_ids[1] = (unsigned)ids[1];
  out_ids[2] = (unsigned)ids[2];
}

static void
geometry_media(const unsigned itri, unsigned media[2], void* data)
{
  struct geometry* ctx = data;
  struct sdis_interface* interf;
  ASSERT(ctx && media);
  ctx->interf(itri, &interf, ctx->data);
  /* FIXME check that the order in which media are returned is right  */
  media[0] = medium_get_id(interf->medium_back);
  media[1] = medium_get_id(interf->medium_front);
}

static void
geometry_position_3d(const unsigned ivert, double out_pos[3], void* data)
{
  struct geometry* ctx = data;
  double pos[3];
  ASSERT(ctx && out_pos);
  ctx->position(ivert, pos, ctx->data);
  out_pos[0] = pos[0];
  out_pos[1] = pos[1];
  out_pos[2] = pos[2];
}

static void
descriptor_indices_3d(const unsigned itri, unsigned ids[3], void* data)
{
  struct senc_descriptor* desc = data;
  SENC(descriptor_get_global_triangle(desc, itri, ids));
}


static void
descriptor_position_3d(const unsigned ivert, float out_pos[3], void* data)
{
  struct senc_descriptor* desc = data;
  double pos[3];
  SENC(descriptor_get_global_vertex(desc, ivert, pos));
  out_pos[0] = (float)pos[0];
  out_pos[1] = (float)pos[1];
  out_pos[2] = (float)pos[2];
}

static void
enclosure_indices_3d(const unsigned itri, unsigned ids[3], void* data)
{
  struct senc_enclosure* enc = data;
  SENC(enclosure_get_triangle(enc, itri, ids));
}

static void
enclosure_position_3d(const unsigned ivert, float out_pos[3], void* data)
{
  struct senc_enclosure* enc = data;
  double pos[3];
  ASSERT(out_pos);
  SENC(enclosure_get_vertex(enc, ivert, pos));
  out_pos[0] = (float)pos[0];
  out_pos[1] = (float)pos[1];
  out_pos[2] = (float)pos[2];
}

#if 0
static void
get_indices_2d(const unsigned iseg, unsigned out_ids[2], void* data)
{
  struct geometry_context* ctx = data;
  size_t ids[2];
  ASSERT(ctx);
  ctx->indices(iseg, ids, ctx->data);
  out_ids[0] = (unsigned)ids[0];
  out_ids[1] = (unsigned)ids[1];
}

static void
get_position_2d(const unsigned ivert, float out_pos[2], void* data)
{
  struct geometry_context* ctx = data;
  double pos[2];
  ASSERT(ctx);
  ctx->position(ivert, pos, ctx->data);
  out_pos[0] = (float)pos[0];
  out_pos[1] = (float)pos[1];
}

#endif

static void
clear_properties(struct sdis_scene* scn)
{
  size_t i;
  ASSERT(scn);
  FOR_EACH(i, 0, darray_interf_size_get(&scn->interfaces)) {
    if(darray_interf_cdata_get(&scn->interfaces)[i]) {
      SDIS(interface_ref_put(darray_interf_data_get(&scn->interfaces)[i]));
    }
  }
  darray_interf_clear(&scn->interfaces);
  darray_medium_clear(&scn->media);
  darray_prim_prop_clear(&scn->prim_props);
}

static res_T
run_analyze_3d
  (struct sdis_scene* scn,
   const size_t ntris, /* #triangles */
   void (*indices)(const size_t itri, size_t ids[3], void*),
   void (*interf)(const size_t itri, struct sdis_interface**, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[3], void* ctx),
   void* ctx,
   struct senc_descriptor** out_desc)
{
  struct geometry geom;
  struct senc_device* senc = NULL;
  struct senc_scene* senc_scn = NULL;
  struct senc_descriptor* desc = NULL;
  size_t nmedia;
  size_t itri;
  res_T res = RES_OK;
  ASSERT(scn && ntris && indices && interf && nverts && position && out_desc);

  res = senc_device_create(scn->dev->logger, scn->dev->allocator,
    scn->dev->nthreads, scn->dev->verbose, &senc);
  if(res != RES_OK) goto error;

  /* Conservatively define the number of media.
   *
   * FIXME The number of media is going to be remove from senc_scene_create
   * profile and thus the following code should be unecessary soon. */
  nmedia = 0;
  FOR_EACH(itri, 0, ntris) {
    struct sdis_interface* itface;
    interf(itri, &itface, ctx);
    nmedia = MMAX(nmedia, medium_get_id(itface->medium_front));
    nmedia = MMAX(nmedia, medium_get_id(itface->medium_back));
  }
  nmedia += 1; /* +1 to define the "number of" media and not the max id */

  res = senc_scene_create(senc, (unsigned)nmedia, &senc_scn);
  if(res != RES_OK) goto error;

  /* Setup the geometry data */
  geom.indices = indices;
  geom.interf = interf;
  geom.position = position;
  geom.data = ctx;
  res = senc_scene_add_geometry
    (senc_scn, (unsigned)ntris, geometry_indices_3d, geometry_media, NULL,
     (unsigned)nverts, geometry_position_3d, &geom);
  if(res != RES_OK) goto error;

  /* Launch the scene analyze */
  res = senc_scene_analyze(senc_scn, &desc);
  if(res != RES_OK) goto error;

exit:
  if(senc) SENC(device_ref_put(senc));
  if(senc_scn) SENC(scene_ref_put(senc_scn));
  if(out_desc) *out_desc = desc;
  return res;
error:
  if(desc) {
    SENC(descriptor_ref_put(desc));
    desc = NULL;
  }
  goto exit;
}

static res_T
register_medium(struct sdis_scene* scn, struct sdis_medium* mdm)
{
  unsigned id;
  size_t nmedia;
  res_T res = RES_OK;
  ASSERT(scn && mdm);

  /* Check that the front medium is already registered against the scene */
  id = medium_get_id(mdm);
  nmedia = darray_medium_size_get(&scn->media);
  if(id >= nmedia) {
    res = darray_medium_resize(&scn->media, id + 1);
    if(res != RES_OK) return res;
  }
  if(darray_medium_cdata_get(&scn->media)[id]) {
    ASSERT(darray_medium_cdata_get(&scn->media)[id] == mdm);
  } else {
    /* Do not take a reference onto the medium since we already take a
     * reference onto at least one interface that uses it, and thus that has a
     * reference onto it */
    darray_medium_data_get(&scn->media)[id] = mdm;
  }
  return RES_OK;
}

static res_T
setup_properties
  (struct sdis_scene* scn,
   struct senc_descriptor* desc,
   void (*interf)(const size_t itri, struct sdis_interface**, void*),
   void* ctx)
{
  unsigned itri, ntris;
  res_T res = RES_OK;
  ASSERT(scn && interf);

  clear_properties(scn);

  SENC(descriptor_get_global_triangles_count(desc, &ntris));
  FOR_EACH(itri, 0, ntris) {
    struct prim_prop* prim_prop;
    struct sdis_interface* itface;
    unsigned enclosures[2];
    unsigned itri_adjusted; /* Triangle id in user space */
    unsigned id;
    size_t ninterfaces;

    /* Retrieve the triangle id in user space */
    SENC(descriptor_get_global_triangle_global_id(desc, itri, &itri_adjusted));

    /* Fetch the enclosures that the triangle splits */
    SENC(descriptor_get_global_triangle_enclosures(desc, itri, enclosures));

    /* Fetch the interface of the primitive */
    interf(itri, &itface, ctx);

    /* Check that the interface is already registered against the scene */
    id = interface_get_id(itface);
    ninterfaces = darray_interf_size_get(&scn->interfaces);
    if(id >= ninterfaces) {
      res = darray_interf_resize(&scn->interfaces, id + 1);
      if(res != RES_OK) goto error;
    }
    if(darray_interf_cdata_get(&scn->interfaces)[id]) {
      ASSERT(darray_interf_cdata_get(&scn->interfaces)[id] == itface);
    } else {
      SDIS(interface_ref_get(itface));
      darray_interf_data_get(&scn->interfaces)[id] = itface;
    }

    /* Register the interface media against the scene */
    res = register_medium(scn, itface->medium_front);
    if(res != RES_OK) goto error;
    res = register_medium(scn, itface->medium_back);
    if(res != RES_OK) goto error;

    /* Allocate primitive properties */
    res = darray_prim_prop_resize(&scn->prim_props, itri+1);
    if(res != RES_OK) goto error;


    /* Setup primitive properties */
    prim_prop = darray_prim_prop_data_get(&scn->prim_props) + itri;
    prim_prop->interf = itface;
    /* FIXME Ensure that the enclosure order is right one. Actually, it seems
     * that Star-Enc front facing convention is reversed wrt Stardis. Faces are
     * front facing when their vertex are CCW ordered */
    prim_prop->back_enclosure = enclosures[0];
    prim_prop->front_enclosure = enclosures[1];
  }

exit:
  return res;
error:
  clear_properties(scn);
  goto exit;
}

#if 0
static res_T
setup_geometry_2d
  (struct sdis_scene* scn,
   const size_t nsegs, /* #segments */
   void (*indices)(const size_t itri, size_t ids[2], void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[2], void* ctx),
   void* ctx)
{
  struct geometry_context context;
  struct s2d_shape* s2d_msh = NULL;
  struct s2d_scene* s2d_scn = NULL;
  struct s2d_vertex_data vdata = S2D_VERTEX_DATA_NULL;
  res_T res = RES_OK;
  ASSERT(scn && nsegs && indices && nverts && position);

  /* Setup the intermediary geometry context */
  context.indices = indices;
  context.position = position;
  context.data = ctx;

  /* Setup the vertex data */
  vdata.usage = S2D_POSITION;
  vdata.type = S2D_FLOAT2;
  vdata.get = get_position_2d;

  /* Create the Star-2D geometry */
  res = s2d_scene_create(scn->dev->s2d, &s2d_scn);
  if(res != RES_OK) goto error;
  res = s2d_shape_create_line_segments(scn->dev->s2d, &s2d_msh);
  if(res != RES_OK) goto error;
  res = s2d_line_segments_set_hit_filter_function
    (s2d_msh, hit_filter_function_2d, NULL);
  if(res != RES_OK) goto error;
  res = s2d_scene_attach_shape(s2d_scn, s2d_msh);
  if(res != RES_OK) goto error;
  res = s2d_line_segments_setup_indexed_vertices(s2d_msh, (unsigned)nsegs,
    get_indices_2d, (unsigned)nverts, &vdata, 1, &context);
  if(res != RES_OK) goto error;
  res = s2d_scene_view_create(s2d_scn, S2D_SAMPLE|S2D_TRACE|S2D_GET_PRIMITIVE,
    &scn->s2d_view);
  if(res != RES_OK) goto error;

exit:
  if(s2d_msh) S2D(shape_ref_put(s2d_msh));
  if(s2d_scn) S2D(scene_ref_put(s2d_scn));
  return res;
error:
  if(scn->s2d_view) S2D(scene_view_ref_put(scn->s2d_view));
  goto exit;
}
#endif

static res_T
setup_scene_geometry_3d(struct sdis_scene* scn, struct senc_descriptor* desc)
{
  struct s3d_shape* s3d_msh = NULL;
  struct s3d_scene* s3d_scn = NULL;
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  unsigned ntris, nverts;
  res_T res = RES_OK;
  ASSERT(scn && desc);

  SENC(descriptor_get_global_triangles_count(desc, &ntris));
  SENC(descriptor_get_global_vertices_count(desc, &nverts));

  /* Setup the vertex data */
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = descriptor_position_3d;

  /* Create the Star-3D geometry of the whole scene */
  #define CALL(Func)  { if(RES_OK != (res = Func)) goto error; } (void)0
  CALL(s3d_scene_create(scn->dev->s3d, &s3d_scn));
  CALL(s3d_shape_create_mesh(scn->dev->s3d, &s3d_msh));
  CALL(s3d_mesh_set_hit_filter_function(s3d_msh, hit_filter_function_3d, NULL));
  CALL(s3d_scene_attach_shape(s3d_scn, s3d_msh));
  CALL(s3d_mesh_setup_indexed_vertices(s3d_msh, ntris, descriptor_indices_3d,
    nverts, &vdata, 1, desc));
  CALL(s3d_scene_view_create(s3d_scn, S3D_TRACE|S3D_GET_PRIMITIVE,
    &scn->s3d_view));
  #undef CALL

exit:
  if(s3d_msh) S3D(shape_ref_put(s3d_msh));
  if(s3d_scn) S3D(scene_ref_put(s3d_scn));
  return res;
error:
  if(scn->s3d_view) S3D(scene_view_ref_put(scn->s3d_view));
  goto exit;
}

static res_T
setup_enclosure_geometry_3d(struct sdis_scene* scn, struct senc_enclosure* enc)
{
  struct s3d_scene* s3d_scn = NULL;
  struct s3d_shape* s3d_msh = NULL;
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  const struct enclosure_header* header;
  struct enclosure enc_dummy;
  struct enclosure* enc_data;
  unsigned itri, ntris, nverts;
  res_T res = RES_OK;
  ASSERT(scn && enc);

  enclosure_init(scn->dev->allocator, &enc_dummy);

  SENC(enclosure_get_header(enc, &header));
  ntris = header->triangle_count;
  nverts = header->vertices_count;

  /* Register the enclosure into the scene. Use a dummy data on their
   * registration. We are going to setup the data after their registration into
   * the hash table in order to avoid a costly copy. In other words, the
   * following hash table registration can be seen as an allocation of the
   * enclosure data that are then setup. */
  res = htable_enclosure_set(&scn->enclosures, &header->enclosure_id, &enc_dummy);
  if(res != RES_OK) goto error;

  /* Fetch the data of the registered enclosure */
  enc_data = htable_enclosure_find(&scn->enclosures, &header->enclosure_id);
  ASSERT(enc_data != NULL);

  /* Setup the vertex data */
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = enclosure_position_3d;

  /* Create the Star-3D geometry */
  #define CALL(Func)  { if(RES_OK != (res = Func)) goto error; } (void)0
  CALL(s3d_scene_create(scn->dev->s3d, &s3d_scn));
  CALL(s3d_shape_create_mesh(scn->dev->s3d, &s3d_msh));
  CALL(s3d_scene_attach_shape(s3d_scn, s3d_msh));
  CALL(s3d_mesh_setup_indexed_vertices(s3d_msh, ntris, enclosure_indices_3d,
    nverts, &vdata, 1, enc));
  CALL(s3d_scene_view_create(s3d_scn, S3D_SAMPLE, &enc_data->s3d_view));
  #undef CALL

  /* Define the identifier of the enclosure primitives in the whole scene */
  res = darray_uint_resize(&enc_data->local2global, ntris);
  if(res != RES_OK) goto error;
  FOR_EACH(itri, 0, ntris) {
    SENC(enclosure_get_triangle_global_id
      (enc, itri, darray_uint_data_get(&enc_data->local2global)+itri));
  }

exit:
  enclosure_release(&enc_dummy);
  if(s3d_msh) S3D(shape_ref_put(s3d_msh));
  if(s3d_scn) S3D(scene_ref_put(s3d_scn));
  return res;
error:
  htable_enclosure_erase(&scn->enclosures, &header->enclosure_id);
  goto exit;
}

static res_T
setup_enclosures_3d(struct sdis_scene* scn, struct senc_descriptor* desc)
{
  struct senc_enclosure* enc = NULL;
  unsigned ienc, nencs;
  res_T res = RES_OK;
  ASSERT(scn && desc);

  SENC(descriptor_get_enclosure_count(desc, &nencs));
  FOR_EACH(ienc, 0, nencs) {
    const struct enclosure_header* header;
    const struct sdis_medium* mdm;

    SENC(descriptor_get_enclosure(desc, ienc, &enc));
    SENC(enclosure_get_header(enc, &header));

    ASSERT(header->enclosed_medium < darray_medium_size_get(&scn->media));
    mdm = darray_medium_cdata_get(&scn->media)[header->enclosed_medium];
    ASSERT(mdm);

    /* Silently discard the solid enclosures */
    if(mdm->type == SDIS_MEDIUM_FLUID) {
      res = setup_enclosure_geometry_3d(scn, enc);
      if(res != RES_OK) goto error;
    }
    SENC(enclosure_ref_put(enc));
    enc = NULL;
  }

exit:
  if(enc) SENC(enclosure_ref_put(enc));
  return res;
error:
  goto exit;
}

static res_T
scene_create
  (struct sdis_device* dev,
   const int is_2d,
   const size_t nprims, /* #primitives */
   void (*indices)(const size_t iprim, size_t ids[], void*),
   void (*interf)(const size_t iprim, struct sdis_interface** bound, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[], void* ctx),
   void* ctx,
   struct sdis_scene** out_scn)
{
  struct senc_descriptor* desc = NULL;
  struct sdis_scene* scn = NULL;
  res_T res = RES_OK;

  if(!dev || !out_scn || !nprims || !indices || !interf || !nverts
  || !position || nprims > UINT_MAX || nverts > UINT_MAX) {
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
  scn->ambient_radiative_temperature = -1;
  darray_interf_init(dev->allocator, &scn->interfaces);
  darray_medium_init(dev->allocator, &scn->media);
  darray_prim_prop_init(dev->allocator, &scn->prim_props);
  htable_enclosure_init(dev->allocator, &scn->enclosures);

  if(is_2d) FATAL("2D is not supported yet.\n");

  res = run_analyze_3d(scn, nprims, indices, interf, nverts, position, ctx, &desc);
  if(res != RES_OK) {
    log_err(dev, "%s: error during the scene analysis.\n", FUNC_NAME);
    goto error;
  }
  res = setup_properties(scn, desc, interf, ctx);
  if(res != RES_OK) {
    log_err(dev, "%s: could not setup the scene interfaces and their media.\n",
      FUNC_NAME);
    goto error;
  }
  res = setup_scene_geometry_3d(scn, desc);
  if(res != RES_OK) {
    log_err(dev, "%s: could not setup the scene geometry.\n", FUNC_NAME);
    goto error;
  }
  res = setup_enclosures_3d(scn, desc);
  if(res != RES_OK) {
    log_err(dev, "%s: could not setup the enclosures.\n", FUNC_NAME);
    goto error;
  }

exit:
  if(out_scn) *out_scn = scn;
  if(desc) SENC(descriptor_ref_put(desc));
  return res;
error:
  if(scn) {
    SDIS(scene_ref_put(scn));
    scn = NULL;
  }
  goto exit;
}

static INLINE res_T
scene_get_medium_2d
  (const struct sdis_scene* scn,
   const double pos[2],
   const struct sdis_medium** out_medium)
{
  const struct sdis_medium* medium = NULL;
  size_t iprim, nprims;
  size_t nfailures = 0;
  const size_t max_failures = 10;
  res_T res = RES_OK;
  ASSERT(scn && pos);

  S2D(scene_view_primitives_count(scn->s2d_view, &nprims));
  FOR_EACH(iprim, 0, nprims) {
    struct s2d_hit hit;
    struct s2d_attrib attr;
    struct s2d_primitive prim;
    float s;
    const float range[2] = {0.f, FLT_MAX};
    float N[2], P[2], dir[2], cos_N_dir;
    s = 1.f / 3.f;

    /* Retrieve a position onto the primitive */
    S2D(scene_view_get_primitive(scn->s2d_view, (unsigned)iprim, &prim));
    S2D(primitive_get_attrib(&prim, S2D_POSITION, s, &attr));

    /* Trace a ray from the randomw walk vertex  toward the retrieved primitive
     * position */
    f2_normalize(dir, f2_sub(dir, attr.value, f2_set_d2(P, pos)));
    S2D(scene_view_trace_ray(scn->s2d_view, P, dir, range, NULL, &hit));

    f2_normalize(N, hit.normal);
    cos_N_dir = f2_dot(N, dir);

    /* Unforeseen error. One has to intersect a primitive ! */
    if(S2D_HIT_NONE(&hit)) {
      ++nfailures;
      if(nfailures < max_failures) {
        continue;
      } else {
        res = RES_BAD_ARG;
        goto error;
      }
    }

    if(absf(cos_N_dir) > 1.e-1f) { /* Not roughly orthognonal */
      const struct sdis_interface* interf;
      interf = scene_get_interface(scn, hit.prim.prim_id);
      medium = interface_get_medium
        (interf, cos_N_dir < 0 ? SDIS_FRONT : SDIS_BACK);
      break;
    }
  }

exit:
  *out_medium = medium;
  return res;
error:
  log_err(scn->dev, "%s: could not retrieve the medium at {%g, %g}.\n",
    FUNC_NAME, SPLIT2(pos));
  goto exit;
}

static INLINE res_T
scene_get_medium_3d
  (const struct sdis_scene* scn,
   const double pos[3],
   const struct sdis_medium** out_medium)
{
  const struct sdis_medium* medium = NULL;
  size_t iprim, nprims;
  size_t nfailures = 0;
  const size_t max_failures = 10;
  res_T res = RES_OK;
  ASSERT(scn && pos);

  S3D(scene_view_primitives_count(scn->s3d_view, &nprims));
  FOR_EACH(iprim, 0, nprims) {
    struct s3d_hit hit;
    struct s3d_attrib attr;
    struct s3d_primitive prim;
    float st[2];
    const float range[2] = {0.f, FLT_MAX};
    float N[3], P[3], dir[3], cos_N_dir;
    st[0] = st[1] = 1.f / 3.f; /* Or MSVC will issue a warning */

    /* Retrieve a position onto the primitive */
    S3D(scene_view_get_primitive(scn->s3d_view, (unsigned)iprim, &prim));
    S3D(primitive_get_attrib(&prim, S3D_POSITION, st, &attr));

    /* Trace a ray from the randomw walk vertex  toward the retrieved primitive
     * position */
    f3_normalize(dir, f3_sub(dir, attr.value, f3_set_d3(P, pos)));
    S3D(scene_view_trace_ray(scn->s3d_view, P, dir, range, NULL, &hit));

    f3_normalize(N, hit.normal);
    cos_N_dir = f3_dot(N, dir);

    /* Unforeseen error. One has to intersect a primitive ! */
    if(S3D_HIT_NONE(&hit)) {
      ++nfailures;
      if(nfailures < max_failures) {
        continue;
      } else {
        res = RES_BAD_ARG;
        goto error;
      }
    }

    if(absf(cos_N_dir) > 1.e-1f) { /* Not roughly orthognonal */
      const struct sdis_interface* interf;
      interf = scene_get_interface(scn, hit.prim.prim_id);
      medium = interface_get_medium
        (interf, cos_N_dir < 0 ? SDIS_FRONT : SDIS_BACK);
      break;
    }
  }

exit:
  *out_medium = medium;
  return res;
error:
  log_err(scn->dev, "%s: could not retrieve the medium at {%g, %g, %g}.\n",
    FUNC_NAME, SPLIT3(pos));
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
  clear_properties(scn);
  darray_interf_release(&scn->interfaces);
  darray_medium_release(&scn->media);
  darray_prim_prop_release(&scn->prim_props);
  htable_enclosure_release(&scn->enclosures);
  if(scn->s2d_view) S2D(scene_view_ref_put(scn->s2d_view));
  if(scn->s3d_view) S3D(scene_view_ref_put(scn->s3d_view));
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
   void (*interf)(const size_t itri, struct sdis_interface** bound, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[3], void* ctx),
   void* ctx,
   struct sdis_scene** out_scn)
{
  return scene_create
    (dev, 0/*is_2D*/, ntris, indices, interf, nverts, position, ctx, out_scn);
}

res_T
sdis_scene_2d_create
  (struct sdis_device* dev,
   const size_t nsegs, /* #segments */
   void (*indices)(const size_t itri, size_t ids[2], void*),
   void (*interf)(const size_t itri, struct sdis_interface** bound, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[2], void* ctx),
   void* ctx,
   struct sdis_scene** out_scn)
{
  return scene_create
    (dev, 1/*is_2D*/, nsegs, indices, interf, nverts, position, ctx, out_scn);
}

res_T
sdis_scene_ref_get(struct sdis_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_get(&scn->ref);
  return RES_OK;
}

res_T
sdis_scene_ref_put(struct sdis_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_put(&scn->ref, scene_release);
  return RES_OK;
}

res_T
sdis_scene_get_aabb
  (const struct sdis_scene* scn, double lower[], double upper[])
{
  float low[3], upp[3];
  res_T res = RES_OK;
  if(!scn || !lower || !upper) return RES_BAD_ARG;

  if(scene_is_2d(scn)) {
    res = s2d_scene_view_get_aabb(scn->s2d_view, low, upp);
    if(res != RES_OK) return res;
    d2_set_f2(lower, low);
    d2_set_f2(upper, upp);
  } else {
    res = s3d_scene_view_get_aabb(scn->s3d_view, low, upp);
    if(res != RES_OK) return res;
    d3_set_f3(lower, low);
    d3_set_f3(upper, upp);
  }
  return RES_OK;
}

res_T
sdis_scene_get_boundary_position
  (const struct sdis_scene* scn,
   const size_t iprim,
   const double uv[],
   double pos[])
{
  if(!scn || !uv || !pos) return RES_BAD_ARG;
  if(iprim >= scene_get_primitives_count(scn)) return RES_BAD_ARG;

  if(scene_is_2d(scn)) {
    struct s2d_primitive prim;
    struct s2d_attrib attr;
    float s = (float)uv[0];

    S2D(scene_view_get_primitive(scn->s2d_view, (unsigned int)iprim, &prim));
    S2D(primitive_get_attrib(&prim, S2D_POSITION, s, &attr));
    d2_set_f2(pos, attr.value);
  } else {
    struct s3d_primitive prim;
    struct s3d_attrib attr;
    float st[2];

    f2_set_d2(st, uv);
    S3D(scene_view_get_primitive(scn->s3d_view, (unsigned int)iprim, &prim));
    S3D(primitive_get_attrib(&prim, S3D_POSITION, st, &attr));
    d3_set_f3(pos, attr.value);
  }
  return RES_OK;
}

/*******************************************************************************
 * Local miscellaneous function
 ******************************************************************************/
const struct sdis_interface*
scene_get_interface(const struct sdis_scene* scn, const unsigned iprim)
{
  ASSERT(scn && iprim < darray_prim_prop_size_get(&scn->prim_props));
  return darray_prim_prop_cdata_get(&scn->prim_props)[iprim].interf;
}

res_T
scene_get_medium
  (const struct sdis_scene* scn,
   const double pos[],
   const struct sdis_medium** out_medium)
{
  return scene_is_2d(scn)
    ? scene_get_medium_2d(scn, pos, out_medium)
    : scene_get_medium_3d(scn, pos, out_medium);
}

