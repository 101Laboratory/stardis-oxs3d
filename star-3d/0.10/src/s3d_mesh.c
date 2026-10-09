/* Copyright (C) 2015-2023 |Méso|Star> (contact@meso-star.com)
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

#include "s3d_c.h"
#include "s3d_device_c.h"
#include "s3d_mesh.h"

#include <rsys/float3.h>

/* Number of floats added to the vertex position in order to ensure the Embree
 * vertex padding constraint */
#define POSITION_PADDING 1

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
mesh_setup_indices
  (struct mesh* mesh,
   const unsigned ntris,
   void (*get_indices)(const unsigned itri, unsigned ids[3], void*),
   const unsigned nverts,
   void* data)
{
  uint32_t* indices;
  unsigned itri;
  unsigned ntris_prev;
  unsigned nverts_new;
  res_T res;
  ASSERT(mesh && ntris && nverts);

  ntris_prev = (unsigned)mesh_get_ntris(mesh);
  ASSERT(get_indices != S3D_KEEP || ntris == ntris_prev);
  (void)ntris_prev;

  if(get_indices == S3D_KEEP)
    return;

  if(mesh->indices) { /* Release the old index buffer */
    index_buffer_ref_put(mesh->indices);
    mesh->indices = NULL;
  }

  /* Allocate the new index buffer */
  res = index_buffer_create(mesh->dev->allocator, &mesh->indices);
  if(res != RES_OK) FATAL("Unsufficient memory\n");
  res = darray_u32_resize(&mesh->indices->data, ntris * 3/*# triangle ids*/);
  if(res != RES_OK) FATAL("Unsufficient memory\n");

  /* Setup the mesh indices */
  indices = mesh_get_ids(mesh);
  nverts_new = 0;
  FOR_EACH(itri, 0, ntris) {
    uint32_t* ids = indices + itri*3;
    int i;
    STATIC_ASSERT(sizeof(unsigned) == sizeof(uint32_t), Unexpected_Type);
    get_indices(itri, ids, data);
    FOR_EACH(i, 0, 3) nverts_new = MMAX(nverts_new, ids[i]);
  }
  /* Transform nverts from the last vertex id to vertices count */
  ++nverts_new;
  if(nverts_new > nverts)
    FATAL("Out of bound indexation\n");
}

static void
mesh_setup_positions
  (struct mesh* mesh,
   const unsigned nverts,
   struct s3d_vertex_data* attr,
   void* data)
{
  float* positions;
  unsigned ivert;
  res_T res;
  ASSERT(mesh && nverts && attr && attr->usage == S3D_POSITION);

  if(attr->get == S3D_KEEP) {
    ASSERT(mesh->attribs[S3D_POSITION]);
    ASSERT(darray_float_size_get
      (&mesh->attribs[S3D_POSITION]->data) - POSITION_PADDING == nverts*3);
    return;
  }

  if(mesh->attribs[S3D_POSITION]) { /* Release the old vertex buffer */
    vertex_buffer_ref_put(mesh->attribs[S3D_POSITION]);
    mesh->attribs[S3D_POSITION] = NULL;
  }

  /* Allocate vertex positions */
  res = vertex_buffer_create(mesh->dev->allocator, &mesh->attribs[S3D_POSITION]);
  if(res != RES_OK) FATAL("Insufficient memory\n");

  /* Embree requires that the last element is at least 16bytes length. One has
   * thus to add some padding to the buffer of positions. */
  res = darray_float_resize
    (&mesh->attribs[S3D_POSITION]->data, nverts*3 + POSITION_PADDING);
  if(res != RES_OK) FATAL("Insufficient memory\n");
  mesh->attribs_type[S3D_POSITION] = S3D_FLOAT3;

  /* Setup the vertex positions */
  positions = darray_float_data_get(&mesh->attribs[S3D_POSITION]->data);
  if(attr->type == S3D_FLOAT3) {
    FOR_EACH(ivert, 0, nverts) {
      attr->get(ivert, positions + ivert*3, data);
    }
  } else {
    FOR_EACH(ivert, 0, nverts) {
      float pos[4];
      unsigned ipos = ivert * 3;
      attr->get(ivert, pos, data);
      switch(attr->type) {
        case S3D_FLOAT:
          positions[ipos + 0] = pos[0];
          positions[ipos + 1] = 0.f;
          positions[ipos + 2] = 0.f;
          break;
        case S3D_FLOAT2:
          positions[ipos + 0] = pos[0];
          positions[ipos + 1] = pos[1];
          positions[ipos + 2] = 0.f;
          break;
        case S3D_FLOAT4: /* Homogeneous coordinates */
          positions[ipos + 0] = pos[0] / pos[3];
          positions[ipos + 1] = pos[1] / pos[3];
          positions[ipos + 2] = pos[2] / pos[3];
          break;
        default: FATAL("Unreachable code\n"); break;
      }
    }
  }
}

static void
mesh_setup_attribs
  (struct mesh* mesh,
   const unsigned nverts,
   const struct s3d_vertex_data* attr,
   void* data)
{
  float* attr_data;
  unsigned dim;
  unsigned ivert;
  res_T res;
  ASSERT(mesh && nverts && attr);
  ASSERT(attr->usage >= S3D_ATTRIB_0 && attr->usage < S3D_ATTRIBS_COUNT__);

  dim = s3d_type_get_dimension(attr->type);
  if(attr->get == S3D_KEEP) {
    ASSERT(mesh->attribs_type[attr->usage] == attr->type);
    ASSERT(mesh->attribs[attr->usage]);
    ASSERT(darray_float_size_get(&mesh->attribs[attr->usage]->data) == nverts*dim);
    return;
  }

  if(mesh->attribs[attr->usage]) { /* Release the previous vertex buffer */
    vertex_buffer_ref_put(mesh->attribs[attr->usage]);
    mesh->attribs[attr->usage] = NULL;
  }

  /* Allocate the new vertex buffer */
  res = vertex_buffer_create(mesh->dev->allocator, &mesh->attribs[attr->usage]);
  if(res != RES_OK) FATAL("Insufficient memory\n");
  res = darray_float_resize(&mesh->attribs[attr->usage]->data, nverts * dim);
  if(res != RES_OK) FATAL("Insufficient memory\n");
  mesh->attribs_type[attr->usage] = attr->type;

  /* Setup the vertex attrib */
  attr_data = darray_float_data_get(&mesh->attribs[attr->usage]->data);
  FOR_EACH(ivert, 0, nverts) {
    attr->get(ivert, attr_data, data);
    attr_data += dim;
  }
}

static FINLINE float
mesh_compute_triangle_2area(struct mesh* mesh, const size_t itri)
{
  const uint32_t* ids;
  const float* pos;
  const float* v0, *v1, *v2;
  const size_t id = itri * 3/*#ids per faces*/;
  float E0[3], E1[3], N[3];
  ASSERT(mesh && itri < mesh_get_ntris(mesh));

  ids = mesh_get_ids(mesh);
  pos = mesh_get_pos(mesh);

  v0 = pos + ids[id+0]*3/*#coords*/;
  v1 = pos + ids[id+1]*3/*#coords*/;
  v2 = pos + ids[id+2]*3/*#coords*/;
  f3_sub(E0, v1, v0);
  f3_sub(E1, v2, v0);

  return f3_len(f3_cross(N, E0, E1));
}

static void
mesh_release(ref_T* ref)
{
  struct mesh* msh;
  struct s3d_device* dev;
  ASSERT(ref);

  msh = CONTAINER_OF(ref, struct mesh, ref);
  mesh_clear(msh);
  dev = msh->dev;
  darray_float_release(&msh->cdf);
  MEM_RM(dev->allocator, msh);
  S3D(device_ref_put(dev));
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
mesh_create(struct s3d_device* dev, struct mesh** out_mesh)
{
  struct mesh* mesh = NULL;
  res_T res = RES_OK;
  ASSERT(dev && out_mesh);

  mesh = (struct mesh*)MEM_CALLOC(dev->allocator, 1, sizeof(struct mesh));
  if(!mesh) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&mesh->ref);
  S3D(device_ref_get(dev));
  mesh->dev = dev;
  darray_float_init(dev->allocator, &mesh->cdf);

exit:
  *out_mesh = mesh;
  return res;
error:
  if(mesh) {
    mesh_ref_put(mesh);
    mesh = NULL;
  }
  goto exit;
}

void
mesh_ref_get(struct mesh* mesh)
{
  ASSERT(mesh);
  ref_get(&mesh->ref);
}

void
mesh_ref_put(struct mesh* mesh)
{
  ASSERT(mesh);
  ref_put(&mesh->ref, mesh_release);
}

void
mesh_clear(struct mesh* mesh)
{
  size_t iattr;
  ASSERT(mesh);
  if(mesh->indices) {
    index_buffer_ref_put(mesh->indices);
    mesh->indices = NULL;
  }
  FOR_EACH(iattr, 0, S3D_ATTRIBS_COUNT__) {
    if(mesh->attribs[iattr]) {
      vertex_buffer_ref_put(mesh->attribs[iattr]);
      mesh->attribs[iattr] = NULL;
    }
  }
  darray_float_clear(&mesh->cdf);
}

size_t
mesh_get_ntris(const struct mesh* mesh)
{
  size_t nids;
  ASSERT(mesh);
  if(!mesh->indices)
    return 0;
  nids = darray_u32_size_get(&mesh->indices->data);
  ASSERT(nids % 3 == 0); /* Only triangular meshes are supported */
  return nids / 3;
}

size_t
mesh_get_nverts(const struct mesh* mesh)
{
  size_t ncoords;
  ASSERT(mesh);
  if(!mesh->attribs[S3D_POSITION])
    return 0;

  ASSERT(mesh->attribs_type[S3D_POSITION] == S3D_FLOAT3);
  ncoords = darray_float_size_get
    (&mesh->attribs[S3D_POSITION]->data) - POSITION_PADDING;
  ASSERT(ncoords % 3 == 0);
  return ncoords / 3;
}

uint32_t*
mesh_get_ids(struct mesh* mesh)
{
  ASSERT(mesh && mesh->indices);
  return darray_u32_data_get(&mesh->indices->data);
}

float*
mesh_get_pos(struct mesh* mesh)
{
  ASSERT(mesh && mesh->attribs[S3D_POSITION]);
  ASSERT(mesh->attribs_type[S3D_POSITION] == S3D_FLOAT3);
  return darray_float_data_get(&mesh->attribs[S3D_POSITION]->data);
}

float*
mesh_get_attr(struct mesh* mesh, const enum s3d_attrib_usage usage)
{
  ASSERT(mesh && usage < S3D_ATTRIBS_COUNT__ && mesh->attribs[usage]);
  return darray_float_data_get(&mesh->attribs[usage]->data);
}

float
mesh_compute_area(struct mesh* mesh)
{
  size_t itri, ntris;
  float area = 0.f;
  ASSERT(mesh);

  ntris = mesh_get_ntris(mesh);
  if(!ntris) return 0.f;

  FOR_EACH(itri, 0, ntris)
    area += mesh_compute_triangle_2area(mesh, itri);
  return area * 0.5f;
}

res_T
mesh_compute_cdf(struct mesh* mesh)
{
  size_t itri, ntris;
  float area = 0.f;
  res_T res = RES_OK;
  ASSERT(mesh);

  darray_float_clear(&mesh->cdf);

  ntris = mesh_get_ntris(mesh);
  if(!ntris) goto exit;

  res = darray_float_resize(&mesh->cdf, ntris);
  if(res != RES_OK) goto error;

  FOR_EACH(itri, 0, ntris) {
    area += mesh_compute_triangle_2area(mesh, itri) * 0.5f;
    darray_float_data_get(&mesh->cdf)[itri] = area;
  }
exit:
  return res;
error:
  darray_float_clear(&mesh->cdf);
  goto exit;
}

float
mesh_compute_volume(struct mesh* mesh, const char flip_surface)
{
  const uint32_t* ids;
  const float* pos;
  size_t itri, ntris;
  double volume = 0.0;
  ASSERT(mesh);

  ntris = mesh_get_ntris(mesh);
  if(!ntris) return 0.f;

  ids = mesh_get_ids(mesh);
  pos = mesh_get_pos(mesh);

  /* Build a tetrahedron whose base is the triangle and whose apex is the
   * coordinate system's origin. Then compute the volume of the tetrahedron and
   * add or sub it from the overall volume whether the normal point toward or
   * backward the apex */
  FOR_EACH(itri, 0, ntris) {
    float E0[3], E1[3], N[3];
    float B, h;
    const size_t id = itri * 3/*#ids per faces*/;
    const float* v0 = pos + ids[id+0]*3/*#coords*/;
    const float* v1 = pos + ids[id+1]*3/*#coords*/;
    const float* v2 = pos + ids[id+2]*3/*#coords*/;
    /* Front face is CW by default */
    f3_sub(E0, v2, v0);
    f3_sub(E1, v1, v0);
    if(flip_surface) {
      f3_cross(N, E1, E0);
    } else {
      f3_cross(N, E0, E1);
    }
    B = f3_normalize(N, N) * 0.5f; /* Base area */
    h = -f3_dot(N, v0); /* Height from the base to the apex */
    volume += (h*B);
  }
   return (float)(volume / 3.0);
}

res_T
mesh_setup_indexed_vertices
  (struct mesh* mesh,
   const unsigned ntris,
   void (*get_indices)(const unsigned itri, unsigned ids[3], void* ctx),
   const unsigned nverts,
   struct s3d_vertex_data attribs[],
   const unsigned nattribs,
   void* data)
{
  unsigned iattr;
  char has_position = 0;
  res_T res = RES_OK;
  ASSERT(mesh);

  if(!ntris || !nverts || !attribs || !nattribs) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Check indices description */
  if(get_indices == S3D_KEEP) {
    if(!mesh->indices) { /* No indice was previously set */
      res = RES_BAD_ARG;
      goto error;
    } else {
      const size_t nids_prev = darray_u32_size_get(&mesh->indices->data);
      const size_t ntris_prev = nids_prev / 3;
      if(ntris_prev != ntris) { /* Inconsistant data */
        res =  RES_BAD_ARG;
        goto error;
      }
    }
  }

  /* Check the vertex data description */
  iattr = 0;
  has_position = 0;
  FOR_EACH(iattr, 0, nattribs) {
    if((unsigned)attribs[iattr].usage >= S3D_ATTRIBS_COUNT__) { /* Invalid usage */
      res = RES_BAD_ARG;
      goto error;
    }
    if(attribs[iattr].get == S3D_KEEP) {
      const enum s3d_attrib_usage attr_usage = attribs[iattr].usage;
      const enum s3d_type type = attribs[iattr].type;
      if(!mesh->attribs[attr_usage]) { /* The vertex attrib was no set */
        res = RES_BAD_ARG;
        goto error;
      } else {
        const enum s3d_type type_prev = mesh->attribs_type[attr_usage];
        const struct darray_float* attr = &mesh->attribs[attr_usage]->data;
        size_t nverts_prev = darray_float_size_get(attr);
        nverts_prev /= s3d_type_get_dimension(type_prev);
        if(type_prev != type || nverts_prev != nverts) { /* Inconsistant data */
          res = RES_BAD_ARG;
          goto error;
        }
      }
    }
    if(attribs[iattr].usage == S3D_POSITION)
      has_position = 1;
  }

  if(!has_position) { /* The vertex must have a position */
    res = RES_BAD_ARG;
    goto error;
  }

  mesh_setup_indices(mesh, ntris, get_indices, nverts, data);

  /* Setup vertex data */
  FOR_EACH(iattr, 0, nattribs) {
    if(attribs[iattr].usage == S3D_POSITION) {
      mesh_setup_positions(mesh, nverts, attribs + iattr, data);
    } else {
      mesh_setup_attribs(mesh, nverts, attribs + iattr, data);
    }
  }

exit:
  return res;
error:
  goto exit;
}

void
mesh_compute_aabb(struct mesh* mesh, float lower[3], float upper[3])
{
  float* pos;
  size_t ivert, nverts;
  ASSERT(mesh && lower && upper);

  f3_splat(lower, FLT_MAX);
  f3_splat(upper,-FLT_MAX);

  nverts = mesh_get_nverts(mesh);
  if(!nverts) return;

  pos = mesh_get_pos(mesh);
  FOR_EACH(ivert, 0, nverts) {
    const size_t ipos = ivert * 3;
    f3_min(lower, lower, pos + ipos);
    f3_max(upper, upper, pos + ipos);
  }
}

void
mesh_copy_indexed_vertices(const struct mesh* src, struct mesh* dst)
{
  int i;
  ASSERT(src && dst && src != dst);

  /* Release the previous index buffer of dst */
  if(dst->indices) {
    index_buffer_ref_put(dst->indices);
    dst->indices = NULL;
  }
  /* Get a reference onto the index buffer of src */
  if(src->indices) {
    index_buffer_ref_get(src->indices);
    dst->indices = src->indices;
  }

  FOR_EACH(i, 0, S3D_ATTRIBS_COUNT__) {
    /* Release the previous vertex buffers of dst */
    if(dst->attribs[i]) {
      vertex_buffer_ref_put(dst->attribs[i]);
      dst->attribs[i] = NULL;
    }
    /* Get a reference onto the vertex buffers of src */
    if(src->attribs[i]) {
      vertex_buffer_ref_get(src->attribs[i]);
      dst->attribs[i] = src->attribs[i];
      dst->attribs_type[i] = src->attribs_type[i];
    }
  }
}

