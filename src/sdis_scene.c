/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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

#define _POSIX_C_SOURCE 200809L /* mmap support */
#define _DEFAULT_SOURCE 1 /* MAP_POPULATE support */
#define _BSD_SOURCE 1 /* MAP_POPULATE for glibc < 2.19 */

#include "sdis_scene_Xd.h"

/* Generate the Generic functions of the scene */
#define SDIS_SCENE_DIMENSION 2
#include "sdis_scene_Xd.h"
#define SDIS_SCENE_DIMENSION 3
#include "sdis_scene_Xd.h"

#include "sdis.h"
#include "sdis_interface_c.h"
#include "sdis_scene_c.h"

#include <float.h>
#include <limits.h>
#ifdef COMPILER_GCC
#include <sys/mman.h>
#endif

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static void
project_position
  (const double V0[3],
   const double E0[3],
   const double N[3],
   const double NxE1[3],
   const double rcp_det,
   const double pos[3],
   double uvw[3])
{
  double T[3], Q[3], k;
  ASSERT(V0 && E0 && N && NxE1 && pos && uvw);

  /* Use Moller/Trumbore intersection test the compute the parametric
   * coordinates of the intersection between the triangle and the ray
   * `r = pos + N*d' */
  d3_sub(T, pos, V0);
  uvw[0] = d3_dot(T, NxE1) * rcp_det;
  d3_cross(Q, T, E0);
  uvw[1] = d3_dot(Q, N) * rcp_det;
  uvw[2] = 1.0 - uvw[0] - uvw[1];

  if(uvw[0] >= 0 && uvw[1] >= 0 && uvw[2] >= 0) {/* The ray hits the triangle */
    ASSERT(eq_eps(uvw[0] + uvw[1] + uvw[2], 1.0, 1.e-6));
    return;
  }

  /* Clamp barycentric coordinates to triangle edges */
  if(uvw[0] >= 0) {
    if(uvw[1] >= 0) {
      k = 1.0 / (uvw[0] + uvw[1]);
      uvw[0] *= k;
      uvw[1] *= k;
      uvw[2] = 0;
    } else if( uvw[2] >= 0) {
      k = 1.0 / (uvw[0] + uvw[2]);
      uvw[0] *= k;
      uvw[1] = 0;
      uvw[2] *= k;
    } else {
      ASSERT(uvw[0] >= 1.f);
      d3(uvw, 1, 0, 0);
    }
  } else if(uvw[1] >= 0) {
    if(uvw[2] >= 0) {
      k = 1.0 / (uvw[1] + uvw[2]);
      uvw[0] = 0;
      uvw[1] *= k;
      uvw[2] *= k;
    } else {
      ASSERT(uvw[1] >= 1);
      d3(uvw, 0, 1, 0);
    }
  } else {
    ASSERT(uvw[2] >= 1);
    d3(uvw, 0, 0, 1);
  }
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
  htable_d_release(&scn->tmp_hc_ub);
  if(scn->s2d_view) S2D(scene_view_ref_put(scn->s2d_view));
  if(scn->s3d_view) S3D(scene_view_ref_put(scn->s3d_view));
  if(scn->senc2d_scn) SENC2D(scene_ref_put(scn->senc2d_scn));
  if(scn->senc3d_scn) SENC3D(scene_ref_put(scn->senc3d_scn));
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
  return scene_create_3d
    (dev, ntris, indices, interf, nverts, position, ctx, out_scn);
}

res_T
sdis_scene_2d_create
  (struct sdis_device* dev,
   const size_t nsegs, /* #segments */
   void (*indices)(const size_t iseg, size_t ids[2], void*),
   void (*interf)(const size_t iseg, struct sdis_interface** bound, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[2], void* ctx),
   void* ctx,
   struct sdis_scene** out_scn)
{
  return scene_create_2d
    (dev, nsegs, indices, interf, nverts, position, ctx, out_scn);
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
sdis_scene_find_closest_point
  (const struct sdis_scene* scn,
   const double pos[3],
   const double radius,
   size_t* iprim,
   double uv[2])
{
  if(!scn) return RES_BAD_ARG;
  if(scene_is_2d(scn)) {
    return scene_find_closest_point_2d(scn, pos, radius, iprim, uv);
  } else {
    return scene_find_closest_point_3d(scn, pos, radius, iprim, uv);
  }
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

res_T
sdis_scene_boundary_project_position
  (const struct sdis_scene* scn,
   const size_t iprim,
   const double pos[],
   double uv[])
{
  if(!scn || !pos || !uv) return RES_BAD_ARG;
  if(iprim >= scene_get_primitives_count(scn)) return RES_BAD_ARG;

  if(scene_is_2d(scn)) {
    struct s2d_primitive prim;
    struct s2d_attrib a;
    double V[2][2]; /* Vertices */
    double E[2][3]; /* V0->V1 and V0->pos */
    double proj;

    /* Retrieve the segment vertices */
    S2D(scene_view_get_primitive(scn->s2d_view, (unsigned int)iprim, &prim));
    S2D(segment_get_vertex_attrib(&prim, 0, S2D_POSITION, &a)); d2_set_f2(V[0], a.value);
    S2D(segment_get_vertex_attrib(&prim, 1, S2D_POSITION, &a)); d2_set_f2(V[1], a.value);

    /* Compute the parametric coordinate of the project of `pos' onto the
     * segment.*/
    d2_sub(E[0], V[1], V[0]);
    d2_normalize(E[0], E[0]);
    d2_sub(E[1], pos,  V[0]);
    proj = d2_dot(E[0], E[1]);

    uv[0] = CLAMP(proj, 0, 1); /* Clamp the parametric coordinate in [0, 1] */

  } else {
    struct s3d_primitive prim;
    struct s3d_attrib a;
    double V[3][3]; /* Vertices */
    double E[2][3]; /* V0->V1 and V0->V2 edges */
    double N[3]; /* Normal */
    double NxE1[3], rcp_det; /* Muller/Trumboer triangle parameters */
    double uvw[3];

    S3D(scene_view_get_primitive(scn->s3d_view, (unsigned int)iprim, &prim));
    S3D(triangle_get_vertex_attrib(&prim, 0, S3D_POSITION, &a)); d3_set_f3(V[0], a.value);
    S3D(triangle_get_vertex_attrib(&prim, 1, S3D_POSITION, &a)); d3_set_f3(V[1], a.value);
    S3D(triangle_get_vertex_attrib(&prim, 2, S3D_POSITION, &a)); d3_set_f3(V[2], a.value);
    d3_sub(E[0], V[1], V[0]);
    d3_sub(E[1], V[2], V[0]);
    d3_cross(N, E[0], E[1]);

    /* Muller/Trumbore triangle parameters */
    d3_cross(NxE1, N, E[1]);
    rcp_det = 1.0 / d3_dot(NxE1, E[0]);

    /* Use the Muller/Trumbore intersection test to project `pos' onto the
     * triangle and to retrieve the parametric coordinates of the projection
     * point */
    project_position(V[0], E[0], N, NxE1, rcp_det, pos, uvw);

    uv[0] = uvw[2];
    uv[1] = uvw[0];
  }
  return RES_OK;
}

res_T
sdis_scene_get_senc2d_scene
  (struct sdis_scene* scn,
   struct senc2d_scene** senc2d_scn)
{
  if(!scn || !senc2d_scn) return RES_BAD_ARG;
  if(!scn->senc2d_scn) return RES_BAD_ARG; /* Scene is 3D */
  SENC2D(scene_ref_get(scn->senc2d_scn));
  *senc2d_scn = scn->senc2d_scn;
  return RES_OK;
}

res_T
sdis_scene_get_senc3d_scene
  (struct sdis_scene* scn,
   struct senc3d_scene** senc3d_scn)
{
  if(!scn || !senc3d_scn) return RES_BAD_ARG;
  if(!scn->senc3d_scn) return RES_BAD_ARG; /* Scene is 2D */
  SENC3D(scene_ref_get(scn->senc3d_scn));
  *senc3d_scn = scn->senc3d_scn;
  return RES_OK;
}

res_T
sdis_scene_get_dimension
  (const struct sdis_scene* scn, enum sdis_scene_dimension* dim)
{
  if(!scn || !dim) return RES_BAD_ARG;
  *dim = scene_is_2d(scn) ? SDIS_SCENE_2D : SDIS_SCENE_3D;
  return RES_OK;
}

res_T
sdis_scene_get_medium_spread
  (struct sdis_scene* scn,
   const struct sdis_medium* mdm,
   double* out_spread)
{
  struct htable_enclosure_iterator it, end;
  double spread = 0;
  res_T res = RES_OK;

  if(!scn || !mdm || !out_spread) {
    res = RES_BAD_ARG;
    goto error;
  }

  htable_enclosure_begin(&scn->enclosures, &it);
  htable_enclosure_end(&scn->enclosures, &end);
  while(!htable_enclosure_iterator_eq(&it, &end)) {
    const struct enclosure* enc = htable_enclosure_iterator_data_get(&it);
    htable_enclosure_iterator_next(&it);
    if(sdis_medium_get_id(mdm) == enc->medium_id) {
      spread += enc->V;
    }
  }
  *out_spread = spread;

exit:
  return res;
error:
  goto exit;
}

/*******************************************************************************
 * Local miscellaneous function
 ******************************************************************************/
struct sdis_interface*
scene_get_interface(const struct sdis_scene* scn, const unsigned iprim)
{
  ASSERT(scn && iprim < darray_prim_prop_size_get(&scn->prim_props));
  return darray_prim_prop_cdata_get(&scn->prim_props)[iprim].interf;
}

res_T
scene_get_medium
  (const struct sdis_scene* scn,
   const double pos[],
   struct get_medium_info* info,
   struct sdis_medium** out_medium)
{
  return scene_is_2d(scn)
    ? scene_get_medium_2d(scn, pos, info, out_medium)
    : scene_get_medium_3d(scn, pos, info, out_medium);
}

res_T
scene_get_medium_in_closed_boundaries
  (const struct sdis_scene* scn,
   const double pos[],
   struct sdis_medium** out_medium)
{
  return scene_is_2d(scn)
    ? scene_get_medium_in_closed_boundaries_2d(scn, pos, out_medium)
    : scene_get_medium_in_closed_boundaries_3d(scn, pos, out_medium);
}

res_T
scene_compute_hash(const struct sdis_scene* scn, hash256_T hash)
{
  void* data = NULL;
  FILE* stream = NULL;
  size_t iprim, nprims;
  size_t len;
  res_T res = RES_OK;
  ASSERT(scn && hash);

  stream = tmpfile();
  if(!stream) {
    res = RES_IO_ERR;
    goto error;
  }

  #define WRITE(Var, Nb)                                                       \
  if(fwrite((Var), sizeof(*(Var)), Nb, stream) != (Nb)) {                      \
    res = RES_IO_ERR;                                                          \
    goto error;                                                                \
  } (void)0
  if(scene_is_2d(scn)) {
    S2D(scene_view_primitives_count(scn->s2d_view, &nprims));
  } else {
    S3D(scene_view_primitives_count(scn->s3d_view, &nprims));
  }
  FOR_EACH(iprim, 0, nprims) {
    struct sdis_interface* interf = NULL;
    size_t ivert;

    if(scene_is_2d(scn)) {
      struct s2d_primitive prim;
      S2D(scene_view_get_primitive(scn->s2d_view, (unsigned)iprim, &prim));
      FOR_EACH(ivert, 0, 2) {
        struct s2d_attrib attr;
        S2D(segment_get_vertex_attrib(&prim, ivert, S2D_POSITION, &attr));
        WRITE(attr.value, 2);
      }
    } else {
      struct s3d_primitive prim;
      S3D(scene_view_get_primitive(scn->s3d_view, (unsigned)iprim, &prim));
      FOR_EACH(ivert, 0, 3) {
        struct s3d_attrib attr;
        S3D(triangle_get_vertex_attrib(&prim, ivert, S3D_POSITION, &attr));
        WRITE(attr.value, 3);
      }
    }

    interf = scene_get_interface(scn, (unsigned)iprim);
    WRITE(&interf->medium_front->type, 1);
    WRITE(&interf->medium_front->id, 1);
    WRITE(&interf->medium_back->type, 1);
    WRITE(&interf->medium_back->id, 1);
  }
  #undef WRITE

  len = (size_t)ftell(stream);
  rewind(stream);
#ifdef COMPILER_GCC
  data = mmap(NULL, len, PROT_READ, MAP_PRIVATE|MAP_POPULATE, fileno(stream), 0);
  if(data == MAP_FAILED) {
    res = RES_IO_ERR;
    goto error;
  }
#else
  data = MEM_ALLOC_ALIGNED(scn->dev->allocator, len, 16);
  if(!data) {
    res = RES_MEM_ERR;
    goto error;
  }
  if(fread(data, len, 1, stream) != 1) {
    res = RES_IO_ERR;
    goto error;
  }
#endif

  res = hash_sha256(scn->dev->allocator, data, len, hash);
  if(res != RES_OK) goto error;

exit:
#ifdef COMPILER_GCC
  if(data) munmap(data, len);
#else
  if(data) MEM_RM(scn->dev->allocator, data);
#endif
  if(stream) fclose(stream);
  return res;
error:
  goto exit;
}
