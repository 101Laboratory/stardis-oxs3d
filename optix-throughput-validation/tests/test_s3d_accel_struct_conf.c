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

#include "s3d.h"
#include "test_s3d_utils.h"

#include <rsys/clock_time.h>
#include <rsys_math.h>
#include <string.h>

/*******************************************************************************
 * Mesh functions and data structure
 ******************************************************************************/
struct mesh {
  double* pos;
  size_t* ids;
  size_t nverts;
  size_t ntris;
  struct mem_allocator* allocator;
};

static void
mesh_init_sphere
  (struct mesh* sphere,
   struct mem_allocator* allocator,
   const size_t nthetas)
{
  const size_t nphis = (size_t)(((double)nthetas + 0.5) * 0.5);
  const double step_theta = 2*PI / (double)nthetas;
  const double step_phi = PI / (double)nphis;
  size_t itheta, iphi;
  size_t i;

  CHK(sphere && allocator && nthetas);
  memset(sphere, 0, sizeof(*sphere));

  sphere->nverts = nthetas * (nphis-1)/*#contour verts*/ + 2 /*polar verts*/;
  sphere->ntris = 2*nthetas * (nphis-2)/*#contour tris*/ + 2*nthetas/*#polar tris*/;
  sphere->allocator = allocator;

  sphere->pos = MEM_CALLOC(allocator, sphere->nverts, sizeof(double[3]));
  CHK(sphere->pos);
  sphere->ids = MEM_CALLOC(allocator, sphere->ntris, sizeof(size_t[3]));
  CHK(sphere->ids);

  /* Build the contour vertices */
  i = 0;
  FOR_EACH(itheta, 0, nthetas) {
    const double theta = -PI + (double)itheta * step_theta;
    const double cos_theta = cos(theta);
    const double sin_theta = sin(theta);
    FOR_EACH(iphi, 0, nphis-1) {
      const double phi = -PI*0.5 + (double)(iphi + 1) * step_phi;
      const double cos_phi = cos(phi);
      const double sin_phi = sin(phi);
      sphere->pos[i++] = cos_phi * cos_theta;
      sphere->pos[i++] = cos_phi * sin_theta;
      sphere->pos[i++] = sin_phi;
    }
  }
  /* polar vertices */
  sphere->pos[i++] = 0.0; sphere->pos[i++] = 0.0; sphere->pos[i++] =-1.0;
  sphere->pos[i++] = 0.0; sphere->pos[i++] = 0.0; sphere->pos[i++] = 1.0;
  CHK(i == sphere->nverts*3);

  /* Define the indices of the contour primitives */
  i = 0;
  FOR_EACH(itheta, 0, nthetas) {
    const size_t itheta0 = itheta * (nphis - 1);
    const size_t itheta1 = ((itheta + 1) % nthetas) * (nphis - 1);
    FOR_EACH(iphi, 0,  nphis-2) {
      const size_t iphi0 = iphi + 0;
      const size_t iphi1 = iphi + 1;
      sphere->ids[i++] = itheta0 + iphi0; /* First triangle */
      sphere->ids[i++] = itheta0 + iphi1;
      sphere->ids[i++] = itheta1 + iphi0;
      sphere->ids[i++] = itheta1 + iphi0; /* Second triangle */
      sphere->ids[i++] = itheta0 + iphi1;
      sphere->ids[i++] = itheta1 + iphi1;
    }
  }
  /* Define the indices of the polar primitives */
  FOR_EACH(itheta, 0, nthetas) {
    const size_t itheta0 = itheta * (nphis - 1);
    const size_t itheta1 = ((itheta + 1) % nthetas) * (nphis - 1);
    sphere->ids[i++] = nthetas * (nphis - 1);
    sphere->ids[i++] = itheta0;
    sphere->ids[i++] = itheta1;
    sphere->ids[i++] = nthetas * (nphis - 1) + 1;
    sphere->ids[i++] = itheta1 + (nphis - 2);
    sphere->ids[i++] = itheta0 + (nphis - 2);
  }
  CHK(i == sphere->ntris*3);
}

static void
mesh_release(struct mesh* mesh)
{
  CHK(mesh);
  MEM_RM(mesh->allocator, mesh->pos);
  MEM_RM(mesh->allocator, mesh->ids);
}

static INLINE void
mesh_dump(const struct mesh* mesh, FILE* stream)
{
  size_t i;
  CHK(mesh && stream);
  FOR_EACH(i, 0, mesh->nverts) {
    fprintf(stream, "v %g %g %g\n",
      mesh->pos[i*3+0],
      mesh->pos[i*3+1],
      mesh->pos[i*3+2]);
  }
  FOR_EACH(i, 0, mesh->ntris) {
    fprintf(stream, "f %lu %lu %lu\n",
      (unsigned long)mesh->ids[i*3+0]+1,
      (unsigned long)mesh->ids[i*3+1]+1,
      (unsigned long)mesh->ids[i*3+2]+1);
  }
}

static void
mesh_get_pos(const unsigned ivert, float pos[3], void* ctx)
{
  const struct mesh* mesh = ctx;
  CHK(pos && ctx && ivert < mesh->nverts);
  pos[0] = (float)mesh->pos[ivert*3+0];
  pos[1] = (float)mesh->pos[ivert*3+1];
  pos[2] = (float)mesh->pos[ivert*3+2];
}

static void
mesh_get_tri(const unsigned itri, unsigned ids[3], void* ctx)
{
  const struct mesh* mesh = ctx;
  CHK(ids && ctx && itri < mesh->ntris);
  ids[0] = (unsigned)mesh->ids[itri*3+0];
  ids[1] = (unsigned)mesh->ids[itri*3+1];
  ids[2] = (unsigned)mesh->ids[itri*3+2];
}

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
time_scene_view_creation
  (struct s3d_scene* scn,
   const struct s3d_accel_struct_conf* cfg,
   const char* string)
{
  char dump[128];
  struct time t0, t1;
  struct s3d_scene_view* view;
  CHK(scn);

  time_current(&t0);
  CHK(s3d_scene_view_create2(scn, S3D_TRACE, cfg, &view) == RES_OK);
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_ALL, NULL, dump, sizeof(dump));
  printf("%s: %s\n", string, dump);
  CHK(s3d_scene_view_ref_put(view) == RES_OK);
}

/*******************************************************************************
 * Main test function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct mesh sphere;
  struct s3d_device* dev;
  struct s3d_shape* shape;
  struct s3d_scene* scn;
  struct s3d_scene_view* view;
  struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
  struct s3d_accel_struct_conf cfg = S3D_ACCEL_STRUCT_CONF_DEFAULT;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  mesh_init_sphere(&sphere, &allocator, 256);
  /*mesh_dump(&sphere, stdout);*/

  CHK(s3d_device_create(NULL, &allocator, 1, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_shape_create_mesh(dev, &shape) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);

  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = mesh_get_pos;
  CHK(s3d_mesh_setup_indexed_vertices(shape, (unsigned)sphere.ntris, mesh_get_tri,
    (unsigned)sphere.nverts, &vdata, 1, &sphere) == RES_OK);

  CHK(s3d_scene_view_create2(NULL, S3D_TRACE, NULL, &view) == RES_BAD_ARG);
  CHK(s3d_scene_view_create2(scn, S3D_TRACE, NULL, NULL) == RES_BAD_ARG);

  time_scene_view_creation(scn, NULL, "All default");

  cfg.quality = S3D_ACCEL_STRUCT_QUALITY_LOW;
  cfg.mask = S3D_ACCEL_STRUCT_FLAG_ROBUST | S3D_ACCEL_STRUCT_FLAG_DYNAMIC;
  time_scene_view_creation(scn, &cfg, "Low quality, robust & dynamic");

  cfg.quality = S3D_ACCEL_STRUCT_QUALITY_MEDIUM;
  cfg.mask = S3D_ACCEL_STRUCT_FLAG_COMPACT;
  time_scene_view_creation(scn, &cfg, "Medium quality, compact");

  cfg.quality = S3D_ACCEL_STRUCT_QUALITY_HIGH;
  cfg.mask = S3D_ACCEL_STRUCT_FLAG_ROBUST | S3D_ACCEL_STRUCT_FLAG_COMPACT;
  time_scene_view_creation(scn, &cfg, "High quality, compact & robust");

  CHK(s3d_shape_ref_put(shape) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_device_ref_put(dev) == RES_OK);

  mesh_release(&sphere);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

