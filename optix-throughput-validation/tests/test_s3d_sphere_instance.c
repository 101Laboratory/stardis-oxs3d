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
#include <rsys/float2.h>
#include <rsys/float3.h>

#include <string.h>

static int
filter_front_face
  (const struct s3d_hit* hit,
   const float pos[3],
   const float dir[3],
   const float range[2],
   void* ray_data,
   void* filter_data)
{
  CHK(hit != NULL);
  CHK(pos != NULL);
  CHK(dir != NULL);
  CHK(range != NULL);
  CHK(range[0] < range[1]);
  CHK(filter_data == NULL);
  CHK(ray_data == NULL);
  CHK(S3D_HIT_NONE(hit) == 0);
  return f3_dot(hit->normal, dir) < 0;
}

static void
test_sampling
  (struct s3d_scene_view* view,
   const unsigned geom_id,
   const unsigned inst0_id)
{
  struct s3d_attrib attr0;
  struct s3d_attrib attr1;
  struct s3d_primitive prim;
  int N = 10000;
  int i;
  float center[3];
  float sum;
  float st[2];
  float E, V, SE;

  /* Check that 50 percents of samples lie onto the 1st instance */
  sum = 0;
  FOR_EACH(i, 0, N) {
    const float u = rand_canonic();
    const float v = rand_canonic();
    const float w = rand_canonic();

    CHK(s3d_scene_view_sample(view, u, v, w, &prim, st) == RES_OK);
    CHK(prim.geom_id == geom_id);
    if(prim.inst_id == inst0_id) {
      sum += 1;
    }

    CHK(s3d_primitive_get_attrib(&prim, S3D_POSITION, st, &attr0) == RES_OK);
    CHK(s3d_primitive_get_attrib
      (&prim, S3D_GEOMETRY_NORMAL, st, &attr1) == RES_OK);

    if(prim.inst_id == inst0_id) {
      f3_sub(attr0.value, attr0.value, f3(center,-1.5, 0, 0));
    } else {
      f3_sub(attr0.value, attr0.value, f3(center, 1.5, 0, 0));
    }
    f3_mulf(attr1.value, attr1.value, 2.f);
    CHK(f3_eq_eps(attr0.value, attr1.value, 1.e-3f));
  }
  E = sum / (float)N;
  V = sum / (float)N - E*E;
  SE = (float)sqrt(V/(float)N);
  CHK(eq_epsf(E, 0.5, 2*SE));
}

static void
test_ray_tracing(struct s3d_scene_view* view)
{
  struct image img;
  struct camera cam;
  const size_t img_sz[2] = {640, 480};
  float pos[3] = {0, 0, 0};
  float tgt[3] = {0, 0, 0};
  float up[3] = {0, 1, 0};
  float proj_ratio;
  size_t x, y;

  image_init(NULL, &img);
  CHK(image_setup
    (&img, img_sz[0], img_sz[1], img_sz[0]*3, IMAGE_RGB8, NULL) == RES_OK);

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
        f3_normalize(normal, hit.normal);
        ((uint8_t*)img.pixels)[ipix+0] = (uint8_t)(fabs(normal[0])*255.f);
        ((uint8_t*)img.pixels)[ipix+1] = (uint8_t)(fabs(normal[1])*255.f);
        ((uint8_t*)img.pixels)[ipix+2] = (uint8_t)(fabs(normal[2])*255.f);
      }
    }
  }

  /* Write image */
  CHK(image_write_ppm_stream(&img, 0, stdout) == RES_OK);
  image_release(&img);
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s3d_device* dev;
  struct s3d_shape* sphere;
  struct s3d_shape* sphere0;
  struct s3d_shape* sphere1;
  struct s3d_scene* scn;
  struct s3d_scene_view* view;
  unsigned geom_id;
  unsigned inst0_id;
  unsigned inst1_id;
  float center[3];
  char filter = 0;
  (void)argc, (void)argv;

  if(argc > 1 && !strcmp(argv[1], "filter")) {
    filter = 1;
  }

  CHK(mem_init_proxy_allocator(&allocator, &mem_default_allocator) == RES_OK);
  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);

  CHK(s3d_shape_create_sphere(dev, &sphere) == RES_OK);
  CHK(s3d_sphere_setup(sphere, f3_splat(center, 0), 2) == RES_OK);
  CHK(s3d_shape_get_id(sphere, &geom_id) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere) == RES_OK);

  CHK(s3d_scene_instantiate(scn, &sphere0) == RES_OK);
  CHK(s3d_scene_instantiate(scn, &sphere1) == RES_OK);
  CHK(s3d_shape_get_id(sphere0, &inst0_id) == RES_OK);
  CHK(s3d_shape_get_id(sphere0, &inst1_id) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere0) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, sphere1) == RES_OK);
  CHK(s3d_instance_set_position(sphere0, f3(center,-1.5, 0, 0)) == RES_OK);
  CHK(s3d_instance_set_position(sphere1, f3(center, 1.5, 0, 0)) == RES_OK);

  if(filter) {
    CHK(s3d_sphere_set_hit_filter_function
      (NULL, filter_front_face, NULL) == RES_BAD_ARG);
    CHK(s3d_sphere_set_hit_filter_function
      (sphere, filter_front_face, NULL) == RES_OK);
  }

  CHK(s3d_scene_view_create
    (scn, S3D_TRACE|S3D_GET_PRIMITIVE|S3D_SAMPLE, &view) == RES_OK);

  test_sampling(view, geom_id, inst0_id);
  test_ray_tracing(view);

  CHK(s3d_device_ref_put(dev) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_shape_ref_put(sphere) == RES_OK);
  CHK(s3d_shape_ref_put(sphere0) == RES_OK);
  CHK(s3d_shape_ref_put(sphere1) == RES_OK);
  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

