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

#ifndef SDIS_SCENE_XD_H
#define SDIS_SCENE_XD_H

#include "sdis_interface_c.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_scene_c.h"

#include <star/ssp.h>
#include <star/senc2d.h>
#include <star/senc3d.h>

#include <rsys/cstr.h>
#include <rsys/float22.h>
#include <rsys/float33.h>
#include <rsys/rsys.h>

/* Emperical cos threshold defining if an angle is sharp */
#define SHARP_ANGLE_COS_THRESOLD -0.70710678 /* ~ cos(3*PI/4) */

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

static INLINE int
check_sdis_scene_create_args(const struct sdis_scene_create_args* args)
{
  return args
      && args->get_indices
      && args->get_interface
      && args->get_position
      && args->nprimitives
      && args->nprimitives < UINT_MAX
      && args->nvertices
      && args->nvertices < UINT_MAX
      && args->fp_to_meter > 0;
}

static INLINE res_T
check_sdis_scene_find_closest_point_args
  (const struct sdis_scene_find_closest_point_args* args)
{
  /* Undefined input arguments */
  if(!args) return RES_BAD_ARG;

  /* Invalid radius */
  if(args->radius <= 0) return RES_BAD_ARG;

  return RES_OK;
}

#endif /* SDIS_SCENE_XD_H */

#include "sdis_device_c.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/mem_allocator.h>

#include <limits.h>

/* Check the submitted dimension and include its specific headers */
#define SENCXD_DIM SDIS_XD_DIMENSION
#if (SDIS_XD_DIMENSION == 2)
#include <star/sencX2d.h>
#include <star/s2d.h>
#elif (SDIS_XD_DIMENSION == 3)
#include <star/sencX3d.h>
#include <star/s3d.h>
#else
  #error "Invalid SDIS_XD_DIMENSION value."
#endif

#include "sdis_Xd_begin.h"

#if DIM == 2
  #define HIT_ON_BOUNDARY hit_on_vertex
#else
  #define HIT_ON_BOUNDARY hit_on_edge
#endif

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
#if DIM == 2
#define ON_VERTEX_EPSILON 1.e-4f
/* Check that `hit' roughly lies on a vertex. */
static INLINE int
hit_on_vertex
  (const struct s2d_hit* hit,
   const float org[2],
   const float dir[2])
{
  struct s2d_attrib v0, v1;
  float E[2];
  float hit_pos[2];
  float segment_len;
  float hit_len0;
  float hit_len1;
  ASSERT(hit && !S2D_HIT_NONE(hit) && org && dir);

  /* Rertieve the segment vertices */
  S2D(segment_get_vertex_attrib(&hit->prim, 0, S2D_POSITION, &v0));
  S2D(segment_get_vertex_attrib(&hit->prim, 1, S2D_POSITION, &v1));

  /* Compute the length of the segment */
  segment_len = f2_len(f2_sub(E, v1.value, v0.value));

  /* Compute the hit position onto the segment */
  f2_add(hit_pos, org, f2_mulf(hit_pos, dir, hit->distance));

  /* Compute the length from hit position to segment vertices */
  hit_len0 = f2_len(f2_sub(E, v0.value, hit_pos));
  hit_len1 = f2_len(f2_sub(E, v1.value, hit_pos));

  if(hit_len0 / segment_len < ON_VERTEX_EPSILON
  || hit_len1 / segment_len < ON_VERTEX_EPSILON)
    return 1;
  return 0;
}

static int
hit_shared_vertex
  (const struct s2d_primitive* seg0,
   const struct s2d_primitive* seg1,
   const float pos0[2], /* Tested position onto the segment 0 */
   const float pos1[2]) /* Tested Position onto the segment 1 */
{
  struct s2d_attrib seg0_vertices[2]; /* Vertex positions of the segment 0 */
  struct s2d_attrib seg1_vertices[2]; /* Vertex positions of the segment 1 */
  float d0[2], d1[2]; /* temporary vector */
  float seg0_len, seg1_len; /* Length of the segments */
  float tmp0_len, tmp1_len;
  float cos_normals;
  int seg0_vert = -1; /* Id of the shared vertex for the segment 0 */
  int seg1_vert = -1; /* Id of the shared vertex for the segment 1 */
  int seg0_ivertex, seg1_ivertex;
  ASSERT(seg0 && seg1 && pos0 && pos1);

  /* Fetch the vertices of the segment 0 */
  S2D(segment_get_vertex_attrib(seg0, 0, S2D_POSITION, &seg0_vertices[0]));
  S2D(segment_get_vertex_attrib(seg0, 1, S2D_POSITION, &seg0_vertices[1]));

  /* Fetch the vertices of the segment 1 */
  S2D(segment_get_vertex_attrib(seg1, 0, S2D_POSITION, &seg1_vertices[0]));
  S2D(segment_get_vertex_attrib(seg1, 1, S2D_POSITION, &seg1_vertices[1]));

  /* Look for the vertex shared by the 2 segments */
  for(seg0_ivertex = 0; seg0_ivertex < 2 && seg0_vert < 0; ++seg0_ivertex) {
  for(seg1_ivertex = 0; seg1_ivertex < 2 && seg1_vert < 0; ++seg1_ivertex) {
    const int vertex_eq = f2_eq_eps
      (seg0_vertices[seg0_ivertex].value,
       seg1_vertices[seg1_ivertex].value,
       1.e-6f);
    if(vertex_eq) {
      seg0_vert = seg0_ivertex;
      seg1_vert = seg1_ivertex;
      /* We assume that the segments are not degenerated. As a consequence we
       * can break here since a vertex of the segment 0 can be equal to at most
       * one vertex of the segment 1 */
      break;
    }
  }}

  /* The segments do not have a common vertex */
  if(seg0_vert < 0) return 0;

  /* Compute the dirctions from shared vertex to the opposite segment vertex */
  f2_sub(d0, seg0_vertices[(seg0_vert+1)%2].value, seg0_vertices[seg0_vert].value);
  f2_sub(d1, seg1_vertices[(seg1_vert+1)%2].value, seg1_vertices[seg1_vert].value);

  /* Compute the cosine between the segments */
  seg0_len = f2_normalize(d0, d0);
  seg1_len = f2_normalize(d1, d1);
  cos_normals = f2_dot(d0, d1);

  /* The angle formed by the 2 segments is sharp. Do not filter the hit */
  if(cos_normals > SHARP_ANGLE_COS_THRESOLD) return 0;

  /* Compute the length from pos<0|1> to shared vertex */
  f2_sub(d0, seg0_vertices[seg0_vert].value, pos0);
  f2_sub(d1, seg1_vertices[seg1_vert].value, pos1);
  tmp0_len = f2_len(d0);
  tmp1_len = f2_len(d1);

  return (eq_epsf(seg0_len, 0, 1.e-6f) || tmp0_len/seg0_len < ON_VERTEX_EPSILON)
      && (eq_epsf(seg1_len, 0, 1.e-6f) || tmp1_len/seg1_len < ON_VERTEX_EPSILON);
}

#else  /* DIM == 3 */
#define ON_EDGE_EPSILON 1.e-4f
/* Check that `hit' roughly lies on an edge. */
static INLINE int
hit_on_edge
  (const struct s3d_hit* hit,
   const float org[3],
   const float dir[3])
{
  struct s3d_attrib v0, v1, v2;
  float E0[3], E1[3], N[3];
  float tri_2area;
  float hit_2area0;
  float hit_2area1;
  float hit_2area2;
  float hit_pos[3];
  ASSERT(hit && !S3D_HIT_NONE(hit) && org && dir);

  /* Retrieve the triangle vertices */
  S3D(triangle_get_vertex_attrib(&hit->prim, 0, S3D_POSITION, &v0));
  S3D(triangle_get_vertex_attrib(&hit->prim, 1, S3D_POSITION, &v1));
  S3D(triangle_get_vertex_attrib(&hit->prim, 2, S3D_POSITION, &v2));

  /* Compute the triangle area * 2 */
  f3_sub(E0, v1.value, v0.value);
  f3_sub(E1, v2.value, v0.value);
  tri_2area = f3_len(f3_cross(N, E0, E1));

  /* Compute the hit position */
  f3_add(hit_pos, org, f3_mulf(hit_pos, dir, hit->distance));

  /* Compute areas */
  f3_sub(E0, v0.value, hit_pos);
  f3_sub(E1, v1.value, hit_pos);
  hit_2area0 = f3_len(f3_cross(N, E0, E1));
  f3_sub(E0, v1.value, hit_pos);
  f3_sub(E1, v2.value, hit_pos);
  hit_2area1 = f3_len(f3_cross(N, E0, E1));
  f3_sub(E0, v2.value, hit_pos);
  f3_sub(E1, v0.value, hit_pos);
  hit_2area2 = f3_len(f3_cross(N, E0, E1));

  if(hit_2area0 / tri_2area < ON_EDGE_EPSILON
  || hit_2area1 / tri_2area < ON_EDGE_EPSILON
  || hit_2area2 / tri_2area < ON_EDGE_EPSILON)
    return 1;

  return 0;
}

static int
hit_shared_edge
  (const struct s3d_primitive* tri0,
   const struct s3d_primitive* tri1,
   const float uv0[2], /* Barycentric coordinates of tested position on tri0 */
   const float uv1[2], /* Barycentric coordinates of tested position on tri1 */
   const float pos0[3], /* Tested position onto the triangle 0 */
   const float pos1[3]) /* Tested Position onto the triangle 1 */
{
  struct s3d_attrib tri0_vertices[3]; /* Vertex positions of the triangle 0 */
  struct s3d_attrib tri1_vertices[3]; /* Vertex positions of the triangle 1 */
  int tri0_edge[2] = {-1, -1}; /* Shared edge vertex ids for the triangle 0 */
  int tri1_edge[2] = {-1, -1}; /* Shared edge vertex ids for the triangle 1 */
  int edge_ivertex = 0; /* Temporary variable */
  int tri0_ivertex, tri1_ivertex;
  ASSERT(tri0 && tri1 && pos0 && pos1);

  /* Fetch the vertices of the triangle 0 */
  S3D(triangle_get_vertex_attrib(tri0, 0, S3D_POSITION, &tri0_vertices[0]));
  S3D(triangle_get_vertex_attrib(tri0, 1, S3D_POSITION, &tri0_vertices[1]));
  S3D(triangle_get_vertex_attrib(tri0, 2, S3D_POSITION, &tri0_vertices[2]));

  /* Fetch the vertices of the triangle 1 */
  S3D(triangle_get_vertex_attrib(tri1, 0, S3D_POSITION, &tri1_vertices[0]));
  S3D(triangle_get_vertex_attrib(tri1, 1, S3D_POSITION, &tri1_vertices[1]));
  S3D(triangle_get_vertex_attrib(tri1, 2, S3D_POSITION, &tri1_vertices[2]));

  /* Look for the vertices shared by the 2 triangles */
  for(tri0_ivertex=0; tri0_ivertex < 3 && edge_ivertex < 2; ++tri0_ivertex) {
  for(tri1_ivertex=0; tri1_ivertex < 3 && edge_ivertex < 2; ++tri1_ivertex) {
    const int vertex_eq = f3_eq_eps
      (tri0_vertices[tri0_ivertex].value,
       tri1_vertices[tri1_ivertex].value,
       1.e-6f);
    if(vertex_eq) {
      tri0_edge[edge_ivertex] = tri0_ivertex;
      tri1_edge[edge_ivertex] = tri1_ivertex;
      ++edge_ivertex;
      /* We assume that the triangles are not degenerated. As a consequence we
       * can break here since a vertex of the triangle 0 can be equal to at
       * most one vertex of the triangle 1 */
      break;
    }
  }}

  /* The triangles do not have a common edge */
  if(edge_ivertex == 0) {
    return 0;

  /* The triangles have a common vertex */
  } else if(edge_ivertex == 1) {
    float bcoord0, bcoord1;
    int hit_vertex;

    /* Retrieve the barycentric coordinate of the position on triangle 0
     * corresponding to the vertex shared between the 2 triangles. */
    switch(tri0_edge[0]) {
      case 0: bcoord0 = uv0[0]; break;
      case 1: bcoord0 = uv0[1]; break;
      case 2: bcoord0 = CLAMP(1.f - uv0[0] - uv0[1], 0.f, 1.f); break;
      default: FATAL("Unreachable code\n"); break;
    }

    /* Retrieve the barycentric coordinate of the position on triangle 1
     * corresponding to the vertex shared between the 2 triangles. */
    switch(tri1_edge[0]) {
      case 0: bcoord1 = uv1[0]; break;
      case 1: bcoord1 = uv1[1]; break;
      case 2: bcoord1 = CLAMP(1.f - uv0[0] - uv0[1], 0.f, 1.f); break;
      default: FATAL("Unreachable code\n"); break;
    }

    /* Check that the both positions lie on the shared vertex */
    hit_vertex = eq_epsf(1.f, bcoord0, ON_VERTEX_EPSILON)
            && eq_epsf(1.f, bcoord1, ON_VERTEX_EPSILON);
    return hit_vertex;

  /* The triangles have a common edge */
  } else {
    float E0[3], E1[3]; /* Temporary variables storing triangle edges */
    float N0[3], N1[3]; /* Temporary Normals */
    float tri0_2area, tri1_2area; /* 2*area of the submitted triangles */
    float tmp0_2area, tmp1_2area;
    float cos_normals;
    int iv0, iv1, iv2;
    int hit_edge;

    /* Ensure that the vertices of the shared edge are registered in the right
     * order regarding the triangle vertices, i.e. (0,1), (1,2) or (2,0) */
    if((tri0_edge[0]+1)%3 != tri0_edge[1]) SWAP(int, tri0_edge[0], tri0_edge[1]);
    if((tri1_edge[0]+1)%3 != tri1_edge[1]) SWAP(int, tri1_edge[0], tri1_edge[1]);

    /* Compute the shared edge normal lying in the triangle 0 plane */
    iv0 =  tri0_edge[0];
    iv1 =  tri0_edge[1];
    iv2 = (tri0_edge[1]+1) % 3;
    f3_sub(E0, tri0_vertices[iv1].value, tri0_vertices[iv0].value);
    f3_sub(E1, tri0_vertices[iv2].value, tri0_vertices[iv0].value);
    f3_cross(N0, E0, E1); /* Triangle 0 normal */
    tri0_2area = f3_len(N0);
    f3_cross(N0, N0, E0);

    /* Compute the shared edge normal lying in the triangle 1 plane */
    iv0 =  tri1_edge[0];
    iv1 =  tri1_edge[1];
    iv2 = (tri1_edge[1]+1) % 3;
    f3_sub(E0, tri1_vertices[iv1].value, tri1_vertices[iv0].value);
    f3_sub(E1, tri1_vertices[iv2].value, tri1_vertices[iv0].value);
    f3_cross(N1, E0, E1);
    tri1_2area = f3_len(N1);
    f3_cross(N1, N1, E0);

    /* Compute the cosine between the 2 edge normals */
    f3_normalize(N0, N0);
    f3_normalize(N1, N1);
    cos_normals = f3_dot(N0, N1);

    /* The angle formed by the 2 triangles is sharp */
    if(cos_normals > SHARP_ANGLE_COS_THRESOLD) return 0;

    /* Compute the 2 times the area of the (pos0, shared_edge.vertex0,
     * shared_edge.vertex1) triangles */
    f3_sub(E0, tri0_vertices[tri0_edge[0]].value, pos0);
    f3_sub(E1, tri0_vertices[tri0_edge[1]].value, pos0);
    tmp0_2area = f3_len(f3_cross(N0, E0, E1));

    /* Compute the 2 times the area of the (pos1, shared_edge.vertex0,
     * shared_edge.vertex1) triangles */
    f3_sub(E0, tri1_vertices[tri1_edge[0]].value, pos1);
    f3_sub(E1, tri1_vertices[tri1_edge[1]].value, pos1);
    tmp1_2area = f3_len(f3_cross(N1, E0, E1));

    hit_edge =
    (  eq_epsf(tri0_2area, 0, 1.e-6f)
    || eq_epsf(tmp0_2area, 0, 1.e-6f)
    || tmp0_2area/tri0_2area < ON_EDGE_EPSILON);
    hit_edge = hit_edge &&
    (  eq_epsf(tri1_2area, 0, 1.e-6f)
    || eq_epsf(tmp1_2area, 0, 1.e-6f)
    || tmp1_2area/tri1_2area < ON_EDGE_EPSILON);
    return hit_edge;
  }
}
#undef ON_EDGE_EPSILON
#endif /* DIM == 2 */

/* Avoid self-intersection for a ray starting from a planar primitive, i.e. a
 * triangle or a line segment */
static int
XD(hit_filter_function)
  (const struct sXd(hit)* hit,
   const float org[DIM],
   const float dir[DIM],
   const float range[2],
   void* query_data,
   void* global_data)
{
  const struct hit_filter_data* filter_data = query_data;
  const struct sXd(hit)* hit_from = NULL;
  (void)org, (void)dir, (void)global_data, (void)range;

  /* No user defined data. Do not filter */
  if(!filter_data) return 0;

  /* Call the custom filter function if it exists
   * or perform regular filtering otherwise */
  if(filter_data->XD(custom_filter)) {
    return filter_data->XD(custom_filter)
      (hit, org, dir, range, filter_data->custom_filter_data, global_data);
  }

  /* There is no intersection to discard */
  hit_from = &filter_data->XD(hit);
  if(SXD_HIT_NONE(hit_from)) return 0;

  if(SXD_PRIMITIVE_EQ(&hit_from->prim, &hit->prim)) return 1;

  /* No displacement => assume self intersection in all situations */
  if(hit->distance <= 0) return 1;

  if(eq_epsf(hit->distance, 0, (float)filter_data->epsilon)) {
    float pos[DIM];
    int reject_hit = 0;
    fX(add)(pos, org, fX(mulf)(pos, dir, hit->distance));
    /* If the targeted point is near of the origin, check that it lies on an
     * edge/vertex shared by the 2 primitives */
#if DIM == 2
    reject_hit = hit_shared_vertex(&hit_from->prim, &hit->prim, org, pos);
#else
    reject_hit = hit_shared_edge
      (&hit_from->prim, &hit->prim, hit_from->uv, hit->uv, org, pos);
#endif
    if(reject_hit) return 1;
  }

  /* If the hit to be considered is (approximately) on a boundary between 2
   * primitives, it may belong to the wrong primitive, i.e. the one that doesn't
   * "face" the direction of the ray. We therefore check the enclosure towards
   * which it is directed, and reject it if it is not the same as the one from
   * which the ray originates */
  if(filter_data->scn && HIT_ON_BOUNDARY(hit, org, dir)) {
    unsigned enc_ids[2] = {ENCLOSURE_ID_NULL, ENCLOSURE_ID_NULL};
    unsigned chk_enc_id = ENCLOSURE_ID_NULL;

    scene_get_enclosure_ids(filter_data->scn, hit->prim.prim_id, enc_ids);
    chk_enc_id = fX(dot)(dir, hit->normal) < 0
      ? enc_ids[0] /* Front */
      : enc_ids[1]; /* Back */

    if(chk_enc_id != filter_data->enc_id) return 1;
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
XD(scene_indices)(const unsigned iprim, unsigned ids[DIM], void* data)
{
  struct sencXd(scene)* scn = data;
  SENCXD(scene_get_primitive(scn, iprim, ids));
}

/* Retrieve the coordinates of a vertex of a Star-EncXD descriptor */
static void
XD(scene_position)(const unsigned ivert, float out_pos[DIM], void* data)
{
  struct sencXd(scene)* scn = data;
  double pos[3];
  int i;
  SENCXD(scene_get_vertex(scn, ivert, pos));
  FOR_EACH(i, 0, DIM) out_pos[i] = (float)pos[i];
}

/* Retrieve the indices of a primitive of a Star-EncXD enclosure */
static void
XD(enclosure_indices)(const unsigned iprim, unsigned ids[DIM], void* data)
{
  struct sencXd(enclosure)* enc = data;
  SENCXD(enclosure_get_primitive(enc, iprim, ids));
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
   sdis_get_primitive_indices_T indices,
   sdis_get_primitive_interface_T interf,
   const size_t nverts, /* #vertices */
   sdis_get_vertex_position_T position,
   void* ctx,
   struct sencXd(scene)** out_scn)
{
  struct geometry geom;
  struct sencXd(device)* senc = NULL;
  struct sencXd(scene)* senc_scn = NULL;
  unsigned count;
  res_T res = RES_OK;
  ASSERT(scn && nprims && indices && interf && nverts && position && out_scn);

  res = sencXd(device_create)(scn->dev->logger, scn->dev->allocator,
    scn->dev->nthreads, scn->dev->verbose, &senc);
  if(res != RES_OK) goto error;

  /* Setup the geometry data */
  geom.indices = indices;
  geom.interf = interf;
  geom.position = position;
  geom.data = ctx;
  res = sencXd(scene_create)(senc,
    SENCXD_(CONVENTION_NORMAL_BACK) | SENCXD_(CONVENTION_NORMAL_OUTSIDE),
    (unsigned)nprims, XD(geometry_indices), geometry_media,
    (unsigned)nverts, XD(geometry_position), &geom, &senc_scn);
  if(res != RES_OK) goto error;
  /* With il-formed scenes, scene creation can success without being able
   * to extract enclosures; in this case just fail */
  res = sencXd(scene_get_enclosure_count(senc_scn, &count));
  if(res != RES_OK) {
    count = 0;
#if DIM==2
    senc2d_scene_get_overlapping_segments_count(senc_scn, &count);
    if(count > 0)
      log_err(scn->dev,
        "%s: the scene includes overlapping segments.\n",
        FUNC_NAME);
#else
    senc3d_scene_get_overlapping_triangles_count(senc_scn, &count);
    if(count > 0)
      log_err(scn->dev,
        "%s: the scene includes overlapping triangles.\n",
        FUNC_NAME);
#endif
    goto error;
    }

exit:
  if(senc) SENCXD(device_ref_put(senc));
  if(out_scn) *out_scn = senc_scn;
  return res;
error:
  if(senc_scn) {
    SENCXD(scene_ref_put(senc_scn));
    senc_scn = NULL;
  }
  goto exit;
}

/* Register the media and the interfaces, map each primitive to its interface
 * and associated to each primitive the identifier of the enclosures that it
 * splits. */
static res_T
XD(setup_properties)
  (struct sdis_scene* scn,
   struct sencXd(scene)* senc_scn,
   void (*interf)(const size_t itri, struct sdis_interface**, void*),
   void* ctx)
{
  unsigned iprim, nprims;
  res_T res = RES_OK;
  ASSERT(scn && senc_scn && interf);

  clear_properties(scn);

  SENCXD(scene_get_primitives_count(senc_scn, &nprims));
  FOR_EACH(iprim, 0, nprims) {
    struct prim_prop* prim_prop;
    struct sdis_interface* itface;
    unsigned enclosures[2];
    unsigned id;
    int i;
    double* enc_upper_bound;
    size_t ninterfaces;

    /* Fetch the enclosures that the segment/triangle splits */
    SENCXD(scene_get_primitive_enclosures(senc_scn, iprim, enclosures));

    /* Fetch the interface of the primitive */
    interf(iprim, &itface, ctx);

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
XD(setup_scene_geometry)(struct sdis_scene* scn, struct sencXd(scene)* senc_scn)
{
  struct sXd(device)* sXd_dev = NULL;
  struct sXd(shape)* sXd_shape = NULL;
  struct sXd(scene)* sXd_scn = NULL;
  struct sXd(vertex_data) vdata = SXD_VERTEX_DATA_NULL;
  unsigned nprims, nverts;
  res_T res = RES_OK;
  ASSERT(scn && senc_scn);

  SENCXD(scene_get_vertices_count(senc_scn, &nverts));

  /* Setup the vertex data */
  vdata.usage = SXD_POSITION;
  vdata.type = SXD_FLOATX;
  vdata.get = XD(scene_position);

  /* Create the Star-XD geometry of the whole scene */
  #define CALL(Func)  { if(RES_OK != (res = Func)) goto error; } (void)0
  sXd_dev = scn->dev->sXd(dev);
  SENCXD(scene_get_primitives_count(senc_scn, &nprims));
#if DIM == 2
  CALL(sXd(shape_create_line_segments)(sXd_dev, &sXd_shape));
  CALL(sXd(line_segments_set_hit_filter_function)(sXd_shape,
    XD(hit_filter_function), NULL));
  CALL(sXd(line_segments_setup_indexed_vertices)(sXd_shape, nprims,
    XD(scene_indices), nverts, &vdata, 1, senc_scn));
#else
  CALL(sXd(shape_create_mesh)(sXd_dev, &sXd_shape));
  CALL(sXd(mesh_set_hit_filter_function)(sXd_shape, XD(hit_filter_function), NULL));
  CALL(sXd(mesh_setup_indexed_vertices)(sXd_shape, nprims, XD(scene_indices),
    nverts, &vdata, 1, senc_scn));
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
XD(register_enclosure)(struct sdis_scene* scn, struct sencXd(enclosure)* enc)
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
  struct sencXd(enclosure_header) header;
  res_T res = RES_OK;
  ASSERT(scn && enc);

  enclosure_init(scn->dev->allocator, &enc_dummy);

  SENCXD(enclosure_get_header(enc, &header));
  sXd_dev = scn->dev->sXd(dev);
  nprims = header.primitives_count;
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

  /* Setup the medium id of the enclosure */
  if(header.enclosed_media_count > 1) {
    enc_data->medium_id = MEDIUM_ID_MULTI;
  } else {
    SENCXD(enclosure_get_medium(enc, 0, &enc_data->medium_id));
  }

  /* Do not configure the enclosure geometry for enclosures that are infinite
   * or composed of several media, i.e. that define boundary conditions */
  if(header.is_infinite || header.enclosed_media_count > 1)
    goto exit;

  /* Setup the vertex data */
  vdata.usage = SXD_POSITION;
  vdata.type = SXD_FLOATX;
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
    enum sencXd(side) side;
    SENCXD(enclosure_get_primitive_id
      (enc, iprim, darray_uint_data_get(&enc_data->local2global)+iprim, &side));
  }

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
XD(setup_enclosures)(struct sdis_scene* scn, struct sencXd(scene)* senc_scn)
{
  struct sencXd(enclosure)* enc = NULL;
  unsigned ienc, nencs;
  int inner_multi = 0;
  res_T res = RES_OK;
  ASSERT(scn && senc_scn);

  SENCXD(scene_get_enclosure_count(senc_scn, &nencs));
  FOR_EACH(ienc, 0, nencs) {
    struct sencXd(enclosure_header) header;

    SENCXD(scene_get_enclosure(senc_scn, ienc, &enc));
    SENCXD(enclosure_get_header(enc, &header));

    if(header.is_infinite) {
      ASSERT(scn->outer_enclosure_id == UINT_MAX); /* Not set yet */
      scn->outer_enclosure_id = ienc;
    }

    if(header.enclosed_media_count != 1 && !header.is_infinite)
      inner_multi++;

    res = XD(register_enclosure)(scn, enc);
    if(res != RES_OK) goto error;

    SENCXD(enclosure_ref_put(enc));
    enc = NULL;
  }

  if(inner_multi) {
    log_info(scn->dev,
      "# Found %d internal enclosure(s) with more than 1 medium.\n",
      inner_multi);
  }

  /* tmp table no more useful */
  htable_d_purge(&scn->tmp_hc_ub);
exit:
  if(enc) SENCXD(enclosure_ref_put(enc));
  return res;
error:
  goto exit;
}

#if DIM == 2
static res_T
setup_primitive_keys_2d(struct sdis_scene* scn, struct senc2d_scene* senc_scn)
{
  unsigned iprim = 0;
  unsigned nprims = 0;
  res_T res = RES_OK;
  ASSERT(scn && senc_scn);

  SENC2D(scene_get_primitives_count(senc_scn, &nprims));

  FOR_EACH(iprim, 0, nprims) {
    struct s2d_primitive prim = S2D_PRIMITIVE_NULL;
    struct sdis_primkey key = SDIS_PRIMKEY_NULL;
    unsigned ids[2] = {0,0};
    double v0[2] = {0,0};
    double v1[2] = {0,0};

    /* Retrieve positions from Star-Enclosre, not Star-2D. Star-Enclosure keeps
     * the positions submitted by the user as they are, without any
     * transformation or conversion (Star-2D converts them to float). This
     * ensures that the caller can construct the same key from his data */
    SENC2D(scene_get_primitive(senc_scn, iprim, ids));
    SENC2D(scene_get_vertex(senc_scn, ids[0], v0));
    SENC2D(scene_get_vertex(senc_scn, ids[1], v1));
    S2D(scene_view_get_primitive(scn->s2d_view, iprim, &prim));

    sdis_primkey_2d_setup(&key, v0, v1);

    res = htable_key2prim2d_set(&scn->key2prim2d, &key, &prim);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  htable_key2prim2d_purge(&scn->key2prim2d);
  goto exit;
}

#elif DIM == 3
static res_T
setup_primitive_keys_3d(struct sdis_scene* scn, struct senc3d_scene* senc_scn)
{
  unsigned iprim = 0;
  unsigned nprims = 0;
  res_T res = RES_OK;
  ASSERT(scn && senc_scn);

  SENC3D(scene_get_primitives_count(senc_scn, &nprims));

  FOR_EACH(iprim, 0, nprims) {
    struct s3d_primitive prim = S3D_PRIMITIVE_NULL;
    struct sdis_primkey key = SDIS_PRIMKEY_NULL;
    unsigned ids[3] = {0};
    double v0[3] = {0};
    double v1[3] = {0};
    double v2[3] = {0};

    /* Retrieve positions from Star-Enclosre, not Star-3D. Star-Enclosure keeps
     * the positions submitted by the user as they are, without any
     * transformation or conversion (Star-3D converts them to float). This
     * ensures that the caller can construct the same key from his data */
    SENC3D(scene_get_primitive(senc_scn, iprim, ids));
    SENC3D(scene_get_vertex(senc_scn, ids[0], v0));
    SENC3D(scene_get_vertex(senc_scn, ids[1], v1));
    SENC3D(scene_get_vertex(senc_scn, ids[2], v2));
    S3D(scene_view_get_primitive(scn->s3d_view, iprim, &prim));

    sdis_primkey_setup(&key, v0, v1, v2);

    res = htable_key2prim3d_set(&scn->key2prim3d, &key, &prim);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  htable_key2prim3d_purge(&scn->key2prim3d);
  goto exit;
}
#endif

/* Create a Stardis scene */
static res_T
XD(scene_create)
  (struct sdis_device* dev,
   const struct sdis_scene_create_args* args,
   struct sdis_scene** out_scn)
{
  struct sencXd(scene)* senc_scn = NULL;
  struct sdis_scene* scn = NULL;
  res_T res = RES_OK;

  if(!dev || !check_sdis_scene_create_args(args) || !out_scn) {
    res = RES_BAD_ARG;
    goto error;
  }

  scn = MEM_CALLOC(dev->allocator, 1, sizeof(struct sdis_scene));
  if(!scn) {
    res = RES_MEM_ERR;
    log_err(dev, "%s: unabale to allocate the scene -- %s\n",
      FUNC_NAME, res_to_cstr(res));
    goto error;
  }

  ref_init(&scn->ref);
  SDIS(device_ref_get(dev));
  scn->dev = dev;
  scn->fp_to_meter = args->fp_to_meter;
  scn->tmin = args->t_range[0];
  scn->tmax = args->t_range[1];
  scn->outer_enclosure_id = UINT_MAX;
  darray_interf_init(dev->allocator, &scn->interfaces);
  darray_medium_init(dev->allocator, &scn->media);
  darray_prim_prop_init(dev->allocator, &scn->prim_props);
  htable_enclosure_init(dev->allocator, &scn->enclosures);
  htable_d_init(dev->allocator, &scn->tmp_hc_ub);
  htable_key2prim2d_init(dev->allocator, &scn->key2prim2d);
  htable_key2prim3d_init(dev->allocator, &scn->key2prim3d);

  if(args->source) {
    SDIS(source_ref_get(args->source));
    scn->source = args->source;
  }

  if(args->radenv) {
    SDIS(radiative_env_ref_get(args->radenv));
    scn->radenv = args->radenv;
  }

  res = XD(run_analyze)
    (scn,
     args->nprimitives,
     args->get_indices,
     args->get_interface,
     args->nvertices,
     args->get_position,
     args->context,
     &senc_scn);
  if(res != RES_OK) {
    log_err(dev, "%s: unable to analyze the scene -- %s\n",
      FUNC_NAME, res_to_cstr(res));
    goto error;
  }
  res = XD(setup_properties)(scn, senc_scn, args->get_interface, args->context);
  if(res != RES_OK) {
    log_err(dev, "%s: unable to configure interfaces and media -- %s\n",
      FUNC_NAME, res_to_cstr(res));
    goto error;
  }
  res = XD(setup_scene_geometry)(scn, senc_scn);
  if(res != RES_OK) {
    log_err(dev, "%s: unable to configure scene geometry -- %s\n",
      FUNC_NAME, res_to_cstr(res));
    goto error;
  }
  res = XD(setup_enclosures)(scn, senc_scn);
  if(res != RES_OK) {
    log_err(dev, "%s: unable to configure enclosures -- %s\n",
      FUNC_NAME, res_to_cstr(res));
    goto error;
  }
  res = XD(setup_primitive_keys)(scn, senc_scn);
  if(res != RES_OK) {
    log_err(dev, "%s: unable to configure primitive keys -- %s\n",
      FUNC_NAME, res_to_cstr(res));
    goto error;
  }
  scn->sencXd(scn) = senc_scn;

exit:
  if(out_scn) *out_scn = scn;
  return res;
error:
  if(senc_scn) SENCXD(scene_ref_put(senc_scn));
  if(scn) {
    SDIS(scene_ref_put(scn));
    scn = NULL;
  }
  goto exit;
}

static res_T
XD(scene_find_closest_point)
  (const struct sdis_scene* scn,
   const struct sdis_scene_find_closest_point_args* args,
   size_t* iprim,
   double uv[2])
{
  struct sXd(hit) hit;
  float query_pos[DIM];
  float query_radius;
  res_T res = RES_OK;

  if(!scn || !iprim || !uv || scene_is_2d(scn) != (DIM == 2)) {
    res = RES_BAD_ARG;
    goto error;
  }
  res = check_sdis_scene_find_closest_point_args(args);
  if(res != RES_OK) goto error;

  /* Avoid a null query radius due to casting in single-precision */
  query_radius = MMAX((float)args->radius, FLT_MIN);

  fX_set_dX(query_pos, args->position);

  /* Do not filter anything */
  if(!args->XD(filter)) {
    res = sXd(scene_view_closest_point)
      (scn->sXd(view), query_pos, query_radius, NULL, &hit);

  /* Filter points according to user-defined filter function */
  } else {
    struct hit_filter_data filter_data = HIT_FILTER_DATA_NULL;
    filter_data.XD(custom_filter) = args->XD(filter);
    filter_data.custom_filter_data = args->filter_data;
    res = sXd(scene_view_closest_point)
      (scn->sXd(view), query_pos, query_radius, &filter_data, &hit);
  }

  if(res != RES_OK) {
    log_err(scn->dev,
      "%s: error querying the closest position at `"FORMAT_VECX"' "
      "for a radius of %g -- %s.\n",
      FUNC_NAME, SPLITX(query_pos), query_radius, res_to_cstr(res));
   goto error;
  }

  if(SXD_HIT_NONE(&hit)) {
    *iprim = SDIS_PRIMITIVE_NONE;
  } else {
    *iprim = hit.prim.scene_prim_id;
#if DIM == 2
    uv[0] = hit.u;
#else
    uv[0] = hit.uv[0];
    uv[1] = hit.uv[1];
#endif
  }

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
static res_T
XD(scene_get_enclosure_id)
  (struct sdis_scene* scn,
   const double pos[DIM],
   unsigned* out_enc_id)
{
  size_t iprim, nprims;
  float P[DIM];
  /* Range of the parametric coordinate into which positions are challenged */
#if DIM == 2
  float st[3];
#else
  float st[3][2];
#endif
  size_t nsteps = 3;
  unsigned enc_id = ENCLOSURE_ID_NULL;
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

  fX_set_dX(P, pos);

  SXD(scene_view_primitives_count(scn->sXd(view), &nprims));
  FOR_EACH(iprim, 0, nprims) {
    struct sXd(hit) hit;
    struct sXd(attrib) attr;
    struct sXd(primitive) prim;
    size_t iprim2;
    const float range[2] = {FLT_MIN, FLT_MAX};
    float N[DIM] = {0};
    float dir[DIM], cos_N_dir;
    size_t istep = 0;

    /* 1 primitive over 2, take a primitive from the end of the primitive list.
     * When primitives are sorted in a coherent manner regarding their
     * position, this strategy avoids to test primitives that are going to be
     * rejected of the same manner due to possible numerical issues of the
     * resulting intersection. */
    if((iprim % 2) == 0) {
      iprim2 = iprim / 2;
    } else {
      iprim2 = nprims - 1 - (iprim / 2);
    }

    do {
      /* Retrieve a position onto the primitive */
      SXD(scene_view_get_primitive(scn->sXd(view), (unsigned)iprim2, &prim));
      SXD(primitive_get_attrib(&prim, SXD_POSITION, st[istep], &attr));

      /* Trace a ray from the random walk vertex toward the retrieved primitive
       * position */
      fX(normalize)(dir, fX(sub)(dir, attr.value, P));
      SXD(scene_view_trace_ray(scn->sXd(view), P, dir, range, NULL, &hit));

    /* Try another position onto the current primitive if there is no
     * intersection or if it is on a vertex/edge */
    } while((SXD_HIT_NONE(&hit) || HIT_ON_BOUNDARY(&hit, P, dir))
         && ++istep < nsteps);

    /* No valid intersection is found on the current primitive.
     * Challenge another. */
    if(istep >= nsteps) continue;

    fX(normalize)(N, hit.normal);
    cos_N_dir = fX(dot)(N, dir);

    /* Not too close and not roughly orthognonal */
    if(hit.distance > 1.e-6 && absf(cos_N_dir) > 1.e-2f) {
      unsigned enc_ids[2];
      scene_get_enclosure_ids(scn, hit.prim.prim_id, enc_ids);
      enc_id = cos_N_dir < 0 ? enc_ids[0] : enc_ids[1];

      break; /* That's all folks */
    }
  }

  if(iprim >= nprims) {
    log_warn(scn->dev,
      "%s: cannot retrieve current enclosure at {%g, %g, %g}.\n",
      FUNC_NAME, P[0], P[1], DIM == 3 ? P[2] : 0);
    res = RES_BAD_OP;
    goto error;
  }

  if(iprim > 10 && iprim > (size_t)((double)nprims * 0.05)) {
    log_warn(scn->dev,
      "%s: performance issue. Up to %lu primitives were tested to find "
      "current enclosure at {%g, %g, %g}.\n",
      FUNC_NAME, (unsigned long)iprim, P[0], P[1], DIM == 3 ? P[2] : 0);
  }

exit:
  *out_enc_id = enc_id;
  return res;
error:
  enc_id = ENCLOSURE_ID_NULL;
  goto exit;
}

static res_T
XD(scene_get_enclosure_id_in_closed_boundaries)
  (struct sdis_scene* scn,
   const double pos[DIM],
   unsigned* out_enc_id)
{
  float dirs[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  float frame[DIM*DIM];
  float P[DIM];
  unsigned enc_id = ENCLOSURE_ID_NULL;
  int idir;
  res_T res = RES_OK;
  ASSERT(scn && pos && out_enc_id);

  /* Build a frame that will be used to rotate the main axis by PI/4 around
   * each axis. This can avoid numerical issues when geometry is discretized
   * along the main axis */
#if DIM == 2
  f22_rotation(frame, (float)PI/4);
#else
  f33_rotation(frame, (float)PI/4, (float)PI/4, (float)PI/4);
#endif

  fX_set_dX(P, pos);
  FOR_EACH(idir, 0, 2*DIM) {
    struct sXd(hit) hit;
    float N[DIM] = {0};
    const float range[2] = {FLT_MIN, FLT_MAX};
    float cos_N_dir;

    /* Transform the directions to avoid to be aligned with the axis */
    fXX_mulfX(dirs[idir], frame, dirs[idir]);

    /* Trace a ray from the random walk vertex toward the retrieved primitive
     * position */
    SXD(scene_view_trace_ray(scn->sXd(view), P, dirs[idir], range, NULL, &hit));

    /* Unforeseen error. One has to intersect a primitive ! */
    if(SXD_HIT_NONE(&hit)) continue;

    /* Discard a hits if it lies on an edge/point */
    if(HIT_ON_BOUNDARY(&hit, P, dirs[idir])) continue;

    fX(normalize)(N, hit.normal);
    cos_N_dir = fX(dot)(N, dirs[idir]);

    /* Not too close and not roughly orthogonal */
    if(hit.distance > 1.e-6 && absf(cos_N_dir) > 1.e-2f) {
      unsigned enc_ids[2];
      scene_get_enclosure_ids(scn, hit.prim.prim_id, enc_ids);
      enc_id = cos_N_dir < 0 ? enc_ids[0] : enc_ids[1];

      break; /* That's all folks */
    }
  }
  if(idir >= 2*DIM) { /* Fallback to scene_get_enclosure_id function */
    res = XD(scene_get_enclosure_id)(scn, pos, &enc_id);
    if(res != RES_OK) goto error;
  }

exit:
  *out_enc_id = enc_id;
  return res;
error:
  enc_id = ENCLOSURE_ID_NULL;
  goto exit;
}

#if (SDIS_XD_DIMENSION == 2)
#include <star/sencX2d_undefs.h>
#else /* SDIS_XD_DIMENSION == 3 */
#include <star/sencX3d_undefs.h>
#endif

#undef HIT_ON_BOUNDARY

#include "sdis_Xd_end.h"
