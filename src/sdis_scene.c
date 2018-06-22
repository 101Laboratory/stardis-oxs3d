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
#include "sdis_scene_c.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/mem_allocator.h>

#include <limits.h>

/* Context used to wrap the user geometry to Star-XD. */
struct geometry_context {
  void (*indices)(const size_t iprim, size_t ids[], void*);
  void (*position)(const size_t ivert, double pos[], void*);
  void* data;
};

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
get_indices_3d(const unsigned itri, unsigned out_ids[3], void* data)
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
get_position_2d(const unsigned ivert, float out_pos[2], void* data)
{
  struct geometry_context* ctx = data;
  double pos[2];
  ASSERT(ctx);
  ctx->position(ivert, pos, ctx->data);
  out_pos[0] = (float)pos[0];
  out_pos[1] = (float)pos[1];
}

static void
get_position_3d(const unsigned ivert, float out_pos[3], void* data)
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
  FOR_EACH(i, 0, darray_interf_size_get(&scn->interfaces)) {
    if(darray_interf_cdata_get(&scn->interfaces)[i]) {
      SDIS(interface_ref_put(darray_interf_data_get(&scn->interfaces)[i]));
    }
  }
  darray_interf_clear(&scn->interfaces);
  darray_interf_clear(&scn->prim_interfaces);
}

static res_T
setup_interfaces
  (struct sdis_scene* scn,
   const size_t ntris, /* #triangles */
   void (*interf)(const size_t itri, struct sdis_interface**, void*),
   void* ctx)
{
  size_t itri;
  res_T res = RES_OK;
  ASSERT(ntris && interf);

  clear_interfaces(scn);

  FOR_EACH(itri, 0, ntris) {
    struct sdis_interface* itface;
    size_t ninterfaces;
    unsigned id;

    /* Retrieve the interface of the primitive */
    interf(itri, &itface, ctx);
    id = interface_get_id(itface);

    /* Check that the interface is already registered against the scene */
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

    /* Register the primitive interface */
    res = darray_interf_push_back(&scn->prim_interfaces, &itface);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  clear_interfaces(scn);
  goto exit;
}

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

static res_T
setup_geometry_3d
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
  vdata.get = get_position_3d;

  /* Create the Star-3D geometry */
  res = s3d_scene_create(scn->dev->s3d, &s3d_scn);
  if(res != RES_OK) goto error;
  res = s3d_shape_create_mesh(scn->dev->s3d, &s3d_msh);
  if(res != RES_OK) goto error;
  res = s3d_mesh_set_hit_filter_function(s3d_msh, hit_filter_function_3d, NULL);
  if(res != RES_OK) goto error;
  res = s3d_scene_attach_shape(s3d_scn, s3d_msh);
  if(res != RES_OK) goto error;
  res = s3d_mesh_setup_indexed_vertices(s3d_msh, (unsigned)ntris,
    get_indices_3d, (unsigned)nverts, &vdata, 1, &context);
  if(res != RES_OK) goto error;
  res = s3d_scene_view_create(s3d_scn, S3D_SAMPLE|S3D_TRACE|S3D_GET_PRIMITIVE,
    &scn->s3d_view);
  if(res != RES_OK) goto error;

exit:
  if(s3d_msh) S3D(shape_ref_put(s3d_msh));
  if(s3d_scn) S3D(scene_ref_put(s3d_scn));
  return res;
error:
  if(scn->s3d_view) S3D(scene_view_ref_put(scn->s3d_view));
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
  darray_interf_init(dev->allocator, &scn->prim_interfaces);

  res = setup_interfaces(scn, nprims, interf, ctx);
  if(res != RES_OK) {
    log_err(dev, "%s: could not setup the scene interfaces.\n", FUNC_NAME);
    goto error;
  }

  if(is_2d) {
    res = setup_geometry_2d(scn, nprims, indices, nverts, position, ctx);
  } else {
    res = setup_geometry_3d(scn, nprims, indices, nverts, position, ctx);
  }
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

static INLINE res_T
scene_get_medium_2d
  (const struct sdis_scene* scn,
   const double pos[2],
   struct get_medium_info* info, /* May be NULL */
   const struct sdis_medium** out_medium)
{
  const struct sdis_medium* medium = NULL;
  size_t iprim, nprims;
  size_t nfailures = 0;
  const size_t max_failures = 10;
  /* Range of the parametric coordinate into which positions are challenged */
  const float s_range[2] = {0.25, 0.75};
  const size_t s_nsteps = 3; /* #challenges per primitive into the range */
  float s_step;
  res_T res = RES_OK;
  ASSERT(scn && pos);

  s_step = (s_range[1] - s_range[0]) / (float)(s_nsteps-1);

  S2D(scene_view_primitives_count(scn->s2d_view, &nprims));
  FOR_EACH(iprim, 0, nprims) {
    struct s2d_hit hit;
    struct s2d_attrib attr;
    struct s2d_primitive prim;
    const float range[2] = {0.f, FLT_MAX};
    float N[2], P[2], dir[2], cos_N_dir;
    float s = s_range[0];

    do {
      /* Retrieve a position onto the primitive */
      S2D(scene_view_get_primitive(scn->s2d_view, (unsigned)iprim, &prim));
      S2D(primitive_get_attrib(&prim, S2D_POSITION, s, &attr));

      /* Trace a ray from the random walk vertex toward the retrieved primitive
       * position */
      f2_normalize(dir, f2_sub(dir, attr.value, f2_set_d2(P, pos)));
      S2D(scene_view_trace_ray(scn->s2d_view, P, dir, range, NULL, &hit));

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
    /* Discard the hit if it is on a vertex, i.e. between 2 segments,  and
     * target a new position onto the current primitive */
    } while((S2D_HIT_NONE(&hit) || hit_on_vertex(&hit))
         && (s+=s_step) <= s_range[1]);

    /* The hits of all targeted positions on the current primitive are on
     * vertices. Challenge positions on another primitive. */
    if(s > s_range[1]) continue;

    f2_normalize(N, hit.normal);
    cos_N_dir = f2_dot(N, dir);

    if(absf(cos_N_dir) > 1.e-1f) { /* Not roughly orthognonal */
      const struct sdis_interface* interf;
      interf = scene_get_interface(scn, hit.prim.prim_id);
      medium = interface_get_medium
        (interf, cos_N_dir < 0 ? SDIS_FRONT : SDIS_BACK);

      /* Register the get_medium_info */
      if(info) {
        f2_set(info->pos_tgt, attr.value);
        f2_set(info->ray_org, P);
        f2_set(info->ray_dir, dir);
        info->hit_2d = hit;
      }
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
   struct get_medium_info* info,
   const struct sdis_medium** out_medium)
{
  const struct sdis_medium* medium = NULL;
  size_t iprim, nprims;
  size_t nfailures = 0;
  const size_t max_failures = 10;
  float st[3][2]; /* Position to challenge onto the primitive */
  const size_t nsteps = sizeof(st)/sizeof(float[2]);
  res_T res = RES_OK;
  ASSERT(scn && pos);

  /* Setup the position to challenge onto the primitives */
  f2(st[0], 1.f/6.f, 5.f/12.f);
  f2(st[1], 5.f/12.f, 1.f/6.f);
  f2(st[2], 5.f/12.f, 5.f/12.f);

  S3D(scene_view_primitives_count(scn->s3d_view, &nprims));
  FOR_EACH(iprim, 0, nprims) {
    struct s3d_hit hit;
    struct s3d_attrib attr;
    struct s3d_primitive prim;
    const float range[2] = {0.f, FLT_MAX};
    float N[3], P[3], dir[3], cos_N_dir;
    size_t istep = 0;

    do {
      /* Retrieve a position onto the primitive */
      S3D(scene_view_get_primitive(scn->s3d_view, (unsigned)iprim, &prim));
      S3D(primitive_get_attrib(&prim, S3D_POSITION, st[istep], &attr));

      /* Trace a ray from the randomw walk vertex  toward the retrieved primitive
       * position */
      f3_normalize(dir, f3_sub(dir, attr.value, f3_set_d3(P, pos)));
      S3D(scene_view_trace_ray(scn->s3d_view, P, dir, range, NULL, &hit));

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
    /* Discard the hit if it is on an edge, i.e. between 2 triangles,  and
     * target a new position onto the current primitive */
    } while((S3D_HIT_NONE(&hit) || hit_on_edge(&hit)) && ++istep < nsteps);

    /* The hits of all targeted positions on the current primitive are on
     * edges. Challenge positions on another primitive. */
    if(istep >= nsteps) continue;

    f3_normalize(N, hit.normal);
    cos_N_dir = f3_dot(N, dir);

    if(absf(cos_N_dir) > 1.e-1f) { /* Not roughly orthognonal */
      const struct sdis_interface* interf;
      interf = scene_get_interface(scn, hit.prim.prim_id);
      medium = interface_get_medium
        (interf, cos_N_dir < 0 ? SDIS_FRONT : SDIS_BACK);

      if(info) {
        f3_set(info->pos_tgt, attr.value);
        f3_set(info->ray_org, P);
        f3_set(info->ray_dir, dir);
        info->hit_3d = hit;
      }
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
  clear_interfaces(scn);
  darray_interf_release(&scn->interfaces);
  darray_interf_release(&scn->prim_interfaces);
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
    S2D(primitive_get_attrib(&prim, S2D_POSITION, 0, &a)); d2_set_f2(V[0], a.value);
    S2D(primitive_get_attrib(&prim, S2D_POSITION, 1, &a)); d2_set_f2(V[1], a.value);

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

/*******************************************************************************
 * Local miscellaneous function
 ******************************************************************************/
const struct sdis_interface*
scene_get_interface(const struct sdis_scene* scn, const unsigned iprim)
{
  ASSERT(scn && iprim < darray_interf_size_get(&scn->prim_interfaces));
  return darray_interf_cdata_get(&scn->prim_interfaces)[iprim];
}

res_T
scene_get_medium
  (const struct sdis_scene* scn,
   const double pos[],
   struct get_medium_info* info,
   const struct sdis_medium** out_medium)
{
  return scene_is_2d(scn)
    ? scene_get_medium_2d(scn, pos, info, out_medium)
    : scene_get_medium_3d(scn, pos, info, out_medium);
}

