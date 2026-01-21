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
#include "test_suvm_ball.h"
#include "test_suvm_box.h"
#include "test_suvm_utils.h"

#include <rsys/float3.h>
#include <string.h>

struct mesh {
  const double* vertices; /* List of double[3] */
  size_t nvertices;
  const size_t* tetrahedra; /* List of size_t[4] */
  size_t ntetrahedra;
};
static const struct mesh MESH_NULL;

/*******************************************************************************
 * Helper functions
 *************v*****************************************************************/
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

static void
write_voxels
  (FILE* stream,
   const int* vxls,
   const size_t def[3],
   const double vxl_sz[3])
{
  size_t ix, iy, iz;
  size_t ivxl;
  size_t i;
  CHK(stream && vxls && def && def[0] && def[1] && def[2]);

  fprintf(stream, "# vtk DataFile Version 2.0\n");
  fprintf(stream, "nothing\n");
  fprintf(stream, "ASCII\n");

  fprintf(stream, "DATASET RECTILINEAR_GRID\n");
  fprintf(stream, "DIMENSIONS %lu %lu %lu\n", def[0]+1, def[1]+1, def[2]+1);
  fprintf(stream, "X_COORDINATES %lu float\n", def[0]+1);
  FOR_EACH(i, 0, def[0]+1) fprintf(stream, "%g ", (double)i*vxl_sz[0]);
  fprintf(stream, "\n");
  fprintf(stream, "Y_COORDINATES %lu float\n", def[1]+1);
  FOR_EACH(i, 0, def[1]+1) fprintf(stream, "%g ", (double)i*vxl_sz[1]);
  fprintf(stream, "\n");
  fprintf(stream, "Z_COORDINATES %lu float\n", def[2]+1);
  FOR_EACH(i, 0, def[2]+1) fprintf(stream, "%g ", (double)i*vxl_sz[2]);
  fprintf(stream, "\n");

  fprintf(stream, "CELL_DATA %lu\n", def[0]*def[1]*def[2]);
  fprintf(stream, "SCALARS intersect_type int 1\n");
  fprintf(stream, "LOOKUP_TABLE default\n");

  ivxl = 0;
  FOR_EACH(iz, 0, def[2]) {
    FOR_EACH(iy, 0, def[1]) {
      FOR_EACH(ix, 0, def[0]) {
        fprintf(stream, "%i\n", vxls[ivxl]);
        ++ivxl;
      }
    }
  }
}

static void
voxelise_volume
  (const struct suvm_volume* vol,
   int* vxls,
   const size_t def[3])
{
  double low[3];
  double upp[3];
  double vxl_sz[3];
  size_t iprim;
  size_t nprims;

  CHK(suvm_volume_get_aabb(vol, low, upp) == RES_OK);
  CHK(suvm_volume_get_primitives_count(vol, &nprims) == RES_OK);

  memset(vxls, 0, sizeof(*vxls)*def[0]*def[1]*def[2]);

  vxl_sz[0] = (upp[0] - low[0]) / (double)def[0];
  vxl_sz[1] = (upp[1] - low[1]) / (double)def[1];
  vxl_sz[2] = (upp[2] - low[2]) / (double)def[2];

  FOR_EACH(iprim, 0, nprims) {
    struct suvm_primitive prim = SUVM_PRIMITIVE_NULL;
    struct suvm_polyhedron poly;
    size_t ivxl_low[3];
    size_t ivxl_upp[3];
    size_t ivxl[3];
    size_t i = 0;

    CHK(suvm_volume_get_primitive(vol, iprim, &prim) == RES_OK);
    CHK(suvm_primitive_setup_polyhedron(&prim, &poly) == RES_OK);

    /* Transform the polyhedron AABB in voxel space */
    ivxl_low[0] = (size_t)((poly.lower[0] - low[0]) / vxl_sz[0]);
    ivxl_low[1] = (size_t)((poly.lower[1] - low[1]) / vxl_sz[1]);
    ivxl_low[2] = (size_t)((poly.lower[2] - low[2]) / vxl_sz[2]);
    ivxl_upp[0] = (size_t)ceil((poly.upper[0] - low[0]) / vxl_sz[0]);
    ivxl_upp[1] = (size_t)ceil((poly.upper[1] - low[1]) / vxl_sz[1]);
    ivxl_upp[2] = (size_t)ceil((poly.upper[2] - low[2]) / vxl_sz[2]);
    CHK(ivxl_low[0] < def[0] && ivxl_upp[0] <= def[0]);
    CHK(ivxl_low[1] < def[1] && ivxl_upp[1] <= def[1]);
    CHK(ivxl_low[2] < def[2] && ivxl_upp[2] <= def[2]);

    FOR_EACH(ivxl[2], ivxl_low[2], ivxl_upp[2]) {
      float vxl_low[3];
      float vxl_upp[3];
      vxl_low[2] = (float)((double)ivxl[2] * vxl_sz[2] + low[2]);
      vxl_upp[2] = vxl_low[2] + (float)vxl_sz[2];
      FOR_EACH(ivxl[1], ivxl_low[1], ivxl_upp[1]) {
        vxl_low[1] = (float)((double)ivxl[1] * vxl_sz[1] + low[1]);
        vxl_upp[1] = vxl_low[1] + (float)vxl_sz[1];
        FOR_EACH(ivxl[0], ivxl_low[0], ivxl_upp[0]) {
          vxl_low[0] = (float)((double)ivxl[0] * vxl_sz[0] + low[0]);
          vxl_upp[0] = vxl_low[0] + (float)vxl_sz[0];

          i = ivxl[0] + ivxl[1]*def[0] + ivxl[2]*def[0]*def[1];
          vxls[i] += (int)suvm_polyhedron_intersect_aabb(&poly, vxl_low, vxl_upp);
        }
      }
    }
  }
}

static void
test_mesh_voxelization
  (struct suvm_device* dev,
   struct mem_allocator* allocator,
   struct mesh* msh,
   const size_t def[3],
   const char* filename) /* NULL <=> do not write the result onto disk */
{
  struct suvm_volume* vol = NULL;
  struct suvm_tetrahedral_mesh_args args = SUVM_TETRAHEDRAL_MESH_ARGS_NULL;
  double low[3], upp[3];
  double vxl_sz[3];
  int* vxls = NULL;

  args.ntetrahedra = msh->ntetrahedra;
  args.nvertices = msh->nvertices;
  args.get_indices = get_indices;
  args.get_position = get_position;
  args.context = msh;

  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);
  CHK(suvm_volume_get_aabb(vol, low, upp) == RES_OK);
  vxl_sz[0] = (upp[0] - low[0]) / (double)def[0];
  vxl_sz[1] = (upp[1] - low[1]) / (double)def[1];
  vxl_sz[2] = (upp[2] - low[2]) / (double)def[2];

  CHK(allocator != NULL);
  vxls = MEM_CALLOC(allocator, def[0]*def[1]*def[2], sizeof(*vxls));
  CHK(vxls != NULL);

  voxelise_volume(vol, vxls, def);

  if(filename) {
    FILE* fp = fopen(filename, "w");
    CHK(fp != NULL);
    write_voxels(fp, vxls, def, vxl_sz);
    CHK(fclose(fp) == 0);
  }

  MEM_RM(allocator, vxls);
  CHK(suvm_volume_ref_put(vol) == RES_OK);
}


static void
test_tetra_aabb_intersection
  (struct suvm_device* dev,
   struct mem_allocator* allocator)
{
  const double vertices[] = {
    0.0, 0.0, 0.0,
    0.0, 0.0, 1.0,
    1.0, 0.0, 1.0,
    0.0, 1.0, 1.0
  };
  const size_t tetra[] = { 0, 1, 2, 3 };
  struct mesh msh = MESH_NULL;
  struct suvm_tetrahedral_mesh_args args = SUVM_TETRAHEDRAL_MESH_ARGS_NULL;
  struct suvm_polyhedron poly;
  struct suvm_primitive prim = SUVM_PRIMITIVE_NULL;
  struct suvm_volume* vol= NULL;
  float low[3];
  float upp[3];
  double vxl_sz[3];
  const size_t def[3] = {16, 16, 16};
  size_t nprims;
  int* vxls = NULL;
  (void)vxl_sz;

  msh.vertices = vertices;
  msh.nvertices = 4;
  msh.tetrahedra = tetra;
  msh.ntetrahedra = 1;

  args.ntetrahedra = 1;
  args.nvertices = 4;
  args.get_indices = get_indices;
  args.get_position = get_position;
  args.context = &msh;

  CHK(suvm_tetrahedral_mesh_create(dev, &args, &vol) == RES_OK);

  CHK(suvm_volume_get_primitives_count(vol, &nprims) == RES_OK);
  CHK(nprims == 1);

  CHK(suvm_volume_get_primitive(vol, 0, &prim) == RES_OK);
  CHK(suvm_primitive_setup_polyhedron(&prim, &poly) == RES_OK);

  f3(low, 0.f, 0.f, 0.f);
  f3(upp, 1.f, 1.f, 1.f);
  CHK(suvm_polyhedron_intersect_aabb(&poly, low, upp) == SUVM_INTERSECT_IS_INCLUDED);

  f3(low, 0.f, 0.f, 0.5f);
  f3(upp, 0.5f, 0.5f, 1.f);
  CHK(suvm_polyhedron_intersect_aabb(&poly, low, upp) == SUVM_INTERSECT_PARTIAL);

  f3(low, 0.f, 0.f, 0.66667f /* ~1-1/3 */);
  f3(upp, 0.33332f/*~1/3*/, 0.33332f/*~1-1/3*/, 1.f);
  CHK(suvm_polyhedron_intersect_aabb(&poly, low, upp) == SUVM_INTERSECT_INCLUDE);

  f3(low, 0.33334f/*~1.3*/, 0.33334f/* ~1/3 */, 0);
  f3(upp, 1.f, 1.f, 0.66665f /*~ 1-1/3 */);
  CHK(suvm_polyhedron_intersect_aabb(&poly, low, upp) == SUVM_INTERSECT_NONE);

  CHK(allocator != NULL);
  vxls = MEM_CALLOC(allocator, def[0]*def[1]*def[2], sizeof(*vxls));
  CHK(vxls != NULL);

  voxelise_volume(vol, vxls, def);

  MEM_RM(allocator, vxls);
  CHK(suvm_volume_ref_put(vol) == RES_OK);
}

/*******************************************************************************
 * Main function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mesh msh = MESH_NULL;
  struct suvm_device* dev = NULL;
  size_t def[3] = {64, 64, 64};
  (void)argc, (void)argv;

  CHK(suvm_device_create(NULL, &mem_default_allocator, 1, &dev) == RES_OK);

  test_tetra_aabb_intersection(dev, &mem_default_allocator);

  msh.vertices = box_vertices;
  msh.nvertices = box_nverts;
  msh.tetrahedra = box_indices;
  msh.ntetrahedra = box_ntetras;
  test_mesh_voxelization(dev, &mem_default_allocator, &msh, def, "box.vtk");

  msh.vertices = ball_vertices;
  msh.nvertices = ball_nvertices;
  msh.tetrahedra = ball_tetrahedra;
  msh.ntetrahedra = ball_ntetrahedra;
  test_mesh_voxelization(dev, &mem_default_allocator, &msh, def, "ball.vtk");

  CHK(suvm_device_ref_put(dev) == RES_OK);
  check_memory_allocator(&mem_default_allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
