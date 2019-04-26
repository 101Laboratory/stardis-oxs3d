/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_SCENE_DIMENSION

#ifndef SDIS_SCENE_XD_H
#define SDIS_SCENE_XD_H

#include "sdis_interface_c.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <rsys/rsys.h>

/*******************************************************************************
 * Define the helper functions and the data types used by the scene
 * independently of its dimension, i.e. 2D or 3D.
 ******************************************************************************/
/* Context used to wrap the user geometry and interfaces to Star-Enc */
struct geometry {
  void (*indices)(const size_t iprim, size_t ids[], void*);
  void (*interf)(const size_t iprim, struct sdis_interface**, void*);
  void (*position)(const size_t ivert, double pos[], void*);
  void* data;
};

/* Fetch the media split by the primitive `iprim'. This first and second media
 * are the media from the front face side and back face side of the primitive,
 * respectively. */
static void
geometry_media(const unsigned iprim, unsigned media[2], void* data)
{
  struct geometry* ctx = data;
  struct sdis_interface* interf;
  ASSERT(ctx && media);
  ctx->interf(iprim, &interf, ctx->data);
  media[0] = medium_get_id(interf->medium_front);
  media[1] = medium_get_id(interf->medium_back);
}

/* Register the submitted medium against the scene if it is not already
 * registered. On registration, no reference is taken onto the medium; the
 * scene references its media through its interfaces and it is thus useless to
 * take another reference onto them. */
static res_T
register_medium(struct sdis_scene* scn, struct sdis_medium* mdm)
{
  unsigned id;
  size_t nmedia;
  res_T res = RES_OK;
  ASSERT(scn && mdm);

  /* Check that the medium is already registered against the scene */
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

/* Release the reference toward the interfaces and thus clear the list of scene
 * interfaces, the list of scene media, and the list of per-primitive
 * interface. */
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

#endif /* SDIS_SCENE_XD_H */
#else /* !SDIS_SCENE_DIMENSION */

#include "sdis_device_c.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/mem_allocator.h>

#include <limits.h>

/* Check the submitted dimension and include its specific headers */
#if (SDIS_SCENE_DIMENSION == 2)
  #include <star/senc2d.h>
  #include <star/s2d.h>
#elif (SDIS_SCENE_DIMENSION == 3)
  #include <star/senc.h>
  #include <star/s3d.h>
#else
  #error "Invalid SDIS_SCENE_DIMENSION value."
#endif

/* Syntactic sugar */
#define DIM SDIS_SCENE_DIMENSION

/* Star-Enc macros generic to the SDIS_SCENE_DIMENSION */
#if DIM == 2
  #define sencXd(Name) CONCAT(senc2d_, Name)
  #define SENCXD SENC2D
#else
  #define sencXd(Name) CONCAT(senc_, Name)
  #define SENCXD SENC
#endif

/* Star-XD macros generic to SDIS_SCENE_DIMENSION */
#define sXd(Name) CONCAT(CONCAT(CONCAT(s, DIM), d_), Name)
#define SXD CONCAT(CONCAT(S, DIM), D)
#define SXD_VERTEX_DATA_NULL CONCAT(CONCAT(S,DIM),D_VERTEX_DATA_NULL)
#define SXD_POSITION CONCAT(CONCAT(S, DIM), D_POSITION)
#define SXD_TRACE CONCAT(CONCAT(S,DIM), D_TRACE)
#define SXD_SAMPLE CONCAT(CONCAT(S,DIM), D_SAMPLE)
#define SXD_GET_PRIMITIVE CONCAT(CONCAT(S,DIM), D_GET_PRIMITIVE)
#define SXD_HIT_NONE CONCAT(CONCAT(S,DIM), D_HIT_NONE)
#define SXD_PRIMITIVE_EQ CONCAT(CONCAT(S,DIM), D_PRIMITIVE_EQ)

/* Vector macros generic to SDIS_SCENE_DIMENSION */
#define fX(Func) CONCAT(CONCAT(CONCAT(f, DIM), _), Func)
#define fX_set_dX CONCAT(CONCAT(CONCAT(f, DIM), _set_d), DIM)

/* Macro making generic its subimitted name to SDIS_SCENE_DIMENSION */
#define XD(Name) CONCAT(CONCAT(CONCAT(Name, _), DIM), d)

#if DIM == 2
  #define HIT_ON_BOUNDARY hit_on_vertex
#else
  #define HIT_ON_BOUNDARY hit_on_edge
#endif

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
#if DIM == 2
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

#else  /* DIM == 3 */
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
#endif /* DIM == 2 */

/* Avoid self-intersection for a ray starting from a planar primitive, i.e. a
 * triangle or a line segment */
static int
XD(hit_filter_function)
  (const struct sXd(hit)* hit,
   const float org[DIM],
   const float dir[DIM],
   void* ray_data,
   void* filter_data)
{
  const struct sXd(hit)* hit_from = ray_data;
  (void)org, (void)dir, (void)filter_data;

  if(!hit_from || SXD_HIT_NONE(hit_from)) return 0; /* No filtering */

  if(SXD_PRIMITIVE_EQ(&hit_from->prim, &hit->prim)) return 1;

  if(eq_epsf(hit->distance, 0, 1.e-6f)) {
    /* If the targeted point is near of the origin, check that it lies on an
     * edge/vertex shared by the 2 primitives. */
#if DIM == 2
    return hit_on_vertex(hit_from) && hit_on_vertex(hit);
#else
    return hit_on_edge(hit_from) && hit_on_edge(hit);
#endif
  }
  return 0;
}

/* Retrieve the indices of `struct geometry' primitive */
static void
XD(geometry_indices)(const unsigned iprim, unsigned out_ids[DIM], void* data)
{
  struct geometry* ctx = data;
  size_t ids[DIM];
  int i;
  ASSERT(ctx && out_ids);
  ctx->indices(iprim, ids, ctx->data);
  FOR_EACH(i, 0, DIM) out_ids[i] = (unsigned)ids[i];
}

/* Retrieve the coordinates of `struct geometry' vertex */
static void
XD(geometry_position)(const unsigned ivert, double out_pos[DIM], void* data)
{
  struct geometry* ctx = data;
  double pos[DIM];
  int i;
  ASSERT(ctx && out_pos);
  ctx->position(ivert, pos, ctx->data);
  FOR_EACH(i, 0, DIM) out_pos[i] = pos[i];
}

/* Retrieve the indices of a primitive of a Star-EncXD descriptor */
static void
XD(descriptor_indices)(const unsigned iprim, unsigned ids[DIM], void* data)
{
  struct sencXd(descriptor)* desc = data;
#if DIM == 2
  SENCXD(descriptor_get_global_segment(desc, iprim, ids));
#else
  SENCXD(descriptor_get_global_triangle(desc, iprim, ids));
#endif
}

/* Retrieve the coordinates of a vertex of a Star-EncXD descriptor */
static void
XD(descriptor_position)(const unsigned ivert, float out_pos[DIM], void* data)
{
  struct sencXd(descriptor)* desc = data;
  double pos[3];
  int i;
  SENCXD(descriptor_get_global_vertex(desc, ivert, pos));
  FOR_EACH(i, 0, DIM) out_pos[i] = (float)pos[i];
}

/* Retrieve the indices of a primitive of a Star-EncXD enclosure */
static void
XD(enclosure_indices)(const unsigned iprim, unsigned ids[DIM], void* data)
{
  struct sencXd(enclosure)* enc = data;
#if DIM == 2
  SENCXD(enclosure_get_segment(enc, iprim, ids));
#else
  SENCXD(enclosure_get_triangle(enc, iprim, ids));
#endif
}

/* Retrieve the coordinates of a vertex of a Star-EncXD encolsure */
static void
XD(enclosure_position)(const unsigned ivert, float out_pos[DIM], void* data)
{
  struct sencXd(enclosure)* enc = data;
  double pos[DIM];
  int i;
  ASSERT(out_pos);
  SENCXD(enclosure_get_vertex(enc, ivert, pos));
  FOR_EACH(i, 0, DIM) out_pos[i] = (float)pos[i];
}

/* Use Star-EncXD to analyze the user defined data. It essentially cleans-up
 * the geometry and extracts the enclosures wrt to the submitted media. Note
 * that data inconsistencies are also detected by this function. In this case
 * an error is returned. */
static res_T
XD(run_analyze)
  (struct sdis_scene* scn,
   const size_t nprims, /* #primitives */
   void (*indices)(const size_t iprim, size_t ids[], void*),
   void (interf)(const size_t iprim, struct sdis_interface**, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[], void*),
   void* ctx,
   struct sencXd(descriptor)** out_desc)
{
  struct geometry geom;
  struct sencXd(device)* senc = NULL;
  struct sencXd(scene)* senc_scn = NULL;
  struct sencXd(descriptor)* desc = NULL;
  res_T res = RES_OK;
  ASSERT(scn && nprims && indices && interf && nverts && position && out_desc);

  res = sencXd(device_create)(scn->dev->logger, scn->dev->allocator,
    1/*scn->dev->nthreads*/, scn->dev->verbose, &senc);
  if(res != RES_OK) goto error;

  res = sencXd(scene_create)(senc,
#if DIM == 2
    SENC2D_CONVENTION_NORMAL_BACK | SENC2D_CONVENTION_NORMAL_OUTSIDE,
#else
    SENC_CONVENTION_NORMAL_BACK | SENC_CONVENTION_NORMAL_OUTSIDE,
#endif
    &senc_scn);
  if(res != RES_OK) goto error;

  /* Setup the geometry data */
  geom.indices = indices;
  geom.interf = interf;
  geom.position = position;
  geom.data = ctx;
  res = sencXd(scene_add_geometry)
    (senc_scn, (unsigned)nprims, XD(geometry_indices), geometry_media, NULL,
     (unsigned)nverts, XD(geometry_position), &geom);
  if(res != RES_OK) goto error;

  /* Launch the scene analyze */
  res = sencXd(scene_analyze)(senc_scn, &desc);
  if(res != RES_OK) goto error;

exit:
  if(senc) SENCXD(device_ref_put(senc));
  if(senc_scn) SENCXD(scene_ref_put(senc_scn));
  if(out_desc) *out_desc = desc;
  return res;
error:
  if(desc) {
    SENCXD(descriptor_ref_put(desc));
    desc = NULL;
  }
  goto exit;

}

/* Register the media and the interfaces, map each primitive to its interface
 * and associated to each primitive the identifier of the enclosures that it
 * splits. */
static res_T
XD(setup_properties)
  (struct sdis_scene* scn,
   struct sencXd(descriptor)* desc,
   void (*interf)(const size_t itri, struct sdis_interface**, void*),
   void* ctx)
{
  unsigned iprim, nprims;
  res_T res = RES_OK;
  ASSERT(scn && interf);

  clear_properties(scn);

#if DIM == 2
  SENCXD(descriptor_get_global_segments_count(desc, &nprims));
#else
  SENCXD(descriptor_get_global_triangles_count(desc, &nprims));
#endif
  FOR_EACH(iprim, 0, nprims) {
    struct prim_prop* prim_prop;
    struct sdis_interface* itface;
    unsigned enclosures[2];
    unsigned iprim_adjusted; /* Primitive id in user space */
    unsigned id;
    int i;
    double* enc_upper_bound;
    size_t ninterfaces;

#if DIM == 2
    /* Retrieve the triangle id in user space */
    SENCXD(descriptor_get_global_segment_global_id(desc, iprim, &iprim_adjusted));
    /* Fetch the enclosures that the segment splits */
    SENCXD(descriptor_get_global_segment_enclosures(desc, iprim, enclosures));
#else
    /* Retrieve the triangle id in user space */
    SENCXD(descriptor_get_global_triangle_global_id(desc, iprim, &iprim_adjusted));
    /* Fetch the enclosures that the triangle splits */
    SENCXD(descriptor_get_global_triangle_enclosures(desc, iprim, enclosures));
#endif

    /* Fetch the interface of the primitive */
    interf(iprim_adjusted, &itface, ctx);

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
    res = darray_prim_prop_resize(&scn->prim_props, iprim+1);
    if(res != RES_OK) goto error;

    /* Setup primitive properties */
    prim_prop = darray_prim_prop_data_get(&scn->prim_props) + iprim;
    prim_prop->interf = itface;
    prim_prop->front_enclosure = enclosures[0];
    prim_prop->back_enclosure = enclosures[1];

    /* Build per-interface hc upper bounds in a tmp table */
    FOR_EACH(i, 0, 2) {
      double hc_ub = interface_get_convection_coef_upper_bound(itface);
      enc_upper_bound = htable_d_find(&scn->tmp_hc_ub, enclosures+i);
      if(!enc_upper_bound) {
        res = htable_d_set(&scn->tmp_hc_ub, enclosures+i, &hc_ub);
      } else {
        *enc_upper_bound = MMAX(*enc_upper_bound, hc_ub);
      }
    }
  }

exit:
  return res;
error:
  clear_properties(scn);
  goto exit;
}

/* Build the Star-XD scene view of the whole scene */
static res_T
XD(setup_scene_geometry)(struct sdis_scene* scn, struct sencXd(descriptor)* desc)
{
  struct sXd(device)* sXd_dev = NULL;
  struct sXd(shape)* sXd_shape = NULL;
  struct sXd(scene)* sXd_scn = NULL;
  struct sXd(vertex_data) vdata = SXD_VERTEX_DATA_NULL;
  unsigned nprims, nverts;
  res_T res = RES_OK;
  ASSERT(scn && desc);

  SENCXD(descriptor_get_global_vertices_count(desc, &nverts));

  /* Setup the vertex data */
  vdata.usage = SXD_POSITION;
#if DIM == 2
  vdata.type = S2D_FLOAT2;
#else
  vdata.type = S3D_FLOAT3;
#endif
  vdata.get = XD(descriptor_position);

  /* Create the Star-XD geometry of the whole scene */
  #define CALL(Func)  { if(RES_OK != (res = Func)) goto error; } (void)0
#if DIM == 2
  sXd_dev = scn->dev->s2d;
  SENCXD(descriptor_get_global_segments_count(desc, &nprims));
  CALL(sXd(shape_create_line_segments)(sXd_dev, &sXd_shape));
  CALL(sXd(line_segments_set_hit_filter_function)(sXd_shape,
    XD(hit_filter_function), NULL));
  CALL(sXd(line_segments_setup_indexed_vertices)(sXd_shape, nprims,
    XD(descriptor_indices), nverts, &vdata, 1, desc));
#else
  sXd_dev = scn->dev->s3d;
  SENCXD(descriptor_get_global_triangles_count(desc, &nprims));
  CALL(sXd(shape_create_mesh)(sXd_dev, &sXd_shape));
  CALL(sXd(mesh_set_hit_filter_function)(sXd_shape, XD(hit_filter_function), NULL));
  CALL(sXd(mesh_setup_indexed_vertices)(sXd_shape, nprims, XD(descriptor_indices),
    nverts, &vdata, 1, desc));
#endif
  CALL(sXd(scene_create)(sXd_dev, &sXd_scn));
  CALL(sXd(scene_attach_shape)(sXd_scn, sXd_shape));
  CALL(sXd(scene_view_create)(sXd_scn, SXD_TRACE|SXD_GET_PRIMITIVE,
    &scn->sXd(view)));
  #undef CALL

exit:
  if(sXd_shape) SXD(shape_ref_put(sXd_shape));
  if(sXd_scn) SXD(scene_ref_put(sXd_scn));
  return res;
error:
  if(scn->sXd(view)) SXD(scene_view_ref_put(scn->sXd(view)));
  goto exit;
}

/* Build the Star-XD scene view of a specific enclosure and map their local
 * primitive id to their primitive id in the whole scene */
static res_T
XD(setup_enclosure_geometry)(struct sdis_scene* scn, struct sencXd(enclosure)* enc)
{
  struct sXd(device)* sXd_dev = NULL;
  struct sXd(scene)* sXd_scn = NULL;
  struct sXd(shape)* sXd_shape = NULL;
  struct sXd(vertex_data) vdata = SXD_VERTEX_DATA_NULL;
  struct enclosure enc_dummy;
  struct enclosure* enc_data;
  float S, V;
  double* p_ub;
  unsigned iprim, nprims, nverts;
#if DIM == 2
  struct senc2d_enclosure_header header;
#else
  struct senc_enclosure_header header;
#endif
  res_T res = RES_OK;
  ASSERT(scn && enc);

  enclosure_init(scn->dev->allocator, &enc_dummy);

  SENCXD(enclosure_get_header(enc, &header));
#if DIM == 2
  sXd_dev = scn->dev->s2d;
  nprims = header.segment_count;
#else
  sXd_dev = scn->dev->s3d;
  nprims = header.triangle_count;
#endif
  nverts = header.vertices_count;

  /* Register the enclosure into the scene. Use a dummy data on their
   * registration; in order to avoid a costly copy, we are going to setup the
   * registered data rather than a local data that would be then registered. In
   * other words, the following hash table registration can be seen as an
   * allocation of the enclosure data to setup. */
  res = htable_enclosure_set(&scn->enclosures, &header.enclosure_id, &enc_dummy);
  if(res != RES_OK) goto error;

  /* Fetch the data of the registered enclosure */
  enc_data = htable_enclosure_find(&scn->enclosures, &header.enclosure_id);
  ASSERT(enc_data != NULL);

    /* Setup the vertex data */
  vdata.usage = SXD_POSITION;
#if DIM == 2
  vdata.type = S2D_FLOAT2;
#else
  vdata.type = S3D_FLOAT3;
#endif
  vdata.get = XD(enclosure_position);

  /* Create the Star-XD geometry */
  #define CALL(Func)  { if(RES_OK != (res = Func)) goto error; } (void)0
#if DIM == 2
  CALL(sXd(shape_create_line_segments)(sXd_dev, &sXd_shape));
  CALL(sXd(line_segments_setup_indexed_vertices)(sXd_shape, nprims,
    XD(enclosure_indices), nverts, &vdata, 1, enc));
#else
  CALL(sXd(shape_create_mesh)(sXd_dev, &sXd_shape));
  CALL(sXd(mesh_setup_indexed_vertices)(sXd_shape, nprims, XD(enclosure_indices),
    nverts, &vdata, 1, enc));
#endif
  CALL(sXd(scene_create)(sXd_dev, &sXd_scn));
  CALL(sXd(scene_attach_shape)(sXd_scn, sXd_shape));
  CALL(sXd(scene_view_create)(sXd_scn, SXD_SAMPLE|SXD_TRACE, &enc_data->sXd(view)));

  /* Compute the S/V ratio */
#if DIM == 2
  CALL(s2d_scene_view_compute_contour_length(enc_data->s2d_view, &S));
  CALL(s2d_scene_view_compute_area(enc_data->s2d_view, &V));
#else
  CALL(s3d_scene_view_compute_area(enc_data->s3d_view, &S));
  CALL(s3d_scene_view_compute_volume(enc_data->s3d_view, &V));
#endif
  enc_data->V = V;
  enc_data->S_over_V = S / V;
  ASSERT(enc_data->S_over_V >= 0);
  #undef CALL

    /* Set enclosure hc upper bound regardless of its media being a fluid */
  p_ub = htable_d_find(&scn->tmp_hc_ub, &header.enclosure_id);
  ASSERT(p_ub);
  enc_data->hc_upper_bound = *p_ub;

  /* Define the identifier of the enclosure primitives in the whole scene */
  res = darray_uint_resize(&enc_data->local2global, nprims);
  if(res != RES_OK) goto error;
  FOR_EACH(iprim, 0, nprims) {
#if DIM == 2
    senc2d_enclosure_get_segment_global_id
      (enc, iprim, darray_uint_data_get(&enc_data->local2global)+iprim);
#else
    senc_enclosure_get_triangle_global_id
      (enc, iprim, darray_uint_data_get(&enc_data->local2global)+iprim);
#endif
  }

  /* Setup the medium id of the enclosure */
  SENCXD(enclosure_get_medium(enc, 0, &enc_data->medium_id));

exit:
  enclosure_release(&enc_dummy);
  if(sXd_shape) SXD(shape_ref_put(sXd_shape));
  if(sXd_scn) SXD(scene_ref_put(sXd_scn));
  return res;
error:
  htable_enclosure_erase(&scn->enclosures, &header.enclosure_id);
  goto exit;
}

/* Build the Star-XD scene view and define its associated data of the finite
 * fluid enclosures */
static res_T
XD(setup_enclosures)(struct sdis_scene* scn, struct sencXd(descriptor)* desc)
{
  struct sencXd(enclosure)* enc = NULL;
  unsigned ienc, nencs;
  unsigned enclosed_medium;
  int outer_found = 0;
  res_T res = RES_OK;
  ASSERT(scn && desc);
  (void)outer_found;

  SENCXD(descriptor_get_enclosure_count(desc, &nencs));
  FOR_EACH(ienc, 0, nencs) {
#if DIM == 2
    struct senc2d_enclosure_header header;
#else
    struct senc_enclosure_header header;
#endif

    SENCXD(descriptor_get_enclosure(desc, ienc, &enc));
    SENCXD(enclosure_get_header(enc, &header));

    if(header.is_infinite) {
      ASSERT(!outer_found);
      outer_found = 1;
      scn->outer_enclosure_id = ienc;
    }

    /* As paths don't go in infinite enclosures we can accept models are broken
     * there. But nowhere else. */
    if(header.enclosed_media_count != 1 && !header.is_infinite) {
#ifndef NDEBUG
      /* Dump the problematic enclosure. */
      double tmp[DIM];
      unsigned indices[DIM];
      unsigned i;
  #if DIM == 2
      FOR_EACH(i, 0, header.vertices_count) {
        SENCXD(enclosure_get_vertex(enc, i, tmp));
        log_warn(scn->dev, "v %g %g\n", SPLIT2(tmp));
      }
      FOR_EACH(i, 0, header.segment_count) {
        ASSERT(senc2d_enclosure_get_segment(enc, i, indices) == RES_OK);
        log_warn(scn->dev, "f %u %u\n", indices[0]+1, indices[1]+1);
      }
  #else
      FOR_EACH(i, 0, header.vertices_count) {
        SENCXD(enclosure_get_vertex(enc, i, tmp));
        log_warn(scn->dev, "v %g %g %g\n", SPLIT3(tmp));
      }
      FOR_EACH(i, 0, header.triangle_count) {
        ASSERT(senc_enclosure_get_triangle(enc, i, indices) == RES_OK);
        log_warn(scn->dev, "f %u %u %u\n",
          indices[0]+1, indices[1]+1, indices[2]+1);
      }
  #endif
#endif
      res = RES_BAD_ARG;
      goto error;
    }

    SENCXD(enclosure_get_medium(enc, 0, &enclosed_medium));
    ASSERT(enclosed_medium < darray_medium_size_get(&scn->media));

    /* Silently discard infinite enclosures */
    if(!header.is_infinite) {
      res = XD(setup_enclosure_geometry)(scn, enc);
      if(res != RES_OK) goto error;
    }
    SENCXD(enclosure_ref_put(enc));
    enc = NULL;
  }

  /* tmp table no more useful */
  htable_d_purge(&scn->tmp_hc_ub);
exit:
  if(enc) SENCXD(enclosure_ref_put(enc));
  return res;
error:
  goto exit;
}

/* Create a Stardis scene */
static res_T
XD(scene_create)
  (struct sdis_device* dev,
   const size_t nprims, /* #primitives */
   void (*indices)(const size_t iprim, size_t ids[], void*),
   void (*interf)(const size_t iprim, struct sdis_interface** bound, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[], void* ctx),
   void* ctx,
   struct sdis_scene** out_scn)
{
  struct sencXd(descriptor)* desc = NULL;
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
  scn->outer_enclosure_id = UINT_MAX;
  darray_interf_init(dev->allocator, &scn->interfaces);
  darray_medium_init(dev->allocator, &scn->media);
  darray_prim_prop_init(dev->allocator, &scn->prim_props);
  htable_enclosure_init(dev->allocator, &scn->enclosures);
  htable_d_init(dev->allocator, &scn->tmp_hc_ub);

  res = XD(run_analyze)(scn, nprims, indices, interf, nverts, position, ctx, &desc);
  if(res != RES_OK) {
    log_err(dev, "%s: error during the scene analysis.\n", FUNC_NAME);
    goto error;
  }
  res = XD(setup_properties)(scn, desc, interf, ctx);
  if(res != RES_OK) {
    log_err(dev, "%s: could not setup the scene interfaces and their media.\n",
      FUNC_NAME);
    goto error;
  }
  res = XD(setup_scene_geometry)(scn, desc);
  if(res != RES_OK) {
    log_err(dev, "%s: could not setup the scene geometry.\n", FUNC_NAME);
    goto error;
  }
  res = XD(setup_enclosures)(scn, desc);
  if(res != RES_OK) {
    log_err(dev, "%s: could not setup the enclosures.\n", FUNC_NAME);
    goto error;
  }
#if DIM==2
  scn->senc2d_descriptor = desc;
#else
  scn->senc_descriptor = desc;
#endif

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

/*******************************************************************************
 * Local functions
 ******************************************************************************/
static INLINE res_T
XD(scene_get_medium)
  (const struct sdis_scene* scn,
   const double pos[DIM],
   struct get_medium_info* info, /* May be NULL */
   struct sdis_medium** out_medium)
{
  struct sdis_medium* medium = NULL;
  size_t iprim, nprims;
  size_t nfailures = 0;
  const size_t max_failures = 10;
  /* Range of the parametric coordinate into which positions are challenged */
#if DIM == 2
  float st[3];
#else
  float st[3][2];
#endif
  size_t nsteps = 3;
  res_T res = RES_OK;
  ASSERT(scn && pos);

#if DIM == 2
  st[0] = 0.25f;
  st[1] = 0.50f;
  st[2] = 0.75f;
#else
  f2(st[0], 1.f/6.f, 5.f/12.f);
  f2(st[1], 5.f/12.f, 1.f/6.f);
  f2(st[2], 5.f/12.f, 5.f/12.f);
#endif

  SXD(scene_view_primitives_count(scn->sXd(view), &nprims));
  FOR_EACH(iprim, 0, nprims) {
    struct sXd(hit) hit;
    struct sXd(attrib) attr;
    struct sXd(primitive) prim;
    const float range[2] = {0.f, FLT_MAX};
    float N[DIM], P[DIM], dir[DIM], cos_N_dir;
    size_t istep = 0;

    do {
      /* Retrieve a position onto the primitive */
      SXD(scene_view_get_primitive(scn->sXd(view), (unsigned)iprim, &prim));
      SXD(primitive_get_attrib(&prim, SXD_POSITION, st[istep], &attr));

      /* Trace a ray from the random walk vertex toward the retrieved primitive
       * position */
      fX(normalize)(dir, fX(sub)(dir, attr.value, fX_set_dX(P, pos)));
      SXD(scene_view_trace_ray(scn->sXd(view), P, dir, range, NULL, &hit));

      /* Unforeseen error. One has to intersect a primitive ! */
      if(SXD_HIT_NONE(&hit)) {
        ++nfailures;
        if(nfailures < max_failures) {
          continue;
        } else {
          res = RES_BAD_ARG;
          goto error;
        }
      }
    /* Discard the hit if it is on a vertex/edge, and target a new position
     * onto the current primitive */
    } while((SXD_HIT_NONE(&hit) || HIT_ON_BOUNDARY(&hit))
         && ++istep < nsteps);

    /* The hits of all targeted positions on the current primitive are on
     * vertices. Challenge positions on another primitive. */
    if(istep >= nsteps) continue;

    fX(normalize)(N, hit.normal);
    cos_N_dir = fX(dot)(N, dir);

    /* Not too close and not roughly orthognonal */
    if(hit.distance > 1.e-6 && absf(cos_N_dir) > 1.e-1f) {
      const struct sdis_interface* interf;
      interf = scene_get_interface(scn, hit.prim.prim_id);
      medium = interface_get_medium
        (interf, cos_N_dir < 0 ? SDIS_FRONT : SDIS_BACK);

      /* Register the get_medium_info */
      if(info) {
        fX(set)(info->pos_tgt, attr.value);
        fX(set)(info->ray_org, P);
        fX(set)(info->ray_dir, dir);
        info->XD(hit) = hit;
      }
      break;
    }
  }

exit:
  *out_medium = medium;
  return res;
error:
#if DIM == 2
  log_err(scn->dev, "%s: could not retrieve the medium at {%g, %g}.\n",
    FUNC_NAME, SPLIT2(pos));
#else
  log_err(scn->dev, "%s: could not retrieve the medium at {%g, %g, %g}.\n",
    FUNC_NAME, SPLIT3(pos));
#endif
  goto exit;
}
#undef SDIS_SCENE_DIMENSION
#undef DIM
#undef sencXd
#undef SENCXD
#undef sXd
#undef SXD
#undef SXD_VERTEX_DATA_NULL
#undef SXD_POSITION
#undef SXD_FLOAT3
#undef SXD_TRACE
#undef SXD_TRACE
#undef SXD_GET_PRIMITIVE
#undef SXD_HIT_NONE
#undef SXD_PRIMITIVE_EQ
#undef fX
#undef fX_set_dX
#undef XD
#undef HIT_ON_BOUNDARY

#endif /* !SDIS_SCENE_DIMENSION */
