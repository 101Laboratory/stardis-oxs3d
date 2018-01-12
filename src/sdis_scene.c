/* Copyright (C) |Meso|Star> 2016-2018 (contact@meso-star.com)
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

#include <rsys/float3.h>
#include <rsys/double3.h>
#include <rsys/mem_allocator.h>

#include <star/s3d.h>

#include <limits.h>

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
  res = s3d_mesh_set_hit_filter_function(s3d_msh, hit_filter_function, NULL);
  if(res != RES_OK) goto error;
  res = s3d_scene_attach_shape(s3d_scn, s3d_msh);
  if(res != RES_OK) goto error;
  res = s3d_mesh_setup_indexed_vertices(s3d_msh, (unsigned)ntris, get_indices,
    (unsigned)nverts, &vdata, 1, &context);
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
  struct sdis_scene* scn = NULL;
  res_T res = RES_OK;

  if(!dev || !out_scn || !ntris || !indices || !interf || !nverts
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
  darray_interf_init(dev->allocator, &scn->interfaces);
  darray_interf_init(dev->allocator, &scn->prim_interfaces);

  res = setup_interfaces(scn, ntris, interf, ctx);
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
  (const struct sdis_scene* scn, double lower[3], double upper[3])
{
  float low[3], upp[3];
  res_T res = RES_OK;
  if(!scn || !lower || !upper) return RES_BAD_ARG;
  res = s3d_scene_view_get_aabb(scn->s3d_view, low, upp);
  if(res != RES_OK) return res;
  d3_set_f3(lower, low);
  d3_set_f3(upper, upp);
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
    const float st[2] = { 1.f/3.f, 1.f/3.f };
    const float range[2] = {0.f, FLT_MAX};
    float N[3], P[3], dir[3], cos_N_dir;

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

int
hit_filter_function
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

