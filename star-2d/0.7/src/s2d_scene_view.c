/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#include "s2d.h"
#include "s2d_c.h"
#include "s2d_device_c.h"
#include "s2d_line_segments.h"
#include "s2d_geometry.h"
#include "s2d_scene_c.h"
#include "s2d_scene_view_c.h"
#include "s2d_shape_c.h"

#include <rsys/algorithm.h>
#include <rsys/float2.h>
#include <rsys/float3.h>
#include<rsys/mem_allocator.h>

#include <limits.h>

/* Number of floats added to the vertex position in order to ensure the Embree
 * vertex padding constraint */
#define POSITION_PADDING 1

struct intersect_context {
  struct RTCRayQueryContext rtc;
  struct s2d_scene_view* scnview;
  void* data; /* User defined data */
  float ws_org[2]; /* World space ray origin */
  float ws_dir[3]; /* World space ray direction */
  float ws_range[2]; /* World space ray range */
  float cos_dir_dir2d; /* Cosine between the 3D ws_dir and its 2D projection */
  int rt_3d; /* Define if the ray is traced in 3D */
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
cmp_float(const void* a, const void* b)
{
  const float key = *(const float*)a;
  const float val = *(const float*)b;
  if(key < val) return -1;
  if(key > val) return +1;
  return 0;
}

static INLINE int
cmp_float_to_fltui(const void* a, const void* b)
{
  const float key = *(const float*)a;
  const struct fltui* fltui = (const struct fltui*)b;
  if(key < fltui->flt) return -1;
  if(key > fltui->flt) return +1;
  return 0;
}

static INLINE int
cmp_size_t_to_nprims_cdf(const void* a, const void* b)
{
  const size_t key = *(const size_t*)a;
  const struct nprims_cdf* nprims_cdf = (const struct nprims_cdf*)b;
  if(key < nprims_cdf->nprims-1) return -1;
  if(key > nprims_cdf->nprims-1) return +1;
  return 0;
}

static INLINE void
scene_view_destroy_geometry(struct s2d_scene_view* scnview, struct geometry* geom)
{
  ASSERT(geom);
  if(geom->rtc) {
    if(geom->rtc_id != RTC_INVALID_GEOMETRY_ID) {
      rtcDetachGeometry(scnview->rtc_scn, geom->rtc_id);
      geom->rtc_id = RTC_INVALID_GEOMETRY_ID;
    }
    rtcReleaseGeometry(geom->rtc);
    geom->rtc = NULL;
    scnview->rtc_scn_update = 1; /* Notify the scene upd */
  }
  geometry_ref_put(geom);
}

static void
on_shape_detach
  (const struct s2d_scene* scn,
   const struct s2d_shape* shape,
   void* data)
{
  struct geometry** pgeom;
  struct geometry* geom;
  struct s2d_scene_view* scnview = (struct s2d_scene_view*)data;
  unsigned shape_id;
  ASSERT(scn && shape && data);
  (void)scn;

  S2D(shape_get_id(shape, &shape_id));
  pgeom = htable_geom_find(&scnview->cached_geoms, &shape_id);

  /* The scnview did not register a geometry for this shape. Ignore the signal */
  if(!pgeom) return;

  geom = *pgeom;
  if(scnview->mask == 0) {
    /* The scnview is NOT in use. Directly rm the cached geometry */
    size_t n; (void)n;
    scene_view_destroy_geometry(scnview, geom);
    n = htable_geom_erase(&scnview->cached_geoms, &shape_id);
    ASSERT(n == 1);
  } else {
    /* The scnview is in use. Delay the deletion of the cached geometry */
    res_T res = darray_uint_push_back(&scnview->detached_shapes, &shape_id);
    if(res != RES_OK) FATAL("Insufficient memory.\n");
  }
}

static INLINE void
hit_setup
  (struct s2d_scene_view* scnview,
   const struct RTCRayHit* ray_hit,
   struct s2d_hit* hit)
{
  struct geometry* geom;
  ASSERT(scnview && hit && ray_hit);

  if(ray_hit->hit.geomID == RTC_INVALID_GEOMETRY_ID) { /* No hit */
    *hit = S2D_HIT_NULL;
    return;
  }
  ASSERT(eq_epsf(ray_hit->hit.Ng_z, 0.f, 1.e-6f));
  ASSERT((unsigned)(ray_hit->hit.instID[0]) == RTC_INVALID_GEOMETRY_ID);

  hit->normal[0] = ray_hit->hit.Ng_x;
  hit->normal[1] = ray_hit->hit.Ng_y;
  hit->distance = ray_hit->ray.tfar;

  /* In Embree3 the normal orientation is flipped wrt to Star-2D convention */
  #if RTC_VERSION_MAJOR >= 3
  f2_minus(hit->normal, hit->normal);
  #endif

  geom  = scene_view_geometry_from_embree_id(scnview, ray_hit->hit.geomID);
  hit->prim.mesh__ = geom;
  hit->prim.prim_id = ray_hit->hit.primID;
  hit->prim.geom_id = geom->name;
  hit->prim.scene_prim_id = hit->prim.prim_id + geom->scene_prim_id_offset;

  /* The Embree "v" parametric coordinate of the extruded quad corresponds to
   * the edge parametric coordinate (handle Embree returning v out of range) */
  hit->u = CLAMP(ray_hit->hit.v, 0, 1);

  if(geom->flip_contour) {
    f2_minus(hit->normal, hit->normal);
  }
}

static res_T
embree_geometry_register
  (struct s2d_scene_view* scnview,
   struct geometry* geom)
{
  ASSERT(scnview);

  /* Create the Embree geometry if it is not valid */
  if(geom->rtc == NULL) {
    geom->rtc = rtcNewGeometry(scnview->scn->dev->rtc, RTC_GEOMETRY_TYPE_QUAD);
    if(geom->rtc == NULL)
      return RES_UNKNOWN_ERR;

    /* Set the Star-2D representation of the geometry to the Embree geometry */
    rtcSetGeometryUserData(geom->rtc, geom);

    /* Attach the Embree geometry to the Embree scene of the scene view */
    geom->rtc_id = rtcAttachGeometry(scnview->rtc_scn, geom->rtc);

    scnview->rtc_scn_update = 1;
  }
  return RES_OK;
}

static res_T
embree_geometry_setup_positions
  (struct s2d_scene_view* scnview, struct geometry* geom)
{
  RTCBuffer buf = NULL;
  size_t nverts;
  size_t i;
  float* verts;
  float* rtc_verts;
  res_T res = RES_OK;
  ASSERT(scnview && geom && geom->rtc);

  nverts = line_segments_get_nverts(geom->lines);
  verts = line_segments_get_attr(geom->lines, S2D_POSITION);

  buf = rtcNewBuffer
    (scnview->scn->dev->rtc,
     nverts*2*sizeof(float[3]) + sizeof(float)*POSITION_PADDING);
  if(!buf) {
    res = rtc_error_to_res_T(rtcGetDeviceError(scnview->scn->dev->rtc));
    goto error;
  }

  /* Extrude segment vertices as quad whose Z is [-1, 1] */
  rtc_verts = rtcGetBufferData(buf);
  FOR_EACH(i, 0, nverts) {
    size_t ivert = i*2/*#coords per vertex*/;
    size_t rtc_ivert = i*3/*#coords*/ * 2/*#rtc vertices per line vertex*/;

    rtc_verts[rtc_ivert + 0] = verts[ivert + 0];
    rtc_verts[rtc_ivert + 1] = verts[ivert + 1];
    rtc_verts[rtc_ivert + 2] = 1.f;

    rtc_verts[rtc_ivert + 3] = verts[ivert + 0];
    rtc_verts[rtc_ivert + 4] = verts[ivert + 1];
    rtc_verts[rtc_ivert + 5] = -1.f;
  }

  rtcSetGeometryBuffer(geom->rtc, RTC_BUFFER_TYPE_VERTEX, 0/*slot*/,
    RTC_FORMAT_FLOAT3, buf, 0/*offset*/, sizeof(float[3])/*stride*/, nverts*2);
  rtcUpdateGeometryBuffer(geom->rtc, RTC_BUFFER_TYPE_VERTEX, 0/*slot*/);

exit:
  if(buf) rtcReleaseBuffer(buf);
  return res;
error:
  goto exit;
}


static INLINE res_T
embree_geometry_setup_indices
  (struct s2d_scene_view* scnview, struct geometry* geom)
{
  RTCBuffer buf = NULL;
  size_t nsegs;
  size_t i;
  uint32_t* ids;
  uint32_t* rtc_ids;
  res_T res = RES_OK;
  ASSERT(scnview && geom && geom->rtc);

  /* Define the index of the extruded line segments */
  nsegs = line_segments_get_nsegments(geom->lines);
  ids = line_segments_get_ids(geom->lines);

  buf = rtcNewBuffer(scnview->scn->dev->rtc, nsegs*sizeof(uint32_t[4]));
  if(!buf) {
    res = rtc_error_to_res_T(rtcGetDeviceError(scnview->scn->dev->rtc));
    goto error;
  }

  /* Define the index of the extruded line segments */
  rtc_ids = rtcGetBufferData(buf);
  FOR_EACH(i, 0, nsegs) {
    size_t id = i*2/*#ids per segment*/;
    size_t rtc_id = i*4/*#ids per quad*/;

    rtc_ids[rtc_id + 0] = ids[id + 0] * 2/*#rtc vertices per line vertex*/ + 1;
    rtc_ids[rtc_id + 1] = ids[id + 0] * 2/*#rtc vertices per line vertex*/ + 0;
    rtc_ids[rtc_id + 2] = ids[id + 1] * 2/*#rtc vertices per line vertex*/ + 0;
    rtc_ids[rtc_id + 3] = ids[id + 1] * 2/*#rtc vertices per line vertex*/ + 1;
  }

  rtcSetGeometryBuffer(geom->rtc,RTC_BUFFER_TYPE_INDEX, 0/*slot*/,
    RTC_FORMAT_UINT4, buf, 0/*offset*/, sizeof(uint32_t[4])/*stride*/, nsegs);
  rtcUpdateGeometryBuffer(geom->rtc, RTC_BUFFER_TYPE_INDEX, 0/*slot*/);

exit:
  if(buf) rtcReleaseBuffer(buf);
  return res;
error:
  goto exit;
}

static INLINE void
embree_geometry_setup_enable_state
  (struct s2d_scene_view* scnview, struct geometry* geom)
{
  ASSERT(scnview && geom && geom->rtc);
  (void)scnview;
  if(geom->is_enabled) {
    rtcEnableGeometry(geom->rtc);
  } else {
    rtcDisableGeometry(geom->rtc);
  }
}

static INLINE void
embree_geometry_setup_filter_function
  (struct s2d_scene_view* scnview, struct geometry* geom)
{
  ASSERT(scnview && geom && geom->rtc);
  (void)scnview;
  if(!geom->lines->filter.func) {
    rtcSetGeometryIntersectFilterFunction(geom->rtc, NULL);
  } else {
    rtcSetGeometryIntersectFilterFunction(geom->rtc, rtc_hit_filter_wrapper);
  }
}

static INLINE res_T
scene_view_setup_embree(struct s2d_scene_view* scnview)
{
  struct htable_geom_iterator it, end;
  int rtc_outdated = 0;
  res_T res = RES_OK;
  ASSERT(scnview);

  /* The rtc_scn could be already allocated since the scene views are cached */
  if(!scnview->rtc_scn) {
    scnview->rtc_scn = rtcNewScene(scnview->scn->dev->rtc);
    if(!scnview->rtc_scn) {
      res = rtc_error_to_res_T(rtcGetDeviceError(scnview->scn->dev->rtc));
      goto error;
    }
    rtcSetSceneFlags
      (scnview->rtc_scn, RTC_SCENE_FLAG_ROBUST | RTC_SCENE_FLAG_DYNAMIC);
    rtc_outdated = 1;
  }

  htable_geom_begin(&scnview->cached_geoms, &it);
  htable_geom_end(&scnview->cached_geoms, &end);

  while(!htable_geom_iterator_eq(&it, &end)) {
    struct geometry** pgeom = htable_geom_iterator_data_get(&it);
    struct geometry* geom = *pgeom;

    htable_geom_iterator_next(&it);

    /* Define whether or not the embree scene is outdated */
    if(geom->embree_outdated_mask) rtc_outdated = 1;

    /* Register the embree geometry */
    res = embree_geometry_register(scnview, geom);
    if(res != RES_OK) goto error;

    /* Flush the embree geometry states */
    if((geom->embree_outdated_mask & EMBREE_VERTICES) != 0) {
      res = embree_geometry_setup_positions(scnview, geom);
      if(res != RES_OK) goto error;
    }
    if((geom->embree_outdated_mask & EMBREE_INDICES) != 0) {
      res = embree_geometry_setup_indices(scnview, geom);
      if(res != RES_OK) goto error;
    }
    if((geom->embree_outdated_mask & EMBREE_ENABLE) != 0)
      embree_geometry_setup_enable_state(scnview, geom);
    if((geom->embree_outdated_mask & EMBREE_FILTER_FUNCTION) != 0)
      embree_geometry_setup_filter_function(scnview, geom);

    /* Commit the updated geometry */
    if(geom->embree_outdated_mask)
      rtcCommitGeometry(geom->rtc);

    geom->embree_outdated_mask = 0;
  }

  rtc_outdated = rtc_outdated || scnview->rtc_scn_update;

  /* Commit the embree changes */
  if(rtc_outdated) {
    rtcCommitScene(scnview->rtc_scn);
    scnview->rtc_commit = 1; /* Notify that the scene view was committed */
    scnview->rtc_scn_update = 0;
  }

exit:
  return res;
error:
  if(scnview->rtc_scn) {
    rtcReleaseScene(scnview->rtc_scn);
    scnview->rtc_scn = NULL;
  }
  goto exit;
}

static res_T
scene_view_register_line_segments
  (struct s2d_scene_view* scnview,
   struct s2d_shape* shape)
{
  struct geometry** pgeom = NULL;
  struct geometry* geom = NULL;
  size_t iattr;
  unsigned shape_id;
  int is_valid;
  res_T res = RES_OK;
  ASSERT(scnview && shape);

  is_valid = shape->lines->indices && shape->lines->attribs[S2D_POSITION];

  /* Retrieved the cached geometry */
  S2D(shape_get_id(shape, &shape_id));
  pgeom = htable_geom_find(&scnview->cached_geoms, &shape_id);
  if(pgeom) {
    geom = *pgeom;
    if(!is_valid) {
      scene_view_destroy_geometry(scnview, geom);
      htable_geom_erase(&scnview->cached_geoms, &shape_id);
    }
  } else if(is_valid) {
    res = geometry_create(scnview->scn->dev, &geom);
    if(res != RES_OK) goto error;
    res = line_segments_create(scnview->scn->dev, &geom->lines);
    if(res != RES_OK) goto error;
    res = htable_geom_set(&scnview->cached_geoms, &shape_id, &geom);
    if(res != RES_OK) goto error;
    geom->name = shape->id.index;
  }

  if(!is_valid) goto exit;

  /* Get a reference onto the shape lines segments indices */
  if(geom->lines->indices != shape->lines->indices) {
    geom->embree_outdated_mask |= EMBREE_INDICES;
    if(geom->lines->indices) { /* Release previous indices of the geometry */
      index_buffer_ref_put(geom->lines->indices);
      geom->lines->indices = NULL;
    }
    ASSERT(shape->lines->indices);
    index_buffer_ref_get(shape->lines->indices);
    geom->lines->indices = shape->lines->indices;
  }

  /* Get a reference onto the shape line segments attribs */
  FOR_EACH(iattr, 0, S2D_ATTRIBS_COUNT__) {
    if(geom->lines->attribs[iattr] == shape->lines->attribs[iattr])
      continue;

    geom->embree_outdated_mask |= EMBREE_VERTICES;

    if(geom->lines->attribs[iattr]) { /* Release the previous geometry attribs */
      vertex_buffer_ref_put(geom->lines->attribs[iattr]);
      geom->lines->attribs[iattr] = NULL;
    }

    if(!shape->lines->attribs[iattr]) continue;

    /* Get the new buffer */
    vertex_buffer_ref_get(shape->lines->attribs[iattr]);
    geom->lines->attribs[iattr] = shape->lines->attribs[iattr];
    geom->lines->attribs_type[iattr] = shape->lines->attribs_type[iattr];
  }

  /* Update the enable flag */
  if(geom->is_enabled != shape->is_enabled) {
    geom->is_enabled = shape->is_enabled;
    geom->embree_outdated_mask |= EMBREE_ENABLE;
  }

  /* Update the filter function */
  if(geom->lines->filter.func != shape->lines->filter.func
  || geom->lines->filter.data != shape->lines->filter.data) {
    geom->lines->filter = shape->lines->filter;
    geom->embree_outdated_mask |= EMBREE_FILTER_FUNCTION;
  }

  geom->flip_contour = shape->flip_contour;

exit:
  return res;
error:
  goto exit;
}

static res_T
scene_view_compute_nprims_cdf
  (struct s2d_scene_view* scnview, const char store_cdf)
{
  struct htable_geom_iterator it, end;
  size_t len;
  unsigned nprims;
  res_T res = RES_OK;
  ASSERT(scnview);
  ASSERT(darray_nprims_cdf_size_get(&scnview->nprims_cdf) == 0);

  htable_geom_begin(&scnview->cached_geoms, &it);
  htable_geom_end(&scnview->cached_geoms, &end);

  nprims = 0;
  while(!htable_geom_iterator_eq(&it, &end)) {
    const unsigned* shape_id = htable_geom_iterator_key_get(&it);
    struct geometry* geom = *htable_geom_iterator_data_get(&it);
    struct nprims_cdf cdf;

    htable_geom_iterator_next(&it);

    if(!geom->is_enabled) continue;

    geom->scene_prim_id_offset = nprims;
    len = line_segments_get_nsegments(geom->lines);
    nprims += (unsigned)len;

    cdf.nprims = nprims;
    cdf.ishape = *shape_id;
    if(store_cdf && len) {
      res = darray_nprims_cdf_push_back(&scnview->nprims_cdf, &cdf);
      if(res != RES_OK) goto error;
    }
  }

exit:
  return res;
error:
  darray_nprims_cdf_clear(&scnview->nprims_cdf);
  goto exit;
}

static void
scene_view_compute_scene_aabb(struct s2d_scene_view* scnview)
{
  struct htable_geom_iterator it, end;
  float lower[2], upper[2];

  ASSERT(scnview->lower[0] == FLT_MAX && scnview->upper[0] == -FLT_MAX);
  ASSERT(scnview->lower[1] == FLT_MAX && scnview->upper[1] == -FLT_MAX);

  htable_geom_begin(&scnview->cached_geoms, &it);
  htable_geom_end(&scnview->cached_geoms, &end);

  while(!htable_geom_iterator_eq(&it, &end)) {
    struct geometry* geom = *htable_geom_iterator_data_get(&it);

    htable_geom_iterator_next(&it);

    if(!geom->is_enabled) continue;

    line_segments_compute_aabb(geom->lines, lower, upper);
    f2_min(scnview->lower, scnview->lower, lower);
    f2_max(scnview->upper, scnview->upper, upper);
  }
}

static res_T
scene_view_compute_cdf(struct s2d_scene_view* scnview)
{
  struct htable_geom_iterator it, end;
  size_t len;
  float length = 0.f;
  res_T res = RES_OK;
  ASSERT(scnview);
  ASSERT(darray_fltui_size_get(&scnview->cdf) == 0);

  htable_geom_begin(&scnview->cached_geoms, &it);
  htable_geom_end(&scnview->cached_geoms, &end);

  while(!htable_geom_iterator_eq(&it, &end)) {
    const unsigned* shape_id = htable_geom_iterator_key_get(&it);
    struct geometry* geom = *htable_geom_iterator_data_get(&it);
    struct fltui fltui;

    htable_geom_iterator_next(&it);

    if(!geom->is_enabled) continue;

    res = line_segments_compute_cdf(geom->lines);
    if(res != RES_OK) goto error;
    len = darray_float_size_get(&geom->lines->cdf);
    if(len) {
      length += darray_float_cdata_get(&geom->lines->cdf)[len - 1];

      fltui.ui = *shape_id;
      fltui.flt = length;
      res = darray_fltui_push_back(&scnview->cdf, &fltui);
      if(res != RES_OK) goto error;
    }
  }
exit:
  return res;
error:
  darray_fltui_clear(&scnview->cdf);
  goto exit;
}

static res_T
scene_view_sync
  (struct s2d_scene_view* scnview,
   const int mask)
{
  struct htable_shape_iterator it, end;
  res_T res = RES_OK;

  ASSERT(scnview);
  ASSERT((mask & (S2D_TRACE|S2D_SAMPLE|S2D_GET_PRIMITIVE)) != 0);

  /* Commit the scene shape to the scnview */
  htable_shape_begin(&scnview->scn->shapes, &it);
  htable_shape_end(&scnview->scn->shapes, &end);
  while(!htable_shape_iterator_eq(&it, &end)) {
    struct s2d_shape** pshape = htable_shape_iterator_data_get(&it);
    struct s2d_shape* shape = *pshape;

    res = scene_view_register_line_segments(scnview, shape);
    if(res != RES_OK) goto error;
    htable_shape_iterator_next(&it);
  }

  scene_view_compute_scene_aabb(scnview);

  /* Setup the scene for the S2D_TRACE scnview */
  if((mask & S2D_TRACE) != 0) {
    res = scene_view_setup_embree(scnview);
    if(res != RES_OK) goto error;
  }
  /* Setup the scene for the S2D_SAMPLE scnview */
  if((mask & S2D_SAMPLE) != 0) {
    res = scene_view_compute_cdf(scnview);
    if(res != RES_OK) goto error;
  }
  /* Setup the scene for the scene_primitive_id/S3D_GET_PRIMITIVE scnview */
  res = scene_view_compute_nprims_cdf(scnview, (mask & S2D_GET_PRIMITIVE)!=0);
  if(res != RES_OK) goto error;

  scnview->mask = mask;

exit:
  return res;
error:
  goto exit;
}

static res_T
scene_view_create(struct s2d_scene* scn, struct s2d_scene_view** out_scnview)
{
  struct s2d_scene_view* scnview = NULL;
  res_T res = RES_OK;
  ASSERT(scn && out_scnview);

  if(!is_list_empty(&scn->scnviews)) {
    /* Retrieve an already allocated scnview */
    scnview = CONTAINER_OF(list_head(&scn->scnviews), struct s2d_scene_view, node);
    list_del(&scnview->node);
    ref_get(&scnview->ref);
  } else {
    scnview = (struct s2d_scene_view*)MEM_CALLOC
      (scn->dev->allocator, 1, sizeof(struct s2d_scene_view));
    if(!scnview) {
      res = RES_MEM_ERR;
      goto error;
    }
    list_init(&scnview->node);
    htable_geom_init(scn->dev->allocator, &scnview->cached_geoms);
    darray_fltui_init(scn->dev->allocator, &scnview->cdf);
    darray_nprims_cdf_init(scn->dev->allocator, &scnview->nprims_cdf);
    darray_uint_init(scn->dev->allocator, &scnview->detached_shapes);
    f2_splat(scnview->lower, FLT_MAX);
    f2_splat(scnview->upper,-FLT_MAX);
    ref_init(&scnview->ref);

    CLBK_INIT(&scnview->on_shape_detach_cb);
    CLBK_SETUP(&scnview->on_shape_detach_cb, on_shape_detach, scnview);
    SIG_CONNECT_CLBK(&scn->sig_shape_detach, &scnview->on_shape_detach_cb);
  }
  S2D(scene_ref_get(scn));
  scnview->scn = scn;
exit:
  *out_scnview = scnview;
  return res;
error:
  if(scnview) {
    S2D(scene_view_ref_put(scnview));
    scnview = NULL;
  }
  goto exit;
}

static void
scene_view_release(ref_T* ref)
{
  struct s2d_scene_view* scnview = CONTAINER_OF(ref, struct s2d_scene_view, ref);
  size_t i, n;
  ASSERT(ref);

  /* Remove the geometry of the shapes detached while the scnview was active */
  n = darray_uint_size_get(&scnview->detached_shapes);
  FOR_EACH(i, 0, n) {
    const unsigned shape_id = darray_uint_cdata_get(&scnview->detached_shapes)[i];
    struct geometry** pgeom = htable_geom_find(&scnview->cached_geoms, &shape_id);
    struct geometry* geom = *pgeom;
    size_t tmp; (void)tmp;
    scene_view_destroy_geometry(scnview, geom);
    tmp = htable_geom_erase(&scnview->cached_geoms, &shape_id);
    ASSERT(tmp == 1);
  }
  darray_uint_clear(&scnview->detached_shapes);

  /* Clear the scnview data structures excepted the cache of geometries that
   * will be used to speed up the future scnview creation */
  darray_fltui_clear(&scnview->cdf);
  darray_nprims_cdf_clear(&scnview->nprims_cdf);
  f2_splat(scnview->lower, FLT_MAX);
  f2_splat(scnview->upper,-FLT_MAX);
  scnview->mask = 0;
  scnview->rtc_commit = 0;

  /* Do not physically release the memory space of the scnview. Add it to the
   * available scnviews pool of the scene */
  list_add(&scnview->scn->scnviews, &scnview->node);
  S2D(scene_ref_put(scnview->scn));
}

static res_T
scene_view_trace_ray
  (struct s2d_scene_view* scnview,
   const float org[2],
   const float dir[],
   const float range[2],
   const int rt_3d,
   void* ray_data,
   struct s2d_hit* hit)
{
  struct RTCRayHit ray_hit;
  struct RTCIntersectArguments intersect_args;
  struct intersect_context intersect_ctx;
  float dot = 1;
  float dir2d[3] = {0.f, 0.f, 0.f};
  float range2d[2] = {FLT_MAX, -FLT_MAX};
  size_t i;

  if(!scnview || !org || !dir || !range || !hit)
    return RES_BAD_ARG;
  if(!rt_3d) {
    if(!f2_is_normalized(dir)) {
      log_error(scnview->scn->dev,
        "%s: unnormalized ray direction {%g, %g}.\n", FUNC_NAME, SPLIT2(dir));
      return RES_BAD_ARG;
    }
  } else {
    if(!f3_is_normalized(dir)) {
      log_error(scnview->scn->dev,
        "%s: unnormalized ray direction {%g, %g, %g}.\n", FUNC_NAME, SPLIT3(dir));
      return RES_BAD_ARG;
    }
    if(eq_epsf(dir[0], 0.f, 1.e-6f) && eq_epsf(dir[1], 0.f, 1.e-6f)) {
      *hit = S2D_HIT_NULL;
      return RES_OK;
    }
  }
  if(range[0] < 0) {
    log_error(scnview->scn->dev,
      "%s: invalid ray range [%g, %g] - it must be in [0, INF).\n",
      FUNC_NAME, range[0], range[1]);
    return RES_BAD_ARG;
  }
  if((scnview->mask & S2D_TRACE) == 0) {
    log_error(scnview->scn->dev,
      "%s: the S2D_TRACE flag is not active onto the submitted scene view.\n",
      FUNC_NAME);
    return RES_BAD_OP;
  }
  if(range[0] > range[1]) { /* Degenerated range <=> disabled ray */
    *hit = S2D_HIT_NULL;
    return RES_OK;
  }

  if(!rt_3d) {
    f2_set(dir2d, dir);
    f2_set(range2d, range);
  } else {
    f2_normalize(dir2d, dir);
    dot = f3_dot(dir2d, dir); /* Cosine between dir and dir_2d */
    range2d[0] = dot*range[0];
    range2d[1] = dot*range[1];
  }

  /* Initialise the ray */
  ray_hit.ray.org_x = org[0];
  ray_hit.ray.org_y = org[1];
  ray_hit.ray.org_z = 0.f;
  ray_hit.ray.dir_x = dir2d[0];
  ray_hit.ray.dir_y = dir2d[1];
  ray_hit.ray.dir_z = 0.f;
  ray_hit.ray.tnear = range2d[0];
  ray_hit.ray.tfar = range2d[1];
  ray_hit.ray.time = FLT_MAX; /* Invalid fields */
  ray_hit.ray.mask = UINT_MAX; /* Invalid fields */
  ray_hit.ray.id = UINT_MAX; /* Invalid fields */
  ray_hit.ray.flags = UINT_MAX; /* Invalid fields */

  /* Initialise the hit */
  ray_hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
  FOR_EACH(i, 0, RTC_MAX_INSTANCE_LEVEL_COUNT) {
    ray_hit.hit.instID[i] = RTC_INVALID_GEOMETRY_ID;
  }

  /* Initialise the intersect context */
  rtcInitIntersectArguments(&intersect_args);
  intersect_args.context = &intersect_ctx.rtc;
  rtcInitRayQueryContext(&intersect_ctx.rtc);
  intersect_ctx.ws_org[0] = org[0];
  intersect_ctx.ws_org[1] = org[1];
  intersect_ctx.ws_dir[0] = dir[0];
  intersect_ctx.ws_dir[1] = dir[1];
  intersect_ctx.ws_dir[2] = rt_3d ? dir[2] : 0.f;
  intersect_ctx.ws_range[0] = range[0];
  intersect_ctx.ws_range[1] = range[1];
  intersect_ctx.scnview = scnview;
  intersect_ctx.data = ray_data;
  intersect_ctx.rt_3d = rt_3d;
  intersect_ctx.cos_dir_dir2d = dot;

  /* Here we go */
  rtcIntersect1(scnview->rtc_scn, &ray_hit, &intersect_args);

  hit_setup(scnview, &ray_hit, hit);

  if(rt_3d && !S2D_HIT_NONE(hit)) hit->distance /= dot;
  return RES_OK;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
s2d_scene_view_create
  (struct s2d_scene* scn,
   const int mask,
   struct s2d_scene_view** out_scnview)
{
  struct s2d_scene_view* scnview = NULL;
  res_T res = RES_OK;

  if(!scn || !out_scnview) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(!(mask & S2D_TRACE)
  && !(mask & S2D_SAMPLE)
  && !(mask & S2D_GET_PRIMITIVE)) {
    log_error(scn->dev, "%s: no valid scene view mask is defined.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  res = scene_view_create(scn, &scnview);
  if(res != RES_OK) goto error;

  res = scene_view_sync(scnview, mask);
  if(res != RES_OK) goto error;

exit:
  if(out_scnview) *out_scnview = scnview;
  return res;
error:
  if(scnview) {
    S2D(scene_view_ref_put(scnview));
    scnview = NULL;
  }
  goto exit;
}

res_T
s2d_scene_view_ref_get(struct s2d_scene_view* scnview)
{
  if(!scnview) return RES_BAD_ARG;
  ref_get(&scnview->ref);
  return RES_OK;
}

res_T
s2d_scene_view_ref_put(struct s2d_scene_view* scnview)
{
  if(!scnview) return RES_BAD_ARG;
  ref_put(&scnview->ref, scene_view_release);
  return RES_OK;
}

res_T
s2d_scene_view_get_mask(struct s2d_scene_view* scnview, int* mask)
{
  if(!scnview || !mask) return RES_BAD_ARG;
  *mask = scnview->mask;
  return RES_OK;
}

res_T
s2d_scene_view_trace_ray
  (struct s2d_scene_view* scnview,
   const float org[2],
   const float dir[2],
   const float range[2],
   void* ray_data,
   struct s2d_hit* hit)
{
  return scene_view_trace_ray(scnview, org, dir, range, 0/*2D*/, ray_data, hit);
}

res_T
s2d_scene_view_trace_ray_3d
  (struct s2d_scene_view* scnview,
   const float org[2],
   const float dir[3],
   const float range[2],
   void* ray_data,
   struct s2d_hit* hit)
{
  return scene_view_trace_ray(scnview, org, dir, range, 1/*3D*/, ray_data, hit);
}

res_T
s2d_scene_view_sample
  (struct s2d_scene_view* scnview,
   const float u, const float v,
   struct s2d_primitive* primitive,
   float* s)
{
  struct geometry** pgeom;
  struct geometry* geom;
  size_t sz;
  size_t i;
  const struct fltui* fltui, *fltui_found;
  const float* flt, *flt_found;
  unsigned ishape;
  float f;
  res_T res = RES_OK;

  if(!scnview || !primitive || !s) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(u < 0.f || u >= 1.f || v < 0.f || v >= 1.f) {
    log_error(scnview->scn->dev,
      "%s: The submitted numbers are not canonical, i.e. they ar not in [0, 1[.\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }
  if((scnview->mask & S2D_SAMPLE) == 0) {
    log_error(scnview->scn->dev,
      "%s: the S2D_SAMPLE flag is not active onto the submitted scene view.\n",
      FUNC_NAME);
    res = RES_BAD_OP;
    goto error;
  }

  /* Find the sampled geometry */
  if(darray_fltui_size_get(&scnview->cdf) == 0) { /* No geometry to sample */
    *primitive = S2D_PRIMITIVE_NULL;
    goto exit;
  } else if(darray_fltui_size_get(&scnview->cdf) == 1) {
    ishape = darray_fltui_cdata_get(&scnview->cdf)[0].ui;
    /* Map u to the CDF bounds */
    f = u * darray_fltui_cdata_get(&scnview->cdf)[0].flt;
  } else {
    fltui = darray_fltui_cdata_get(&scnview->cdf);
    sz = darray_fltui_size_get(&scnview->cdf);
    f = u * fltui[sz-1].flt; /* Map u to [0, ScenePerimeter[ */
    fltui_found = search_lower_bound
      (&f, fltui, sz, sizeof(*fltui), cmp_float_to_fltui);
    ASSERT(fltui_found);

    /* search_lower_bound returns the first entry that is not less than `f'.
     * The following code discards entries that are also `equal' to `f' */
    i = (size_t)(fltui_found - fltui);
    while(fltui[i].flt == f && i < sz) ++i;
    ASSERT(i < sz);

    fltui_found = fltui + i;
    ishape = fltui_found->ui;

    /* Map f to [0, ShapePerimeter[ */
    if(i) f -= fltui[i-1].flt;
  }
  pgeom = htable_geom_find(&scnview->cached_geoms, &ishape);
  ASSERT(pgeom);
  geom = *pgeom;

  /* Find the sampled segment */
  flt = darray_float_cdata_get(&geom->lines->cdf);
  sz = darray_float_size_get(&geom->lines->cdf);
  flt_found = search_lower_bound(&f, flt, sz, sizeof(*flt), cmp_float);
  ASSERT(flt_found);

  /* search_lower_bound returns the first entry that is not less than `f'.
   * The following code discards entries that are also equal to `f' */
  i = (size_t)(flt_found - flt);
  while(flt[i] == f && i < sz) ++i;
  ASSERT(i < sz);

  primitive->mesh__ = geom;
  primitive->geom_id = geom->name;
  primitive->prim_id = (unsigned)i;
  primitive->scene_prim_id = primitive->prim_id + geom->scene_prim_id_offset;
  S2D(primitive_sample(primitive, v, s));

exit:
  return res;
error:
  goto exit;
}

res_T
s2d_scene_view_get_primitive
  (struct s2d_scene_view* scnview,
   const unsigned iprim,
   struct s2d_primitive* prim)
{
  struct geometry** pgeom;
  struct geometry* geom;
  const struct nprims_cdf* begin, *found;
  size_t nprims;
  size_t sz;
  size_t i;
  unsigned ishape;
  res_T res = RES_OK;

  if(!scnview || !prim) {
    res = RES_BAD_ARG;
    goto error;
  }
  if((scnview->mask & S2D_GET_PRIMITIVE) == 0) {
    log_error(scnview->scn->dev,
      "%s: the S2D_GET_PRIMITIVE flag is not active onto the submitted scene view.\n",
      FUNC_NAME);
    res = RES_BAD_OP;
    goto error;
  }
  S2D(scene_view_primitives_count(scnview, &nprims));
  if(iprim >= nprims) {
    log_error(scnview->scn->dev,
      "%s: The primitive index %u exceeds the number of primitives %u.\n",
      FUNC_NAME, iprim, (unsigned)nprims);
    res = RES_BAD_ARG;
    goto error;
  }

  i = iprim;
  if(darray_nprims_cdf_size_get(&scnview->nprims_cdf) == 1) {
    ishape = darray_nprims_cdf_cdata_get(&scnview->nprims_cdf)[0].ishape;
  } else {
    begin = darray_nprims_cdf_cdata_get(&scnview->nprims_cdf);
    sz = darray_nprims_cdf_size_get(&scnview->nprims_cdf);
    found = search_lower_bound
      (&i, begin, sz, sizeof(*begin), cmp_size_t_to_nprims_cdf);
    ASSERT(found);
    ishape = found->ishape;
    if(found != begin) {
      ASSERT(i >= found[-1].nprims);
      i -= found[-1].nprims;
    }
  }
  pgeom = htable_geom_find(&scnview->cached_geoms, &ishape);
  ASSERT(pgeom);
  geom = *pgeom;
  ASSERT(i < line_segments_get_nsegments(geom->lines));
  prim->mesh__ = geom;
  prim->geom_id = geom->name;
  prim->prim_id = (unsigned)i;
  prim->scene_prim_id = geom->scene_prim_id_offset + prim->prim_id;

exit:
  return res;
error:
  goto exit;
}

res_T
s2d_scene_view_primitives_count
  (struct s2d_scene_view* scnview, size_t* prims_count)
{
  size_t nprims = 0;
  res_T res = RES_OK;

  if(!scnview || !prims_count) {
    res = RES_BAD_ARG;
    goto error;
  }
  if((scnview->mask & S2D_GET_PRIMITIVE) != 0) {
    const size_t len = darray_nprims_cdf_size_get(&scnview->nprims_cdf);
    if(!len) {
      nprims = 0;
    } else {
      nprims = darray_nprims_cdf_cdata_get(&scnview->nprims_cdf)[len-1].nprims;
    }
  } else {
    struct htable_geom_iterator it, end;

    htable_geom_begin(&scnview->cached_geoms, &it);
    htable_geom_end(&scnview->cached_geoms, &end);
    nprims = 0;
    while(!htable_geom_iterator_eq(&it, &end)) {
      struct geometry** pgeom = htable_geom_iterator_data_get(&it);
      struct geometry* geom = *pgeom;
      htable_geom_iterator_next(&it);

      if(geom->is_enabled) {
        nprims += line_segments_get_nsegments(geom->lines);
      }
    }
  }

exit:
  if(prims_count) *prims_count = nprims;
  return res;
error:
  goto exit;
}

res_T
s2d_scene_view_compute_contour_length
  (struct s2d_scene_view* scnview, float* out_length)
{
  float length = 0;
  res_T res = RES_OK;

  if(!scnview || !out_length) {
    res = RES_BAD_ARG;
    goto error;
  }

  if((scnview->mask & S2D_SAMPLE) != 0) {
    /* Retrieve the overall scene contour length from the scene cumulative
     * distribution function. Note that the CDF stores the cumulative contour
     * length. The real contour length is thus the CDF upper bound */
    size_t len = darray_fltui_size_get(&scnview->cdf);
    if(!len) {
      length = 0.f;
    } else {
      length = darray_fltui_cdata_get(&scnview->cdf)[len-1].flt;
    }
  } else {
    struct htable_geom_iterator it, end;

    htable_geom_begin(&scnview->cached_geoms, &it);
    htable_geom_end(&scnview->cached_geoms, &end);
    length = 0.f;
    while(!htable_geom_iterator_eq(&it, &end)) {
      struct geometry** pgeom = htable_geom_iterator_data_get(&it);
      struct geometry* geom = *pgeom;
      htable_geom_iterator_next(&it);

      if(geom->is_enabled) {
        length += line_segments_compute_length(geom->lines);
      }
    }
  }

exit:
  if(out_length) *out_length = length;
  return res;
error:
  length = -1.f;
  goto exit;
}

res_T
s2d_scene_view_compute_area(struct s2d_scene_view* scnview, float* out_area)
{
  struct htable_geom_iterator it, end;
  float area = 0.f;
  res_T res = RES_OK;

  if(!scnview || !out_area) {
    res = RES_BAD_ARG;
    goto error;
  }

  htable_geom_begin(&scnview->cached_geoms, &it);
  htable_geom_end(&scnview->cached_geoms, &end);
  area = 0.f;
  while(!htable_geom_iterator_eq(&it, &end)) {
    struct geometry** pgeom = htable_geom_iterator_data_get(&it);
    struct geometry* geom = *pgeom;
    htable_geom_iterator_next(&it);

    if(geom->is_enabled) {
      area += line_segments_compute_area(geom->lines, geom->flip_contour);
    }
  }

  if(area < 0.f) {
    log_warning(scnview->scn->dev,
"%s:\n"
"\tthe area is negative. The scene shapes might not represent closed\n"
"\tpolygons, or the edge normals might not point inward the polygon.\n",
      FUNC_NAME);
  }

exit:
  if(out_area) *out_area = area;
  return res;
error:
  area = -1.f;
  goto exit;
}

res_T
s2d_scene_view_get_aabb
  (struct s2d_scene_view* scnview, float lower[2], float upper[2])
{
  if(!scnview || !lower || !upper) return RES_BAD_ARG;
  f2_set(lower, scnview->lower);
  f2_set(upper, scnview->upper);
  return RES_OK;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
void
scene_view_destroy(struct s2d_scene_view* scnview)
{
  struct htable_geom_iterator it, end;
  ASSERT(scnview && !is_list_empty(&scnview->node)/*Not in use*/);
  ASSERT(scnview->mask == 0);

  /* Delete the cached geometries */
  htable_geom_begin(&scnview->cached_geoms, &it);
  htable_geom_end(&scnview->cached_geoms, &end);
  while(!htable_geom_iterator_eq(&it, &end)) {
    struct geometry** pgeom = htable_geom_iterator_data_get(&it);
    struct geometry* geom = *pgeom;
    scene_view_destroy_geometry(scnview, geom);
    htable_geom_iterator_next(&it);
  }

  /* Delete the back-end scene */
  if(scnview->rtc_scn) rtcReleaseScene(scnview->rtc_scn);

  /* Release internal data structure */
  htable_geom_release(&scnview->cached_geoms);
  darray_fltui_release(&scnview->cdf);
  darray_nprims_cdf_release(&scnview->nprims_cdf);
  darray_uint_release(&scnview->detached_shapes);

  /* Remove the scnview from its pool */
  list_del(&scnview->node);
  CLBK_DISCONNECT(&scnview->on_shape_detach_cb);

  /* Free the scnview memory space */
  MEM_RM(scnview->scn->dev->allocator, scnview);
}

void
rtc_hit_filter_wrapper(const struct RTCFilterFunctionNArguments* args)
{
  struct s2d_hit hit;
  struct RTCRayHit ray_hit;
  struct intersect_context* ctx;
  struct geometry* geom;
  struct hit_filter* filter;
  int is_hit_filtered = 0;
  ASSERT(args && args->N == 1 && args->context && args->valid[0] != 0);

  rtc_rayN_get_ray(args->ray, args->N, 0, &ray_hit.ray);
  rtc_hitN_get_hit(args->hit, args->N, 0, &ray_hit.hit);

  ctx = CONTAINER_OF(args->context, struct intersect_context, rtc);

  geom = args->geometryUserPtr;
  filter = &geom->lines->filter;
  ASSERT(filter->func);

  hit_setup(ctx->scnview, &ray_hit, &hit);
  if(ctx->rt_3d) {
    hit.distance /= ctx->cos_dir_dir2d;
  }
  is_hit_filtered = filter->func
    (&hit, ctx->ws_org, ctx->ws_dir, ctx->ws_range, ctx->data, filter->data);
  if(is_hit_filtered) {
    args->valid[0] = 0;
  }
}

