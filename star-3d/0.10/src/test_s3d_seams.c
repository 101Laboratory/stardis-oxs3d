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

#include <rsys/mem_allocator.h>
#include <rsys/float33.h>
#include <rsys/double33.h>

struct desc {
  const float* vertices;
  const unsigned* indices;
};

/*******************************************************************************
 * Callbacks
 ******************************************************************************/
static INLINE void
get_ids(const unsigned itri, unsigned ids[3], void* data)
{
  const unsigned id = itri * 3;
  struct desc* desc = data;
  CHK(desc != NULL);
  CHK(ids != NULL);
  ids[0] = desc->indices[id + 0];
  ids[1] = desc->indices[id + 1];
  ids[2] = desc->indices[id + 2];
}

static INLINE void
get_position(const unsigned ivert, float position[3], void* data)
{
  struct desc* desc = data;
  CHK(desc != NULL);
  CHK(position != NULL);
  position[0] = desc->vertices[ivert * 3 + 0];
  position[1] = desc->vertices[ivert * 3 + 1];
  position[2] = desc->vertices[ivert * 3 + 2];
}

static INLINE void
get_normal(const unsigned ivert, float normal[3], void* data)
{
  (void) ivert, (void) data;
  CHK(normal != NULL);
  normal[0] = 1.f;
  normal[1] = 0.f;
  normal[2] = 0.f;
}

static INLINE void
get_uv(const unsigned ivert, float uv[2], void* data)
{
  (void) ivert, (void) data;
  CHK(uv != NULL);
  uv[0] = -1.f;
  uv[1] = 1.f;
}

static INLINE void
get_polygon_vertices(const size_t ivert, double position[2], void* ctx)
{
  const double* verts = ctx;
  CHK(position != NULL);
  CHK(ctx != NULL);
  position[0] = verts[ivert * 2 + 0];
  position[1] = verts[ivert * 2 + 1];
}

static const float SQUARE_EDGES__ [] = {
  -0.1f, -0.1f, 0.f,
   0.1f, -0.1f, 0.f,
   0.1f,  0.1f, 0.f,
  -0.1f,  0.1f, 0.f
};
static const unsigned SQUARE_NVERTS__ = sizeof(SQUARE_EDGES__) / (sizeof(float)*3);
static const unsigned SQUARE_TRG_IDS__ [] = { 0, 2, 1, 2, 0, 3 };
static const unsigned SQUARE_NTRIS__ = sizeof(SQUARE_TRG_IDS__) / (sizeof(unsigned)*3);
static const struct desc SQUARE_DESC__ = { SQUARE_EDGES__, SQUARE_TRG_IDS__ };

static int
check_ray(int use_double)
{
  struct mem_allocator allocator;
  struct s3d_device* dev;
  struct s3d_hit hit;
  struct s3d_scene* scn;
  struct s3d_scene* scn2;
  struct s3d_scene_view* scnview;
  struct s3d_shape* square;
  struct s3d_shape* inst;
  struct s3d_vertex_data attribs;
  float transformf[12];
  double transform[12];
  float range[2] = { 0.f, FLT_MAX };
  float org[3] = {
    3.3492994308471680f, -9.7470426559448242f, 2.6555661803570274f
  };
  float dir[3] = {
    -0.26465030351986046f, 0.77017831656345948f, 0.58033229924097962f
  };
  float pos[3];

  if(use_double) {
    d33_rotation_pitch(transform, PI);
    f33_set_d33(transformf, transform);
  } else {
    f33_rotation_pitch(transformf, (float)PI);
  }
  f3_splat(transformf + 9, 0);
  transformf[11] = 10;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s3d_device_create(NULL, &allocator, 1, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_create(dev, &scn2) == RES_OK);

  attribs.usage = S3D_POSITION;
  attribs.type = S3D_FLOAT3;
  attribs.get = get_position;

  CHK(s3d_shape_create_mesh(dev, &square) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(square, SQUARE_NTRIS__, get_ids,
    SQUARE_NVERTS__, &attribs, 1, (void*) &SQUARE_DESC__) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, square) == RES_OK);
  s3d_scene_instantiate(scn, &inst);
  CHK(s3d_instance_set_transform(inst, transformf) == RES_OK);
  CHK(s3d_scene_attach_shape(scn2, inst) == RES_OK);

  CHK(s3d_scene_view_create(scn2, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  printf("\nRaytrace using %s: ", use_double ? "double" : "float");
  if(!S3D_HIT_NONE(&hit)) {
    f3_add(pos, org, f3_mulf(pos, dir, hit.distance));
    printf("Hit at [%g %g %g]\n",SPLIT3(pos));
  } else {
    printf("No hit\n");
  }
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_ref_put(scn2) == RES_OK);
  CHK(s3d_device_ref_put(dev) == RES_OK);
  CHK(s3d_shape_ref_put(square) == RES_OK);
  CHK(s3d_shape_ref_put(inst) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return S3D_HIT_NONE(&hit) ? RES_UNKNOWN_ERR : RES_OK;
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  CHK(check_ray(1) == RES_OK);
  CHK(check_ray(0) == RES_OK);
  return 0;
}

