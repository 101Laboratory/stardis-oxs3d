/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#include "suvm.h"
#include "test_suvm_utils.h"
#include "test_suvm_ball.h"
#include "test_suvm_box.h"

#include <rsys/dynamic_array.h>
#include <rsys/double3.h>
#include <rsys/float3.h>

#define DARRAY_NAME prim
#define DARRAY_DATA struct suvm_primitive
#include <rsys/dynamic_array.h>

struct mesh {
  const double* vertices; /* List of double[3] */
  size_t nvertices;
  const size_t* tetrahedra; /* List of size_t[4] */
  size_t ntetrahedra;
  size_t tetrahedron_data_alignment;
  size_t vertex_data_alignment;
};
static const struct mesh MESH_NULL;

/*******************************************************************************
 * Geometry functions
 ******************************************************************************/
static void
get_indices(const size_t itetra, size_t ids[4], void* ctx)
{
  struct mesh* msh = ctx;
  CHK(itetra < msh->ntetrahedra);
  CHK(ids != NULL);
  ids[0] = msh->tetrahedra[itetra*4+0];
  ids[1] = msh->tetrahedra[itetra*4+1];
  ids[2] = msh->tetrahedra[itetra*4+2];
  ids[3] = msh->tetrahedra[itetra*4+3];
}

static void
get_position(const size_t ivert, double pos[3], void* ctx)
{
  struct mesh* msh = ctx;
  CHK(ctx != NULL);
  CHK(pos != NULL);
  CHK(ivert < msh->nvertices);
  pos[0] = msh->vertices[ivert*3+0];
  pos[1] = msh->vertices[ivert*3+1];
  pos[2] = msh->vertices[ivert*3+2];
}

/* Use tetra indices as tetra data */
static void
get_tetra_data(const size_t itetra, void* data, void* ctx)
{
  struct mesh* msh = ctx;
  size_t* ids = data;
  CHK(ctx != NULL);
  CHK(data != NULL);
  CHK(itetra < msh->ntetrahedra);
  CHK(IS_ALIGNED(data, msh->tetrahedron_data_alignment));
  ids[0] = msh->tetrahedra[itetra*4+0];
  ids[1] = msh->tetrahedra[itetra*4+1];
  ids[2] = msh->tetrahedra[itetra*4+2];
  ids[3] = msh->tetrahedra[itetra*4+3];
}

/* Use vertex position as vertex data */
static void
get_vert_data(const size_t ivert, void* data, void* ctx)
{
  struct mesh* msh = ctx;
  double* pos = data;
  CHK(ctx != NULL);
  CHK(data != NULL);
  CHK(ivert < msh->nvertices);
  CHK(IS_ALIGNED(data, msh->vertex_data_alignment));
  pos[0] = msh->vertices[ivert*3+0];
  pos[1] = msh->vertices[ivert*3+1];
  pos[2] = msh->vertices[ivert*3+2];
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
cmp_prim(const void* a, const void* b)
{
  const struct suvm_primitive* prim0 = a;
  const struct suvm_primitive* prim1 = b;
  CHK(prim0 && prim1);
  return prim0->iprim < prim1->iprim ? -1
       : prim0->iprim > prim1->iprim ? +1 : 0;
}

static void
check_volume_polyhedra(const struct suvm_volume* vol, struct mesh* msh)
{
  size_t iprim;
  size_t nprims;

  CHK(suvm_volume_get_primitives_count(vol, &nprims) == RES_OK);
  CHK(nprims == msh->ntetrahedra);

  FOR_EACH(iprim, 0, nprims) {
    struct suvm_primitive prim = SUVM_PRIMITIVE_NULL;
    struct suvm_polyhedron poly;
    double v[4][3];
    double E0[3];
    double E1[3];
    double N[3];
    size_t ids[4];
    float f[3];
    CHK(suvm_volume_get_primitive(vol, iprim, &prim) == RES_OK);
    CHK(!SUVM_PRIMITIVE_NONE(&prim));
    CHK(prim.iprim == iprim);
    CHK(prim.nvertices == 4);
    CHK(suvm_primitive_setup_polyhedron(&prim, &poly) == RES_OK);

    get_indices(iprim, ids, msh);
    get_position(ids[0], v[0], msh);
    get_position(ids[1], v[1], msh);
    get_position(ids[2], v[2], msh);
    get_position(ids[3], v[3], msh);

    CHK(f3_eq(poly.v[0], f3_set_d3(f, v[0])));
    CHK(f3_eq(poly.v[1], f3_set_d3(f, v[1])));
    CHK(f3_eq(poly.v[2], f3_set_d3(f, v[2])));
    CHK(f3_eq(poly.v[3], f3_set_d3(f, v[3])));

    d3_sub(E0, v[1], v[0]);
    d3_sub(E1, v[2], v[0]);
    d3_cross(N, E0, E1);
    f3_normalize(f, f3_set_d3(f, N));
    CHK(f3_eq_eps(poly.N[0], f, 1.e-4f));

    d3_sub(E0, v[3], v[0]);
    d3_sub(E1, v[1], v[0]);
    d3_cross(N, E0, E1);
    f3_normalize(f, f3_set_d3(f, N));
    CHK(f3_eq_eps(poly.N[1], f, 1.e-4f));

    d3_sub(E0, v[3], v[1]);
    d3_sub(E1, v[2], v[1]);
    d3_cross(N, E0, E1);
    f3_normalize(f, f3_set_d3(f, N));
    CHK(f3_eq_eps(poly.N[2], f, 1.e-4f));

    d3_sub(E0, v[3], v[2]);
    d3_sub(E1, v[0], v[2]);
    d3_cross(N, E0, E1);
    f3_normalize(f, f3_set_d3(f, N));
    CHK(f3_eq_eps(poly.N[3], f, 1.e-4f));
  }
}

static void
check_volume_mesh(const struct suvm_volume* vol, struct mesh* msh)
{
  struct suvm_mesh_desc desc = SUVM_MESH_DESC_NULL;
  size_t i;

  CHK(suvm_volume_get_mesh_desc(vol, &desc) == RES_OK);
  CHK(desc.nvertices == msh->nvertices);
  CHK(desc.nprimitives == msh->ntetrahedra);
  CHK(desc.dvertex == 3);
  CHK(desc.dprimitive == 4);

  FOR_EACH(i, 0, desc.nvertices) {
    CHK(desc.positions[i*3+0] == (float)msh->vertices[i*3+0]);
    CHK(desc.positions[i*3+1] == (float)msh->vertices[i*3+1]);
    CHK(desc.positions[i*3+2] == (float)msh->vertices[i*3+2]);
  }
  FOR_EACH(i, 0, desc.nprimitives) {
    CHK(desc.indices[i*4+0] == (uint32_t)msh->tetrahedra[i*4+0]);
    CHK(desc.indices[i*4+1] == (uint32_t)msh->tetrahedra[i*4+1]);
    CHK(desc.indices[i*4+2] == (uint32_t)msh->tetrahedra[i*4+2]);
    CHK(desc.indices[i*4+3] == (uint32_t)msh->tetrahedra[i*4+3]);
  }
}

static void
test_tetrahedral_mesh_creation(struct suvm_device* dev)
{
  struct mesh msh = MESH_NULL;
  struct suvm_tetrahedral_mesh_args args = SUVM_TETRAHEDRAL_MESH_ARGS_NULL;
  struct suvm_volume* vol = NULL;
  struct suvm_primitive prim = SUVM_PRIMITIVE_NULL;
  struct suvm_mesh_desc desc = SUVM_MESH_DESC_NULL;
  struct suvm_polyhedron poly;
  size_t nprims;

  args.ntetrahedra = box_ntetras;
  args.nvertices = box_nverts;
  args.get_indices = get_indices;
  args.get_position = get_position;
  args.tetrahedron_data = SUVM_DATA_NULL;
  args.vertex_data = SUVM_DATA_NULL;
  args.context = &msh;

  msh.vertices = box_vertices;
  msh.nvertices = box_nverts;
  msh.tetrahedra = box_indices;
  msh.ntetrahedra = box_ntetras;

  CHK(suvm_tetrahedral_mesh_create(NULL, &args, &vol) == RES_BAD_ARG);
  CHK(suvm_tetrahedral_mesh_create(dev, NULL, &vol) == RES_BAD_ARG);
  CHK(suvm_tetrahedral_mesh_create(dev, &args, NULL) == RES_BAD_ARG);
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);

  CHK(suvm_volume_ref_get(NULL) == RES_BAD_ARG);
  CHK(suvm_volume_ref_get(vol) == RES_OK);
  CHK(suvm_volume_ref_put(NULL) == RES_BAD_ARG);
  CHK(suvm_volume_ref_put(vol) == RES_OK);
  CHK(suvm_volume_ref_put(vol) == RES_OK);

  args.ntetrahedra = 0;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_BAD_ARG);
  args.ntetrahedra = box_ntetras;
  args.nvertices = 0;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_BAD_ARG);
  args.nvertices = box_nverts;
  args.get_indices = NULL;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_BAD_ARG);
  args.get_indices = get_indices;
  args.get_position = NULL;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_BAD_ARG);
  args.get_position = get_position;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);
  CHK(suvm_volume_ref_put(vol) == RES_OK);

  args.precompute_normals = 1;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);
  CHK(suvm_volume_ref_put(vol) == RES_OK);

  args.tetrahedron_data.get = get_tetra_data;
  args.tetrahedron_data.size = sizeof(size_t[4]);
  args.tetrahedron_data.alignment = 64;
  args.vertex_data.get = get_vert_data;
  args.vertex_data.size = sizeof(double[3]);
  args.vertex_data.alignment = 32;
  msh.tetrahedron_data_alignment = args.tetrahedron_data.alignment;
  msh.vertex_data_alignment = args.vertex_data.alignment;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);
  CHK(suvm_volume_ref_put(vol) == RES_OK);

  args.tetrahedron_data.size = 0;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_BAD_ARG);
  args.tetrahedron_data.size = sizeof(size_t[4]);
  args.tetrahedron_data.alignment = 0;
  msh.tetrahedron_data_alignment = 0;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_BAD_ARG);
  args.tetrahedron_data.alignment = 3;
  msh.tetrahedron_data_alignment = 3;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_BAD_ARG);
  args.tetrahedron_data.alignment = 64;
  msh.tetrahedron_data_alignment = 64;
  args.vertex_data.size = 0;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_BAD_ARG);
  args.vertex_data.size = sizeof(double[3]);
  args.vertex_data.alignment = 5;
  msh.vertex_data_alignment = 5;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_BAD_ARG);
  args.vertex_data.alignment = 0;
  msh.vertex_data_alignment = 0;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_BAD_ARG);
  args.vertex_data.alignment = 32;
  msh.vertex_data_alignment = 32;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);

  CHK(suvm_volume_get_primitives_count(NULL, &nprims) == RES_BAD_ARG);
  CHK(suvm_volume_get_primitives_count(vol, NULL) == RES_BAD_ARG);
  CHK(suvm_volume_get_primitives_count(vol, &nprims) == RES_OK);
  CHK(nprims == msh.ntetrahedra);

  CHK(suvm_volume_get_primitive(NULL, 0, &prim) == RES_BAD_ARG);
  CHK(suvm_volume_get_primitive(vol, nprims, &prim) == RES_BAD_ARG);
  CHK(suvm_volume_get_primitive(vol, 0, NULL) == RES_BAD_ARG);
  CHK(suvm_volume_get_primitive(vol, 0, &prim) == RES_OK);

  CHK(suvm_primitive_setup_polyhedron(NULL, &poly) == RES_BAD_ARG);
  CHK(suvm_primitive_setup_polyhedron(&prim, NULL) == RES_BAD_ARG);
  CHK(suvm_primitive_setup_polyhedron(&prim, &poly) == RES_OK);

  check_volume_polyhedra(vol, &msh);

  CHK(suvm_volume_get_mesh_desc(NULL, &desc) == RES_BAD_ARG);
  CHK(suvm_volume_get_mesh_desc(vol, NULL) == RES_BAD_ARG);
  CHK(suvm_volume_get_mesh_desc(vol, &desc) == RES_OK);

  check_volume_mesh(vol, &msh);

  CHK(suvm_volume_ref_put(vol) == RES_OK);
}

static void
check_prim
  (const struct suvm_primitive* prim,
   const struct suvm_volume* vol,
   struct mesh* msh)
{
  size_t ids[4];
  double verts[4][3];
  const size_t* data;
  const double* vert_data[4];

  /* Check primitive data */
  CHK(prim != NULL);
  CHK(vol != NULL);
  CHK(prim->volume__ == vol);
  CHK(!SUVM_PRIMITIVE_NONE(prim));
  CHK(prim->data != NULL);
  CHK(prim->data != NULL);
  CHK(prim->vertex_data[0] != NULL);
  CHK(prim->vertex_data[1] != NULL);
  CHK(prim->vertex_data[2] != NULL);
  CHK(prim->vertex_data[3] != NULL);
  CHK(prim->nvertices == 4);

  /* Fetch tetrahedron vertices */
  get_indices(prim->iprim, ids, msh);
  CHK(prim->indices[0] == ids[0]);
  CHK(prim->indices[1] == ids[1]);
  CHK(prim->indices[2] == ids[2]);
  CHK(prim->indices[3] == ids[3]);
  get_position(ids[0], verts[0], msh);
  get_position(ids[1], verts[1], msh);
  get_position(ids[2], verts[2], msh);
  get_position(ids[3], verts[3], msh);

  /* Check per primitive data */
  data = prim->data;
  CHK(data[0] == ids[0]);
  CHK(data[1] == ids[1]);
  CHK(data[2] == ids[2]);
  CHK(data[3] == ids[3]);

  /* Check per vertex data */
  vert_data[0] =  prim->vertex_data[0];
  vert_data[1] =  prim->vertex_data[1];
  vert_data[2] =  prim->vertex_data[2];
  vert_data[3] =  prim->vertex_data[3];
  CHK(d3_eq(vert_data[0], verts[0]));
  CHK(d3_eq(vert_data[1], verts[1]));
  CHK(d3_eq(vert_data[2], verts[2]));
  CHK(d3_eq(vert_data[3], verts[3]));
}

static void
check_prim_at
  (const struct suvm_primitive* prim,
   const struct suvm_volume* vol,
   const double pos[3],
   const double bcoords[4],
   struct mesh* msh)
{
  size_t ids[4];
  double verts[4][3];
  double E0[3], E1[3];
  double N[4][3];
  double dst[4];
  double p[3];
  const double* vert_data[4];

  check_prim(prim, vol, msh);

  /* Fetch tetrahedron vertices */
  get_indices(prim->iprim, ids, msh);
  get_position(ids[0], verts[0], msh);
  get_position(ids[1], verts[1], msh);
  get_position(ids[2], verts[2], msh);
  get_position(ids[3], verts[3], msh);

  /* Compute tetrahdron normals */
  d3_sub(E0, verts[1], verts[0]);
  d3_sub(E1, verts[2], verts[0]);
  d3_cross(N[0], E0, E1);
  d3_sub(E0, verts[1], verts[3]);
  d3_sub(E1, verts[0], verts[3]);
  d3_cross(N[1], E0, E1);
  d3_sub(E0, verts[2], verts[3]);
  d3_sub(E1, verts[1], verts[3]);
  d3_cross(N[2], E0, E1);
  d3_sub(E0, verts[0], verts[3]);
  d3_sub(E1, verts[2], verts[3]);
  d3_cross(N[3], E0, E1);

  /* Evaluate the distance from pos to the tetrahedron planes */
  dst[0] = d3_dot(N[0], d3_sub(p, pos, verts[0]));
  dst[1] = d3_dot(N[1], d3_sub(p, pos, verts[1]));
  dst[2] = d3_dot(N[2], d3_sub(p, pos, verts[2]));
  dst[3] = d3_dot(N[3], d3_sub(p, pos, verts[3]));

  /* Check that pos lies into the tetrahedron */
  CHK(dst[0] >= 0);
  CHK(dst[1] >= 0);
  CHK(dst[2] >= 0);
  CHK(dst[3] >= 0);

  vert_data[0] =  prim->vertex_data[0];
  vert_data[1] =  prim->vertex_data[1];
  vert_data[2] =  prim->vertex_data[2];
  vert_data[3] =  prim->vertex_data[3];

  /* Check interpolation parameter */
  p[0] =
    vert_data[0][0] * bcoords[0]
  + vert_data[1][0] * bcoords[1]
  + vert_data[2][0] * bcoords[2]
  + vert_data[3][0] * bcoords[3];
  p[1] =
    vert_data[0][1] * bcoords[0]
  + vert_data[1][1] * bcoords[1]
  + vert_data[2][1] * bcoords[2]
  + vert_data[3][1] * bcoords[3];
  p[2] =
    vert_data[0][2] * bcoords[0]
  + vert_data[1][2] * bcoords[1]
  + vert_data[2][2] * bcoords[2]
  + vert_data[3][2] * bcoords[3];
  CHK(d3_eq_eps(p, pos, 1.e-6));
}

static void
check_prims_intersect_aabb
  (struct darray_prim* primitives,
   const struct suvm_volume* vol,
   const double low[3],
   const double upp[3],
   struct mesh* msh)
{
  struct suvm_primitive* prims = NULL;
  size_t nprims;
  size_t iprim;
  size_t iprim_challenge;
  size_t ids[4];
  double vtx[4][3];
  double prim_low[3];
  double prim_upp[3];
  float lowf[3];
  float uppf[3];

  CHK(primitives);
  prims = darray_prim_data_get(primitives);
  nprims = darray_prim_size_get(primitives);

  FOR_EACH(iprim, 0, nprims) {
    check_prim(prims + iprim, vol, msh);

    /* Fetch tetrahedron vertices */
    get_indices(prims[iprim].iprim, ids, msh);
    get_position(ids[0], vtx[0], msh);
    get_position(ids[1], vtx[1], msh);
    get_position(ids[2], vtx[2], msh);
    get_position(ids[3], vtx[3], msh);

    /* Compute the primitive AABB */
    prim_low[0] = MMIN(MMIN(vtx[0][0], vtx[1][0]), MMIN(vtx[2][0], vtx[3][0]));
    prim_low[1] = MMIN(MMIN(vtx[0][1], vtx[1][1]), MMIN(vtx[2][1], vtx[3][1]));
    prim_low[2] = MMIN(MMIN(vtx[0][2], vtx[1][2]), MMIN(vtx[2][2], vtx[3][2]));
    prim_upp[0] = MMAX(MMAX(vtx[0][0], vtx[1][0]), MMAX(vtx[2][0], vtx[3][0]));
    prim_upp[1] = MMAX(MMAX(vtx[0][1], vtx[1][1]), MMAX(vtx[2][1], vtx[3][1]));
    prim_upp[2] = MMAX(MMAX(vtx[0][2], vtx[1][2]), MMAX(vtx[2][2], vtx[3][2]));

    /* Check AABB intersections */
    CHK(prim_low[0] < upp[0] && prim_upp[0] > low[0]);
    CHK(prim_low[1] < upp[1] && prim_upp[1] > low[1]);
    CHK(prim_low[2] < upp[2] && prim_upp[2] > low[2]);
  }

  /* Sort the primitives in ascending order wrt its identifier */
  qsort(prims, nprims, sizeof(*prims), cmp_prim);

  /* Exhaustively check all the mesh primitives against the AABB and ensure
   * that the input primitives are effectively the same of the ones detected by
   * this brute force procedure */
  iprim_challenge = 0;
  f3_set_d3(lowf, low);
  f3_set_d3(uppf, upp);
  FOR_EACH(iprim, 0, msh->ntetrahedra) {
    struct suvm_primitive prim;
    struct suvm_polyhedron tetra;
    enum suvm_intersection_type intersect;

    CHK(suvm_volume_get_primitive(vol, iprim, &prim) == RES_OK);
    CHK(suvm_primitive_setup_polyhedron(&prim, &tetra) == RES_OK);

    /* Check that the primitive intersects the AABB */
    intersect = suvm_polyhedron_intersect_aabb(&tetra, lowf, uppf);
    if(intersect != SUVM_INTERSECT_NONE) {
      CHK(iprim == prims[iprim_challenge].iprim);
      ++iprim_challenge;
    }
  }
  /* All submitted primitives were exhaustively challenged */
  CHK(iprim_challenge == nprims);
}

static void
check_hash
  (const struct suvm_volume* vol,
   const struct mesh* msh,
   const int has_prim_data,
   const int has_vert_data)
{
  struct suvm_mesh_desc msh_desc;
  void* mem = NULL;
  float* pos = NULL;
  uint32_t* ids = NULL;
  void* prims = NULL;
  void* verts = NULL;
  size_t sz_pos = 0;
  size_t sz_ids = 0;
  size_t sz_prims = 0;
  size_t sz_verts = 0;
  size_t i;
  hash256_T hash0, hash1;

  CHK(suvm_volume_compute_hash(NULL, 0, hash0) == RES_BAD_ARG);
  CHK(suvm_volume_compute_hash(vol, 0, NULL) == RES_BAD_ARG);
  CHK(suvm_volume_compute_hash(vol, 0, hash0) == RES_OK);

  hash_sha256(NULL, 0, hash1);
  CHK(hash256_eq(hash0, hash1));

  msh_desc = SUVM_MESH_DESC_NULL;
  CHK(suvm_mesh_desc_compute_hash(NULL, hash0) == RES_BAD_ARG);
  CHK(suvm_mesh_desc_compute_hash(&msh_desc, NULL) == RES_BAD_ARG);
  CHK(suvm_mesh_desc_compute_hash(&msh_desc, hash0) == RES_OK);

  CHK(suvm_volume_get_mesh_desc(vol, &msh_desc) == RES_OK);
  CHK(suvm_mesh_desc_compute_hash(&msh_desc, hash1) == RES_OK);
  CHK(!hash256_eq(hash0, hash1));

  /* Compute data size to hash */
  sz_pos = msh->nvertices*sizeof(float[3]);
  sz_ids = msh->ntetrahedra*sizeof(uint32_t[4]);
  if(has_prim_data) {
     sz_prims = msh->ntetrahedra*sizeof(size_t[4]);
  }
  if(has_vert_data) {
    sz_verts = msh->nvertices*sizeof(double[3]);
  }
  mem = mem_calloc(1, sz_pos + sz_ids + sz_prims + sz_verts);
  CHK(mem != NULL);

  /* Copy data to hash into the allocated memory block. Be carefull the memory
   * layout used by SUVM to hash the volume. First the vertices, then the
   * indices followed by the per prim data and finally the per vertex data */

  /* SUVM stores the vertices in single precision. Convert vertex coordinates
   * in float to ensure bit correspondance */
  pos = mem;
  FOR_EACH(i, 0, msh->nvertices) {
    pos[i*3 + 0] = (float)msh->vertices[i*3+0];
    pos[i*3 + 1] = (float)msh->vertices[i*3+1];
    pos[i*3 + 2] = (float)msh->vertices[i*3+2];
  }

  /* SUVM stores the tetrahedron indices as 32 bits unsigned integers. Convert
   * indices to ensure bit correspondance */
  ids = (uint32_t*)((char*)mem + sz_pos);
  FOR_EACH(i, 0, msh->ntetrahedra) {
    ids[i*4 + 0] = (uint32_t)msh->tetrahedra[i*4+0];
    ids[i*4 + 1] = (uint32_t)msh->tetrahedra[i*4+1];
    ids[i*4 + 2] = (uint32_t)msh->tetrahedra[i*4+2];
    ids[i*4 + 3] = (uint32_t)msh->tetrahedra[i*4+3];
  }

  /* Copy per primitive data */
  if(has_prim_data) {
    prims = ((char*)mem + sz_pos + sz_ids);
    memcpy(prims, msh->tetrahedra, msh->ntetrahedra*sizeof(size_t[4]));
  }

  /* Copy per vertex data */
  if(has_vert_data) {
    verts = ((char*)mem + sz_pos + sz_ids + sz_prims);
    memcpy(verts, msh->vertices, msh->nvertices*sizeof(double[3]));
  }

  CHK(suvm_volume_compute_hash(vol, SUVM_POSITIONS, hash0) == RES_OK);
  hash_sha256(pos, sz_pos, hash1);
  CHK(hash256_eq(hash0, hash1));

  CHK(suvm_volume_compute_hash(vol, SUVM_INDICES, hash0) == RES_OK);
  hash_sha256(ids, sz_ids, hash1);
  CHK(hash256_eq(hash0, hash1));

  CHK(suvm_volume_compute_hash(vol, SUVM_PRIMITIVE_DATA, hash0) == RES_OK);
  hash_sha256(prims, sz_prims, hash1);
  CHK(hash256_eq(hash0, hash1));

  CHK(suvm_volume_compute_hash(vol, SUVM_VERTEX_DATA, hash0) == RES_OK);
  hash_sha256(verts, sz_verts, hash1);
  CHK(hash256_eq(hash0, hash1));

  CHK(suvm_volume_compute_hash(vol, SUVM_POSITIONS|SUVM_INDICES, hash0) == RES_OK);
  hash_sha256(mem, sz_pos + sz_ids, hash1);
  CHK(hash256_eq(hash0, hash1));

  CHK(suvm_volume_compute_hash
    (vol, SUVM_POSITIONS|SUVM_INDICES|SUVM_PRIMITIVE_DATA, hash0) == RES_OK);
  hash_sha256(mem, sz_pos + sz_ids + sz_prims, hash1);
  CHK(hash256_eq(hash0, hash1));

  CHK(suvm_volume_compute_hash(vol,
     SUVM_POSITIONS|SUVM_INDICES|SUVM_PRIMITIVE_DATA|SUVM_VERTEX_DATA,
     hash0) == RES_OK);
  hash_sha256(mem, sz_pos + sz_ids + sz_prims + sz_verts, hash1);
  CHK(hash256_eq(hash0, hash1));

  mem_rm(mem);
}

static void
test_volume_hash(struct suvm_device* dev, struct mesh* msh)
{
  struct suvm_tetrahedral_mesh_args args = SUVM_TETRAHEDRAL_MESH_ARGS_NULL;
  struct suvm_volume* vol = NULL;

  args.ntetrahedra = msh->ntetrahedra;
  args.nvertices = msh->nvertices;
  args.get_indices = get_indices;
  args.get_position = get_position;
  args.context = msh;
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);

  check_hash(vol, msh, 0, 0);
  CHK(suvm_volume_ref_put(vol) == RES_OK);

  args.vertex_data.get = get_vert_data;
  args.vertex_data.size = sizeof(double[3]);
  args.vertex_data.alignment = ALIGNOF(double[3]);
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);

  check_hash(vol, msh, 0, 1);
  CHK(suvm_volume_ref_put(vol) == RES_OK);

  args.tetrahedron_data.get = get_tetra_data;
  args.tetrahedron_data.size = sizeof(size_t[4]);
  args.tetrahedron_data.alignment = ALIGNOF(size_t[4]);
  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);
  check_hash(vol, msh, 1, 1);

  CHK(suvm_volume_ref_put(vol) == RES_OK);
}

static void
prim_intersect_aabb
  (const struct suvm_primitive* prim,
   const double low[3],
   const double upp[3],
   void* context)
{
  struct darray_prim* prims = context;
  CHK(prim && low && upp && context);
  CHK(low[0] < upp[0]);
  CHK(low[1] < upp[1]);
  CHK(low[2] < upp[2]);
  CHK(darray_prim_push_back(prims, prim) == RES_OK);
}

static void
test_volume_at_cube(struct suvm_device* dev)
{
  struct darray_prim prims;
  struct mesh msh = MESH_NULL;
  struct suvm_tetrahedral_mesh_args args = SUVM_TETRAHEDRAL_MESH_ARGS_NULL;
  struct suvm_primitive prim = SUVM_PRIMITIVE_NULL;
  struct suvm_volume* vol = NULL;
  double bcoords[4];
  double pos[3];
  double tmp[3];
  double low[3], upp[3];
  const size_t nsamps = 100;
  size_t i;

  darray_prim_init(NULL, &prims);

  args.ntetrahedra = box_ntetras;
  args.nvertices = box_nverts;
  args.get_indices = get_indices;
  args.get_position = get_position;
  args.tetrahedron_data.get = get_tetra_data;
  args.tetrahedron_data.size = sizeof(size_t[4]);
  args.tetrahedron_data.alignment = ALIGNOF(size_t[4]);
  args.vertex_data.get = get_vert_data;
  args.vertex_data.size = sizeof(double[3]);
  args.vertex_data.alignment = ALIGNOF(double[3]);
  args.context = &msh;

  msh.vertices = box_vertices;
  msh.nvertices = box_nverts;
  msh.tetrahedra = box_indices;
  msh.ntetrahedra = box_ntetras;
  msh.tetrahedron_data_alignment = args.tetrahedron_data.alignment;
  msh.vertex_data_alignment = args.vertex_data.alignment;

  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);
  check_volume_mesh(vol, &msh);

  pos[0] = 0.25;
  pos[1] = 0.4;
  pos[2] = 0.4;

  CHK(suvm_volume_at(NULL, pos, &prim, bcoords) == RES_BAD_ARG);
  CHK(suvm_volume_at(vol, NULL, &prim, bcoords) == RES_BAD_ARG);
  CHK(suvm_volume_at(vol, pos, NULL, bcoords) == RES_BAD_ARG);
  CHK(suvm_volume_at(vol, pos, &prim, NULL) == RES_BAD_ARG);
  CHK(suvm_volume_at(vol, pos, &prim, bcoords) == RES_OK);
  CHK(!SUVM_PRIMITIVE_NONE(&prim));
  CHK(prim.iprim == 2);
  check_prim_at(&prim, vol, pos, bcoords, &msh);

  CHK(suvm_volume_get_aabb(NULL, low, upp) == RES_BAD_ARG);
  CHK(suvm_volume_get_aabb(vol, NULL, upp) == RES_BAD_ARG);
  CHK(suvm_volume_get_aabb(vol, low, NULL) == RES_BAD_ARG);
  CHK(suvm_volume_get_aabb(vol, low, upp) == RES_OK);
  CHK(d3_eq(low, d3_splat(tmp, 0)));
  CHK(d3_eq(upp, d3_splat(tmp, 1)));
  FOR_EACH(i, 0, nsamps) {
    double samp[3];
    samp[0] = low[0] + rand_canonic()*(upp[0] - low[0]);
    samp[1] = low[1] + rand_canonic()*(upp[1] - low[1]);
    samp[2] = low[2] + rand_canonic()*(upp[2] - low[2]);
    CHK(suvm_volume_at(vol, samp, &prim, bcoords) == RES_OK);
    check_prim_at(&prim, vol, samp, bcoords, &msh);
  }

  pos[0] = upp[0] + 0.1;
  pos[1] = upp[1] + 0.1;
  pos[2] = upp[2] + 0.1;
  CHK(suvm_volume_at(vol, pos, &prim, bcoords) == RES_OK);
  CHK(SUVM_PRIMITIVE_NONE(&prim));

  d3_splat(low, 0.25);
  d3_splat(upp, 0.75);

  CHK(suvm_volume_intersect_aabb
    (NULL, low, upp, prim_intersect_aabb, &prims) == RES_BAD_ARG);
  CHK(suvm_volume_intersect_aabb
    (vol, NULL, upp, prim_intersect_aabb, &prims) == RES_BAD_ARG);
  CHK(suvm_volume_intersect_aabb
    (vol, low, NULL, prim_intersect_aabb, &prims) == RES_BAD_ARG);
  CHK(suvm_volume_intersect_aabb
    (vol, low, upp, NULL, &prims) == RES_BAD_ARG);
  CHK(suvm_volume_intersect_aabb
    (vol, low, upp, prim_intersect_aabb, &prims) == RES_OK);

  CHK(darray_prim_size_get(&prims) == box_ntetras);
  check_prims_intersect_aabb(&prims, vol, low, upp, &msh);

  CHK(suvm_volume_intersect_aabb
    (vol, upp, low, prim_intersect_aabb, &prims) == RES_BAD_ARG);
  CHK(suvm_volume_intersect_aabb
    (vol, low, low, prim_intersect_aabb, &prims) == RES_BAD_ARG);

  darray_prim_clear(&prims);
  d3(low,-1, 0, 0);
  d3(upp, 0, 1, 1);
  CHK(suvm_volume_intersect_aabb
    (vol, low, upp, prim_intersect_aabb, &prims) == RES_OK);
  CHK(darray_prim_size_get(&prims) == 0);

  d3(low, 1, 0, 0);
  d3(upp, 2, 1, 1);
  CHK(suvm_volume_intersect_aabb
    (vol, low, upp, prim_intersect_aabb, &prims) == RES_OK);
  CHK(darray_prim_size_get(&prims) == 0);

  d3(low, 0.9, 0, 0);
  d3(upp, 2, 1, 1);
  CHK(suvm_volume_intersect_aabb
    (vol, low, upp, prim_intersect_aabb, &prims) == RES_OK);
  CHK(darray_prim_size_get(&prims) == 10);
  check_prims_intersect_aabb(&prims, vol, low, upp, &msh);

  CHK(suvm_volume_ref_put(vol) == RES_OK);
  darray_prim_release(&prims);
}

static void
test_volume_hash_cube(struct suvm_device* dev)
{
  struct mesh msh = MESH_NULL;

  msh.vertices = box_vertices;
  msh.nvertices = box_nverts;
  msh.tetrahedra = box_indices;
  msh.ntetrahedra = box_ntetras;
  msh.tetrahedron_data_alignment = ALIGNOF(size_t[4]);
  msh.vertex_data_alignment = ALIGNOF(double[3]);
  test_volume_hash(dev, &msh);
}

static void
test_volume_at_ball(struct suvm_device* dev)
{
  struct darray_prim prims;
  struct mesh msh = MESH_NULL;
  struct suvm_tetrahedral_mesh_args args = SUVM_TETRAHEDRAL_MESH_ARGS_NULL;
  struct suvm_primitive prim = SUVM_PRIMITIVE_NULL;
  struct suvm_volume* vol = NULL;
  double bcoords[4];
  double aabb_low[3], aabb_upp[3], aabb_sz;
  double low[3], upp[3];
  double vec[3];
  size_t ivert;
  const size_t nsamps = 10000;
  const size_t naabbs = 100;
  size_t i;
  int check_at = 0;
  int check_intersect_aabb = 0;

  darray_prim_init(NULL, &prims);

  args.precompute_normals = 1;
  args.ntetrahedra = ball_ntetrahedra;
  args.nvertices = ball_nvertices;
  args.get_indices = get_indices;
  args.get_position = get_position;
  args.tetrahedron_data.get = get_tetra_data;
  args.tetrahedron_data.size = sizeof(size_t[4]);
  args.tetrahedron_data.alignment = ALIGNOF(size_t[4]);
  args.vertex_data.get = get_vert_data;
  args.vertex_data.size = sizeof(double[3]);
  args.vertex_data.alignment = ALIGNOF(double[3]);
  args.context = &msh;

  msh.vertices = ball_vertices;
  msh.nvertices = ball_nvertices;
  msh.tetrahedra = ball_tetrahedra;
  msh.ntetrahedra = ball_ntetrahedra;
  msh.tetrahedron_data_alignment = args.tetrahedron_data.alignment;
  msh.vertex_data_alignment = args.vertex_data.alignment;

  /* Compute the ball aabb */
  d3_splat(aabb_low, DBL_MAX);
  d3_splat(aabb_upp,-DBL_MAX);
  FOR_EACH(ivert, 0, ball_nvertices) {
    aabb_low[0] = MMIN(aabb_low[0], ball_vertices[ivert*3+0]);
    aabb_low[1] = MMIN(aabb_low[1], ball_vertices[ivert*3+1]);
    aabb_low[2] = MMIN(aabb_low[2], ball_vertices[ivert*3+2]);
    aabb_upp[0] = MMAX(aabb_upp[0], ball_vertices[ivert*3+0]);
    aabb_upp[1] = MMAX(aabb_upp[1], ball_vertices[ivert*3+1]);
    aabb_upp[2] = MMAX(aabb_upp[2], ball_vertices[ivert*3+2]);
  }
  aabb_sz = d3_len(d3_sub(vec, aabb_upp, aabb_low));

  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);
  check_volume_mesh(vol, &msh);

  check_volume_polyhedra(vol, &msh);

  /* Check volume AABB */
  CHK(suvm_volume_get_aabb(vol, low, upp) == RES_OK);
  CHK(d3_eq_eps(aabb_low, low, aabb_sz*1.e-6));
  CHK(d3_eq_eps(aabb_upp, upp, aabb_sz*1.e-6));

  /* Check at operator */
  check_at = 0;
  FOR_EACH(i, 0, nsamps) {
    double samp[3];
    samp[0] = low[0] + rand_canonic()*(upp[0] - low[0]);
    samp[1] = low[1] + rand_canonic()*(upp[1] - low[1]);
    samp[2] = low[2] + rand_canonic()*(upp[2] - low[2]);
    CHK(suvm_volume_at(vol, samp, &prim, bcoords) == RES_OK);

    if(!SUVM_PRIMITIVE_NONE(&prim)) {
      check_at += 1;
      check_prim_at(&prim, vol, samp, bcoords, &msh);
    }
  }
  CHK(check_at != 0);

  /* Check intersect AABB */
  check_intersect_aabb = 0;
  FOR_EACH(i, 0, naabbs) {
    double pt0[3];
    double pt1[3];

    /* Sample 2 random points into the mesh AABB */
    pt0[0] = low[0] + rand_canonic()*(upp[0] - low[0]);
    pt0[1] = low[1] + rand_canonic()*(upp[1] - low[1]);
    pt0[2] = low[2] + rand_canonic()*(upp[2] - low[2]);
    pt1[0] = low[0] + rand_canonic()*(upp[0] - low[0]);
    pt1[1] = low[1] + rand_canonic()*(upp[1] - low[1]);
    pt1[2] = low[2] + rand_canonic()*(upp[2] - low[2]);

    /* Build the bounding box of the sampled point */
    aabb_low[0] = MMIN(pt0[0], pt1[0]);
    aabb_low[1] = MMIN(pt0[1], pt1[1]);
    aabb_low[2] = MMIN(pt0[2], pt1[2]);
    aabb_upp[0] = MMAX(pt0[0], pt1[0]);
    aabb_upp[1] = MMAX(pt0[1], pt1[1]);
    aabb_upp[2] = MMAX(pt0[2], pt1[2]);

    CHK(suvm_volume_intersect_aabb
      (vol, aabb_low, aabb_upp, prim_intersect_aabb, &prims) == RES_OK);

    check_intersect_aabb = darray_prim_size_get(&prims) != 0;
    check_prims_intersect_aabb(&prims, vol, aabb_low, aabb_upp, &msh);
    darray_prim_clear(&prims);
  }
  CHK(check_intersect_aabb != 0);

  CHK(suvm_volume_ref_put(vol) == RES_OK);
  darray_prim_release(&prims);
}

static void
test_volume_hash_ball(struct suvm_device* dev)
{
  struct mesh msh = MESH_NULL;

  msh.vertices = ball_vertices;
  msh.nvertices = ball_nvertices;
  msh.tetrahedra = ball_tetrahedra;
  msh.ntetrahedra = ball_ntetrahedra;
  msh.tetrahedron_data_alignment = ALIGNOF(size_t[4]);
  msh.vertex_data_alignment = ALIGNOF(double[3]);
  test_volume_hash(dev, &msh);
}

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct suvm_device* dev = NULL;
  (void)argc, (void)argv;

  CHK(suvm_device_create(NULL, &mem_default_allocator, 1, &dev) == RES_OK);

  test_tetrahedral_mesh_creation(dev);
  test_volume_at_cube(dev);
  test_volume_at_ball(dev);
  test_volume_hash_cube(dev);
  test_volume_hash_ball(dev);

  CHK(suvm_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&mem_default_allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

