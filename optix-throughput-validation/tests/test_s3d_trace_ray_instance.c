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
#include "test_s3d_camera.h"
#include "test_s3d_cbox.h"
#include "test_s3d_utils.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/float33.h>
#include <rsys/image.h>

static const float quad_verts[] = {
  -1.f, -1.f, 0.f,
  -1.f,  1.f, 0.f,
   1.f,  1.f, 0.f,
   1.f, -1.f, 0.f
};
static const unsigned quad_nverts = sizeof(quad_verts)/(sizeof(float)*3);
static const unsigned quad_ids[] = { 0, 1, 3, 3, 1, 2 };
static const unsigned quad_ntris = sizeof(quad_ids)/(sizeof(unsigned)*3);

struct ray {
  float org[3];
  float dir[3];
};

static int
filter
  (const struct s3d_hit* hit,
   const float ray_org[3],
   const float ray_dir[3],
   const float ray_range[2],
   void* ray_data,
   void* filter_data)
{
  struct ray* ray = ray_data;

  CHK(hit != NULL);
  CHK(ray_org != NULL);
  CHK(ray_dir != NULL);
  CHK(ray_data != NULL);
  CHK(ray_range != NULL);
  CHK(filter_data == NULL);
  CHK(f3_eq(ray_org, ray->org) == 1);
  CHK(f3_eq(ray_dir, ray->dir) == 1);
  return 0;
}

static void
quad_get_ids(const unsigned itri, unsigned ids[3], void* data)
{
  const unsigned id = itri * 3;
  CHK(ids != NULL);
  CHK(itri < quad_ntris);
  (void)data;
  ids[0] = quad_ids[id + 0];
  ids[1] = quad_ids[id + 1];
  ids[2] = quad_ids[id + 2];
}

static void
quad_get_pos(const unsigned ivert, float pos[3], void* data)
{
  const unsigned i = ivert*3;
  CHK(pos != NULL);
  CHK(ivert < quad_nverts);
  (void)data;
  pos[0] = quad_verts[i+0];
  pos[1] = quad_verts[i+1];
  pos[2] = quad_verts[i+2];
}

static void
test_quad(struct s3d_device* dev)
{
  struct ray ray;
  struct s3d_attrib attr;
  struct s3d_hit hit[2];
  struct s3d_scene* scn;
  struct s3d_scene_view* view[2];
  struct s3d_shape* quad;
  struct s3d_shape* quad_inst;
  struct s3d_vertex_data vdata;
  unsigned quad_id;
  unsigned quad_inst_id;
  float transform[12];
  float dir[3];
  float range[2];

  f33_rotation_pitch(transform, (float)PI);
  f3_splat(transform+9, 0);

  vdata.type = S3D_FLOAT3;
  vdata.usage = S3D_POSITION;
  vdata.get = quad_get_pos;
  CHK(s3d_shape_create_mesh(dev, &quad) == RES_OK);
  CHK(s3d_mesh_set_hit_filter_function(quad, filter, NULL) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (quad, quad_ntris, quad_get_ids, quad_nverts, &vdata, 1, NULL) == RES_OK);

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, quad) == RES_OK);
  CHK(s3d_scene_instantiate(scn, &quad_inst) == RES_OK);
  CHK(s3d_instance_set_transform(quad_inst, transform) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, quad_inst) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view[0]) == RES_OK);

  CHK(s3d_scene_clear(scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, quad) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view[1]) == RES_OK);

  CHK(s3d_shape_get_id(quad, &quad_id) == RES_OK);
  CHK(s3d_shape_get_id(quad_inst, &quad_inst_id) == RES_OK);

  f3(ray.org, 0.f, 0.5f, -1.f);
  f3(ray.dir, 0.f, 0.f, 1.f);
  f2(range, 0.f, FLT_MAX);
  CHK(s3d_scene_view_trace_ray
    (view[0], ray.org, ray.dir, range, &ray, &hit[0]) == RES_OK);
  CHK(s3d_scene_view_trace_ray
    (view[1], ray.org, ray.dir, range, &ray, &hit[1]) == RES_OK);

  CHK(hit[0].prim.prim_id == 0);
  CHK(hit[1].prim.prim_id == 1);
  CHK(hit[0].prim.geom_id == quad_id);
  CHK(hit[1].prim.geom_id == quad_id);
  CHK(hit[0].prim.inst_id == quad_inst_id);
  CHK(hit[1].prim.inst_id == S3D_INVALID_ID);
  CHK(f3_eq_eps(hit[0].normal, f3_minus(dir, hit[1].normal), 1.e-6f) == 1);
  CHK(eq_epsf(hit[0].distance, hit[1].distance, 1.e-6f) == 1);

  CHK(s3d_primitive_get_attrib
    (&hit[0].prim, S3D_GEOMETRY_NORMAL, hit[0].uv, &attr) == RES_OK);
  f3_normalize(attr.value, attr.value);
  f3_normalize(hit[0].normal, hit[0].normal);
  CHK(f3_eq_eps(hit[0].normal, attr.value, 1.e-6f) == 1);

  CHK(s3d_primitive_get_attrib
    (&hit[1].prim, S3D_GEOMETRY_NORMAL, hit[1].uv, &attr) == RES_OK);
  f3_normalize(attr.value, attr.value);
  f3_normalize(hit[1].normal, hit[1].normal);
  CHK(f3_eq_eps(hit[1].normal, attr.value, 1.e-6f) == 1);

  CHK(s3d_scene_view_ref_put(view[0]) == RES_OK);
  CHK(s3d_scene_view_ref_put(view[1]) == RES_OK);

  CHK(s3d_shape_ref_put(quad_inst) == RES_OK);
  CHK(s3d_shape_ref_put(quad) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
}

static void
test_cbox(struct s3d_device* dev)
{
  struct image img;
  struct camera cam;
  struct cbox_desc cbox_desc;
  struct s3d_scene* scn;
  struct s3d_scene* cbox;
  struct s3d_shape* shape;
  struct s3d_vertex_data vdata;
  struct s3d_scene_view* view;
  float lower[3], upper[3], extend[3];
  float size[2];
  float pos[3], tgt[3], up[3];
  float org[3], dir[3], range[2];
  float proj_ratio;
  unsigned walls_id;
  const size_t img_sz[2] = { 640, 480 };
  const size_t N = 8;
  size_t x, y;

  CHK(s3d_scene_create(dev, &cbox) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);

  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = cbox_get_position;

  /* Walls */
  cbox_desc.vertices = cbox_walls;
  cbox_desc.indices = cbox_walls_ids;
  CHK(s3d_shape_create_mesh(dev, &shape) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(shape, cbox_walls_ntris, cbox_get_ids,
    cbox_walls_nverts, &vdata, 1, &cbox_desc) == RES_OK);
  CHK(s3d_scene_attach_shape(cbox, shape) == RES_OK);
  CHK(s3d_shape_get_id(shape, &walls_id) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);

  /* Short block */
  cbox_desc.vertices = cbox_short_block;
  cbox_desc.indices = cbox_block_ids;
  CHK(s3d_shape_create_mesh(dev, &shape) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(shape, cbox_block_ntris, cbox_get_ids,
    cbox_block_nverts, &vdata, 1, &cbox_desc) == RES_OK);
  CHK(s3d_scene_attach_shape(cbox, shape) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);

  /* Tall block */
  cbox_desc.vertices = cbox_tall_block;
  cbox_desc.indices = cbox_block_ids;
  CHK(s3d_shape_create_mesh(dev, &shape) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(shape, cbox_block_ntris, cbox_get_ids,
    cbox_block_nverts, &vdata, 1, &cbox_desc) == RES_OK);
  CHK(s3d_scene_attach_shape(cbox, shape) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);

  /* Compute the cbox extends */
  CHK(s3d_scene_view_create(cbox, S3D_GET_PRIMITIVE, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, lower, upper) == RES_OK);
  CHK(s3d_scene_view_ref_put(view) == RES_OK);
  f3_sub(extend, upper, lower);

  /* Create instances */
  size[0] = extend[0]*(float)N + (extend[0]*0.05f) * (float)(N-1);
  size[1] = extend[2]*(float)N + (extend[2]*0.05f) * (float)(N-1);
  pos[0] = -size[0] * 0.5f;
  pos[1] = 0;
  FOR_EACH(x, 0, N) {
    pos[2] = -size[1] * 0.5f;
    FOR_EACH(y, 0, N) {
      CHK(s3d_scene_instantiate(cbox, &shape) == RES_OK);
      CHK(s3d_instance_set_position(shape, pos) == RES_OK);
      CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
      CHK(s3d_shape_ref_put(shape) == RES_OK);
      pos[2] += extend[2] * 1.05f;
    }
    pos[0] += extend[0] * 1.05f;
  }

  /* Setup point of view */
  f3(pos, 0.f, -3000.f, 0.f);
  f3(tgt, 0.f, 0.f, 0.f);
  f3(up, 0.f, 0.f, 1.f);
  proj_ratio = (float)img_sz[0] / (float)img_sz[1];
  camera_init(&cam, pos, tgt, up, (float)PI*0.5f, proj_ratio);

  image_init(NULL, &img);
  CHK(image_setup
    (&img, img_sz[0], img_sz[1], img_sz[0]*3, IMAGE_RGB8, NULL) == RES_OK);

  /* Trace rays */
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  range[0] = 0.f;
  range[1] = FLT_MAX;
  FOR_EACH(y, 0, img_sz[1]) {
    float pixel[2];
    pixel[1] = (float)y / (float)img_sz[1];
    FOR_EACH(x, 0, img_sz[0]) {
      const size_t ipix = (y*img_sz[0] + x)*3/*RGB*/;
      struct s3d_hit hit;

      pixel[0] = (float)x/(float)img_sz[0];
      camera_ray(&cam, pixel, org, dir);
      CHK(s3d_scene_view_trace_ray(view, org, dir, range, NULL, &hit) == RES_OK);

      if(S3D_HIT_NONE(&hit)) {
        ((uint8_t*)img.pixels)[ipix+0] = 0;
        ((uint8_t*)img.pixels)[ipix+1] = 0;
        ((uint8_t*)img.pixels)[ipix+2] = 0;
      } else {
        float normal[3] = {0.f, 0.f, 0.f};
        float col[3], dot;
        float f = (float)hit.prim.inst_id / (float)(N*N);
        f3(col, f, MMAX(0.f, 1.f-f), MMAX(0.f, 1.f-f));

        if(hit.prim.geom_id == walls_id) {
          if(hit.prim.prim_id == 4 || hit.prim.prim_id == 5) {
            f3(col, col[0], 0.f, 0.f);
          } else if(hit.prim.prim_id == 6 || hit.prim.prim_id == 7) {
            f3(col, 0.f, col[1], 0.f);
          }
        }

        f3_normalize(normal, hit.normal);
        dot = absf(f3_dot(normal, dir));
        ((uint8_t*)img.pixels)[ipix+0] = (uint8_t)(dot * col[0] * 255.f);
        ((uint8_t*)img.pixels)[ipix+1] = (uint8_t)(dot * col[1] * 255.f);
        ((uint8_t*)img.pixels)[ipix+2] = (uint8_t)(dot * col[2] * 255.f);
      }
    }
  }
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  /* Write image */
  CHK(image_write_ppm_stream(&img, 0, stdout) == RES_OK);
  image_release(&img);

  /* Release data */
  CHK(s3d_scene_ref_put(cbox) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_device* dev;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);

  test_quad(dev);
  test_cbox(dev);

  CHK(s3d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
