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
#include "test_s3d_utils.h"

#include <rsys/image.h>
#include <rsys_math.h>

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct image img;
  struct camera cam;
  struct s3d_hit hit;
  struct s3d_device* dev;
  struct s3d_scene* scn;
  struct s3d_shape* shape;
  struct s3d_scene_view* view;
  const size_t img_sz[2] = {640, 480};
  const float radius = 2;
  const float center[3] = {1.0, 1.0, 0};
  float pos[3] = {0, 0, 0};
  float tgt[3] = {0, 0, 0};
  float up[3] = {0, 1, 0};
  const float range[2] = {0, FLT_MAX};
  float org[3];
  float dir[3];

  float proj_ratio;
  size_t x, y;
  int hit_something = 0;
  (void)argc, (void)argv;

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  image_init(&allocator, &img);
  CHK(image_setup
    (&img, img_sz[0], img_sz[1], img_sz[0]*3, IMAGE_RGB8, NULL) == RES_OK);

  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_shape_create_sphere(dev, &shape) == RES_OK);
  CHK(s3d_sphere_setup(shape, center, radius) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);

  f3(org, 1.0, 1.0, -4);
  f3(dir, 0.0, 0.0, 1);
  CHK(s3d_scene_view_trace_ray(view, org, dir, range, NULL, &hit) == RES_OK);
  CHK(!S3D_HIT_NONE(&hit));
  CHK(eq_epsf(hit.distance, 2, 1.e-6f));

  f3(pos, 0, 0, -10);
  f3(tgt, 0, 0, 0);
  f3(up, 0, 1, 0);
  proj_ratio = (float)img_sz[0] / (float)img_sz[1];
  camera_init(&cam, pos, tgt, up, (float)PI*0.25f, proj_ratio);

  FOR_EACH(y, 0, img_sz[1]) {
    float pixel[2];
    pixel[1] = (float)y / (float)img_sz[1];
    FOR_EACH(x, 0, img_sz[0]) {
      const size_t ipix = (y*img_sz[0] + x)*3/*RGB*/;

      pixel[0] = (float)x/(float)img_sz[0];
      camera_ray(&cam, pixel, org, dir);
      CHK(s3d_scene_view_trace_ray(view, org, dir, range, NULL, &hit) == RES_OK);
      if(S3D_HIT_NONE(&hit)) {
        ((uint8_t*)img.pixels)[ipix+0] = 0;
        ((uint8_t*)img.pixels)[ipix+1] = 0;
        ((uint8_t*)img.pixels)[ipix+2] = 0;
      } else {
        struct s3d_attrib attr;
        float normal[3] = {0.f, 0.f, 0.f};
        float tmp[3];
        float len;
        float dot;

        f3_normalize(normal, hit.normal);
        CHK(s3d_primitive_get_attrib
          (&hit.prim, S3D_GEOMETRY_NORMAL, hit.uv, &attr) == RES_OK);
        f3_normalize(attr.value, attr.value);
        CHK(f3_eq_eps(normal, attr.value, 1.e-3f));

        f3_add(pos, org, f3_mulf(pos, dir, hit.distance));
        CHK(s3d_primitive_get_attrib
          (&hit.prim, S3D_POSITION, hit.uv, &attr) == RES_OK);
        CHK(f3_eq_eps(pos, attr.value, 1.e-3f));

        len = f3_len(f3_sub(pos, pos, center));
        CHK(eq_epsf(len, radius, 1.e-3f));
        CHK(f3_eq_eps(f3_mulf(tmp, normal, radius), pos, 1.e-3f));

        dot = absf(f3_dot(normal, dir));
        ((uint8_t*)img.pixels)[ipix+0] = (uint8_t)(dot*255.f);
        ((uint8_t*)img.pixels)[ipix+1] = (uint8_t)(dot*255.f);
        ((uint8_t*)img.pixels)[ipix+2] = (uint8_t)(dot*255.f);
        hit_something = 1;
      }
    }
  }
  CHK(hit_something == 1);

  /* Write image */
  CHK(image_write_ppm_stream(&img, 0, stdout) == RES_OK);
  image_release(&img);

  CHK(s3d_device_ref_put(dev) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

