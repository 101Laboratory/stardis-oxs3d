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

#define STACK_GUARD() \
    volatile unsigned __guard = 0xDEADBEEF

#define CHECK_GUARD() \
    if (__guard != 0xDEADBEEF) __debugbreak()


#include "s3d.h"
#include "test_s3d_camera.h"
#include "test_s3d_cbox.h"
#include "test_s3d_utils.h"

#include <rsys/image.h>
#include <rsys/float3.h>



int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_device* dev;
  struct s3d_shape* box;
  struct s3d_shape* sphere;
  struct s3d_scene* scn;
  struct s3d_scene_view* view;
  struct s3d_vertex_data vdata;
  struct cbox_desc desc;
  struct image img;
  struct camera cam;
  const size_t img_sz[2] = {640, 480};
  float pos[3] = {0, 0, 0};
  float tgt[3] = {0, 0, 0};
  float up[3] = {0, 1, 0};
  float tmp[3];
  float lower[3];
  float upper[3];
  float proj_ratio;
  size_t x, y;
  float center[3];
  (void)argc, (void)argv;
  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);

  CHK(s3d_shape_create_sphere(dev, &sphere) == RES_OK);
  CHK(s3d_sphere_setup(sphere, f3(center, 150, 200, 90), 90) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere) == RES_OK);
  CHK(s3d_shape_ref_put(sphere) == RES_OK);

  CHK(s3d_shape_create_sphere(dev, &sphere) == RES_OK);
  CHK(s3d_sphere_setup(sphere, f3(center, 400, 200, 90), 90) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere) == RES_OK);
  CHK(s3d_shape_ref_put(sphere) == RES_OK);

  desc.vertices = cbox_walls;
  desc.indices = cbox_walls_ids;
  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = cbox_get_position;
  CHK(s3d_shape_create_mesh(dev, &box) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(box, cbox_walls_ntris, cbox_get_ids,
    cbox_walls_nverts, &vdata, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, box) == RES_OK);
  CHK(s3d_shape_ref_put(box) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);
  CHK(s3d_scene_view_get_aabb(view, lower, upper) == RES_OK);
  CHK(f3_eq(lower, f3(tmp, 0, 0, 0)));
  CHK(f3_eq(upper, f3(tmp, 552, 559, 548)));

  image_init(NULL, &img);
  CHK(image_setup
    (&img, img_sz[0], img_sz[1], img_sz[0]*3, IMAGE_RGB8, NULL) == RES_OK);

  f3(pos, 278.f, -1000.f, 273.f);
  f3(tgt, 278.f, 0.f, 273.f);
  f3(up, 0, 0, 1);
  proj_ratio = (float)img_sz[0] / (float)img_sz[1];
  camera_init(&cam, pos, tgt, up, (float)PI*0.25f, proj_ratio);

  FOR_EACH(y, 0, img_sz[1]) {
    float pixel[2];
    pixel[1] = (float)y / (float)img_sz[1];
    FOR_EACH(x, 0, img_sz[0]) {
      const size_t ipix = (y*img_sz[0] + x)*3/*RGB*/;
      struct s3d_hit hit;
      const float range[2] = {0, FLT_MAX};
      float org[3];
      float dir[3];

      pixel[0] = (float)x/(float)img_sz[0];
      camera_ray(&cam, pixel, org, dir);
      CHK(s3d_scene_view_trace_ray(view, org, dir, range, NULL, &hit) == RES_OK);
      if(S3D_HIT_NONE(&hit)) {
        ((uint8_t*)img.pixels)[ipix+0] = 0;
        ((uint8_t*)img.pixels)[ipix+1] = 0;
        ((uint8_t*)img.pixels)[ipix+2] = 0;
      } else {
        float normal[3] = {0.f, 0.f, 0.f};
        float dot;
        f3_normalize(normal, hit.normal);
        dot = absf(f3_dot(normal, dir));
        ((uint8_t*)img.pixels)[ipix+0] = (uint8_t)(dot*255.f);
        ((uint8_t*)img.pixels)[ipix+1] = (uint8_t)(dot*255.f);
        ((uint8_t*)img.pixels)[ipix+2] = (uint8_t)(dot*255.f);
      }
    }
  }
  
  /* Write image */
  FILE* f = fopen("out.ppm", "w");
  CHK(image_write_ppm_stream(&img, 0, f) == RES_OK);
  fclose(f);

  image_release(&img);

  return 0;

  CHK(s3d_device_ref_put(dev) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  _exit(0);
}
