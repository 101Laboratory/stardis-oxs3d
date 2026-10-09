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

#define _POSIX_C_SOURCE 200112L /* exp2f, fabsf */

#include "s3d.h"
#include "test_s3d_camera.h"
#include "test_s3d_cbox.h"
#include "test_s3d_utils.h"

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/image.h>

#include <string.h>

#define IMG_WIDTH 640
#define IMG_HEIGHT 480

struct ray_data {
  float ray_org[3];
  float ray_dir[3];
  float ray_range[2];
};

static int
filter_func
  (const struct s3d_hit* hit,
   const float pos[3],
   const float dir[3],
   const float range[2],
   void* ray_data,
   void* filter_data)
{
  struct ray_data* data = ray_data;
  CHK(hit != NULL);
  CHK(pos != NULL);
  CHK(dir != NULL);
  CHK(range != NULL);
  CHK(ray_data != NULL);
  CHK((uintptr_t)filter_data == 0xDECAFBAD);
  CHK(S3D_HIT_NONE(hit) == 0);
  CHK(f3_eq(pos, data->ray_org));
  CHK(f3_eq(dir, data->ray_dir));
  CHK(f2_eq(range, data->ray_range));
  return hit->prim.prim_id % 2 == 0;
}

static void
triangle_get_ids(const unsigned itri, unsigned ids[3], void* ctx)
{
  (void)ctx;
  CHK(itri == 0);
  CHK(ids);
  ids[0] = 0;
  ids[1] = 1;
  ids[2] = 2;
}

static void
triangle_get_pos(const unsigned ivert, float pos[3], void* ctx)
{
  float* vertices = ctx;
  CHK(ctx);
  CHK(ivert < 3);
  CHK(pos);
  switch (ivert) { /* Setup a random triangle */
  case 0: f3_set(pos, vertices + 0); break;
  case 1: f3_set(pos, vertices + 3); break;
  case 2: f3_set(pos, vertices + 6); break;
  default: FATAL("Unreachable code\n"); break;
  }
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct image img;
  struct s3d_device* dev;
  struct s3d_hit hit;
  struct s3d_scene* scn;
  struct s3d_scene* scn2;
  struct s3d_scene_view* scnview;
  struct s3d_shape* inst;
  struct s3d_shape* walls;
  struct s3d_shape* walls_copy;
  struct s3d_shape* tall_block;
  struct s3d_shape* short_block;
  struct s3d_vertex_data attribs;
  struct s3d_primitive prims[30];
  struct camera cam;
  struct cbox_desc desc;
  unsigned ntris, nverts;
  size_t nprims;
  size_t ix, iy;
  float transform[12];
  float vec[3];
  float lower[3], upper[3];
  float pos[3], tgt[3], up[3];
  float org[3] = { 0.f, 0.f, 0.f };
  float dir[3] = { 0.f, 1.f, 0.f };
  float range[2] = { 0.f, FLT_MAX };
  unsigned inst_id;
  unsigned walls_id;
  unsigned tall_block_id;
  unsigned short_block_id;
  size_t a, i;
  char filter = 0;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  if(argc > 1 && !strcmp(argv[1], "filter")) {
    filter = 1;
  }

  image_init(&allocator, &img);
  CHK(image_setup
    (&img, IMG_WIDTH, IMG_HEIGHT, IMG_WIDTH*3, IMAGE_RGB8, NULL) == RES_OK);

  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);

  /* Trace ray in empty scene */
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S3D_HIT_NONE(&hit));
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  attribs.usage = S3D_POSITION;
  attribs.type = S3D_FLOAT3;
  attribs.get = cbox_get_position;

  ntris = cbox_walls_ntris;
  nverts = cbox_walls_nverts;
  desc.vertices = cbox_walls;
  desc.indices = cbox_walls_ids;
  CHK(s3d_shape_create_mesh(dev, &walls) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (walls, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, walls) == RES_OK);
  CHK(s3d_shape_ref_put(walls) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_trace_ray(NULL, NULL, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, NULL, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, org, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, org, NULL, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, NULL, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, NULL, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, org, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, NULL, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, NULL, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, org, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, org, NULL, range, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, NULL, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, NULL, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, org, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, NULL) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, NULL, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, NULL, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, org, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, org, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, NULL, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, NULL, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, org, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, NULL, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, NULL, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, org, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, org, NULL, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, NULL, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, NULL, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(NULL, org, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  f3(dir, 1.f, 1.f, 1.f);
  CHK(s3d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_BAD_ARG);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  f3(dir, 0.f, 1.f, 0.f);
  CHK(s3d_scene_clear(scn) == RES_OK);

  /* Update the inst with the CBox tall block mesh */
  ntris = cbox_block_ntris;
  nverts = cbox_block_nverts;
  desc.vertices = cbox_short_block;
  desc.indices = cbox_block_ids;
  CHK(s3d_shape_create_mesh(dev, &tall_block) == RES_OK);
  CHK(s3d_shape_get_id(tall_block, &tall_block_id) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (tall_block, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, tall_block) == RES_OK);

  /* Update the inst vertices */
  desc.vertices = cbox_tall_block;
  CHK(s3d_mesh_setup_indexed_vertices
    (tall_block, ntris, S3D_KEEP, nverts, &attribs, 1, &desc) == RES_OK);

  /* Create a the CBox short block inst */
  desc.vertices = cbox_short_block;
  CHK(s3d_shape_create_mesh(dev, &short_block) == RES_OK);
  CHK(s3d_shape_get_id(short_block, &short_block_id) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (short_block, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, short_block) == RES_OK);

  /* Instantiate the scene */
  CHK(s3d_scene_instantiate(scn, &inst) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 20);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s3d_shape_get_id(inst, &inst_id) == RES_OK);

  /* Create the CBox walls */
  desc.indices = cbox_walls_ids;
  desc.vertices = cbox_walls;
  nverts = cbox_walls_nverts;
  ntris = cbox_walls_ntris;
  CHK(s3d_shape_create_mesh(dev, &walls) == RES_OK);
  CHK(s3d_shape_get_id(walls, &walls_id) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices
    (walls, ntris, cbox_get_ids, nverts, &attribs, 1, &desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, walls) == RES_OK);

  /* Check that the ids are all different */
  CHK(walls_id != short_block_id);
  CHK(walls_id != tall_block_id);
  CHK(short_block_id != tall_block_id);

  /* Attach the CBox instance to a scene */
  CHK(s3d_scene_create(dev, &scn2) == RES_OK);
  f3(org, -100.f, 0.f, -2.f);
  CHK(s3d_scene_attach_shape(scn2, inst) == RES_OK);
  CHK(s3d_instance_set_position(inst, org) == RES_OK);

  CHK(s3d_shape_enable(inst, 0) == RES_OK);
  CHK(s3d_scene_view_create(scn2, S3D_TRACE|S3D_SAMPLE, &scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_shape_enable(inst, 1) == RES_OK);
  CHK(s3d_scene_view_create(scn2, S3D_TRACE, &scnview) == RES_OK);
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s3d_shape_create_mesh(dev, &walls_copy) == RES_OK);
  CHK(s3d_mesh_copy(walls, walls_copy) == RES_OK);
  CHK(s3d_shape_ref_put(walls) == RES_OK);
  CHK(s3d_shape_get_id(walls_copy, &walls_id) == RES_OK);
  if(filter) {
    CHK(s3d_mesh_set_hit_filter_function
      (walls_copy, filter_func, (void*)(uintptr_t)0xDECAFBAD) == RES_OK);
  }

  CHK(s3d_scene_clear(scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, walls_copy) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, short_block) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, tall_block) == RES_OK);

  CHK(s3d_scene_view_create(scn2, S3D_TRACE|S3D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s3d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 30);

  CHK(s3d_scene_view_get_aabb(scnview, lower, upper) == RES_OK);
  CHK(eq_epsf(lower[0], -100.f, 1.e-6f) == 1);
  CHK(eq_epsf(lower[1], 0.f, 1.e-6f) == 1);
  CHK(eq_epsf(lower[2], -2.f, 1.e-6f) == 1);
  CHK(eq_epsf(upper[0], 452.f, 1.e-6f) == 1);
  CHK(eq_epsf(upper[1], 559.f, 1.e-6f) == 1);
  CHK(eq_epsf(upper[2], 546.f, 1.e-6f) == 1);

  FOR_EACH(i, 0, nprims) {
    size_t j;
    CHK(s3d_scene_view_get_primitive(scnview, (unsigned)i, prims + i) == RES_OK);
    CHK(S3D_PRIMITIVE_EQ(prims + i, &S3D_PRIMITIVE_NULL) == 0);
    FOR_EACH(j, 0, i) {
      CHK(S3D_PRIMITIVE_EQ(prims + i, prims + j) == 0);
    }

    CHK(s3d_primitive_get_transform(prims + i, transform) == RES_OK);
    CHK(f3_eq(transform + 0, f3(vec, 1.f, 0.f, 0.f)) == 1);
    CHK(f3_eq(transform + 3, f3(vec, 0.f, 1.f, 0.f)) == 1);
    CHK(f3_eq(transform + 6, f3(vec, 0.f, 0.f, 1.f)) == 1);
    CHK(f3_eq(transform + 9, f3(vec, -100.f, 0.f, -2.f)) == 1);
  }

  f3(pos, 178.f, -1000.f, 273.f);
  f3(tgt, 178.f, 0.f, 273.f);
  f3(up, 0.f, 0.f, 1.f);
  camera_init(&cam, pos, tgt, up, (float)PI*0.25f,
    (float)IMG_WIDTH/(float)IMG_HEIGHT);
  FOR_EACH(iy, 0, IMG_HEIGHT) {
    float pixel[2];

    pixel[1] = (float)iy/(float)IMG_HEIGHT;
    FOR_EACH(ix, 0, IMG_WIDTH) {
      struct ray_data ray_data;
      const size_t ipix = (iy*IMG_WIDTH + ix) * 3/*RGB*/;

      pixel[0] = (float)ix/(float)IMG_WIDTH;
      camera_ray(&cam, pixel, org, dir);

      f3_set(ray_data.ray_org, org);
      f3_set(ray_data.ray_dir, dir);
      f2_set(ray_data.ray_range, range);
      CHK(s3d_scene_view_trace_ray
        (scnview, org, dir, range, &ray_data, &hit) == RES_OK);

      if(S3D_HIT_NONE(&hit)) {
        ((uint8_t*)img.pixels)[ipix+0] = 0;
        ((uint8_t*)img.pixels)[ipix+1] = 0;
        ((uint8_t*)img.pixels)[ipix+2] = 0;
      } else {
        float N[3], len, dot, col[3] = { 1.f, 1.f, 1.f };
        struct s3d_attrib attr;

        CHK(hit.prim.inst_id == inst_id);
        CHK(hit.prim.geom_id == walls_id
         || hit.prim.geom_id == tall_block_id
         || hit.prim.geom_id == short_block_id);
        CHK(hit.prim.geom_id < 10);

        CHK(s3d_primitive_get_transform(&hit.prim, transform) == RES_OK);
        CHK(f3_eq(transform + 0, f3(vec, 1.f, 0.f, 0.f)) == 1);
        CHK(f3_eq(transform + 3, f3(vec, 0.f, 1.f, 0.f)) == 1);
        CHK(f3_eq(transform + 6, f3(vec, 0.f, 0.f, 1.f)) == 1);
        CHK(f3_eq(transform + 9, f3(vec, -100.f, 0.f, -2.f)) == 1);

        CHK(s3d_primitive_get_attrib
          (&hit.prim, S3D_POSITION, hit.uv, &attr) == RES_OK);
        CHK(attr.type == S3D_FLOAT3);
        CHK(attr.usage == S3D_POSITION);
        f3_add(pos, f3_mulf(pos, dir, hit.distance), org);
        CHK(f3_eq_eps
          (pos, attr.value, 1.e-3f/*Sic O_o!! Really bad precision!*/));

        len = f3_normalize(N, hit.normal);
        CHK(len != 0);

        CHK(s3d_primitive_get_attrib
          (&hit.prim, S3D_GEOMETRY_NORMAL, hit.uv, &attr) == RES_OK);
        CHK(attr.type == S3D_FLOAT3);
        CHK(attr.usage == S3D_GEOMETRY_NORMAL);
        f3_normalize(attr.value, attr.value);
        CHK(f3_eq_eps(attr.value, N, 1.e-6f) == 1);

        CHK(hit.prim.scene_prim_id >= hit.prim.prim_id);
        CHK(hit.prim.scene_prim_id < 30);

        if(hit.prim.geom_id == walls_id) {
          if(hit.prim.prim_id == 4 || hit.prim.prim_id == 5) {
            col[0] = 1.f, col[1] = 0.f, col[2] = 0.f;
          } else if(hit.prim.prim_id == 6 || hit.prim.prim_id == 7) {
            col[0] = 0.f, col[1] = 1.f, col[2] = 0.f;
          }
        }

        dot = f3_dot(N, dir);
        if(dot < 0.f)
          dot = f3_dot(f3_minus(N, N), dir);

        ((uint8_t*)img.pixels)[ipix+0] = (unsigned char)(dot * col[0] * 255.f);
        ((uint8_t*)img.pixels)[ipix+1] = (unsigned char)(dot * col[1] * 255.f);
        ((uint8_t*)img.pixels)[ipix+2] = (unsigned char)(dot * col[2] * 255.f);
      }
    }
  }
  CHK(s3d_scene_view_ref_put(scnview) == RES_OK);

  CHK(image_write_ppm_stream(&img, 0, stdout) == RES_OK);
  image_release(&img);

  CHK(s3d_shape_ref_put(inst) == RES_OK);
  CHK(s3d_shape_ref_put(short_block) == RES_OK);
  CHK(s3d_shape_ref_put(tall_block) == RES_OK);
  CHK(s3d_shape_ref_put(walls_copy) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_scene_ref_put(scn2) == RES_OK);

  /* Check accuracy on a configuration whose analytic distance is known
   * ===================================================================
   * SEQUENTIAL VERSION (CPU-friendly, 3-layered nested loop)
   *
   * Structure:
   *   Layer 1: FOR_EACH(a, 0, 16)       -- 16 amplitude levels
   *   Layer 2:   FOR_EACH(i, 0, 100)    -- 100 random triangles per amplitude
   *   Layer 3:     FOR_EACH(j, 0, 1000) -- 1000 ray traces per triangle
   *
   * Total: 16 x 100 x 1000 = 1,600,000 sequential ray traces.
   * Each iteration of Layer 3 calls s3d_scene_view_trace_ray() once.
   * Dependencies:
   *   - Layer 3 rays are independent (same triangle/BVH, different random ray)
   *   - Layer 2 iterations need per-triangle BVH rebuild (serial dependency)
   *   - Layer 1 iterations are fully independent (different amplitude/eps)
   * =================================================================== */
  goto gpu_parallel;
  FOR_EACH(a, 0, 16) {
    const float amplitude = exp2f((float)a);
    const float eps = 5e-6f * amplitude;
    float vertices[9];
    struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
    struct s3d_scene_view* view = NULL;
    struct s3d_shape* msh = NULL;
    FOR_EACH(i, 0, 100) {
      float A[3], B[3], C[3], AB[3], AC[3], N[3];
      int j, n;

      /* Randomly generate a triangle ABC */
      FOR_EACH(n, 0, 3)
        A[n] = (rand_canonic() - 0.5f) * amplitude;
      do {
        FOR_EACH(n, 0, 3) B[n] = (rand_canonic() - 0.5f) * amplitude;
      } while (f3_eq_eps(A, B, eps));
      do {
        FOR_EACH(n, 0, 3) C[n] = (rand_canonic() - 0.5f) * amplitude;
      } while (f3_eq_eps(A, C, eps) || f3_eq_eps(B, C, eps));

      f3_sub(AB, B, A);
      f3_sub(AC, C, A);
      f3_cross(N, AB, AC);
      f3_normalize(N, N);

      f3_set(vertices + 0, A);
      f3_set(vertices + 3, B);
      f3_set(vertices + 6, C);

      CHK(s3d_scene_create(dev, &scn) == RES_OK);
      CHK(s3d_shape_create_mesh(dev, &msh) == RES_OK);
      CHK(s3d_scene_attach_shape(scn, msh) == RES_OK);

      vdata.usage = S3D_POSITION;
      vdata.type = S3D_FLOAT3;
      vdata.get = triangle_get_pos;
      CHK(s3d_mesh_setup_indexed_vertices
      (msh, 1, triangle_get_ids, 3, &vdata, 1, vertices) == RES_OK);

      CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);

      FOR_EACH(j, 0, 1000) {
        float proj[3]; /* Projection of pos on the line */
        float tmp[3];
        float u, v, w, h;

        /* Randomly generate a pos not on the triangle
         * with know position wrt the problem: pos = A + u.AB + v.AC + k.N */
        u = 3 * rand_canonic() - 1;
        v = 3 * rand_canonic() - 1;
        w = 1 - u - v;
        h = (2 * rand_canonic() - 1) * amplitude;
        f3_add(proj, A, f3_add(proj, f3_mulf(proj, AB, u), f3_mulf(tmp, AC, v)));
        f3_add(pos, proj, f3_mulf(pos, N, h));

        /* Raytrace from pos towards proj */
        f3_mulf(dir, N, (h > 0 ? -1.f : 1.f));
        f3_normalize(dir, dir);
        CHK(s3d_scene_view_trace_ray(view, pos, dir, range, NULL, &hit)
          == RES_OK);

        /* Check result */
        if(u < 0 || v < 0 || w < 0) {
          if(!S3D_HIT_NONE(&hit))
            CHK(u >= -FLT_EPSILON && v >= -FLT_EPSILON && w >= -FLT_EPSILON);
        } else {
          if(S3D_HIT_NONE(&hit))
            CHK(u <= FLT_EPSILON || v <= FLT_EPSILON || w <= FLT_EPSILON);
        }
        if(!S3D_HIT_NONE(&hit)) {
          struct s3d_attrib attr;
          float d;
          CHK(eq_epsf(hit.distance, fabsf(h), eps));
          CHK(s3d_primitive_get_attrib(&hit.prim, S3D_POSITION, hit.uv, &attr)
            == RES_OK);
          /* Intersection-point's position is less accurate than hit distance */
          d = f3_len(f3_sub(tmp, attr.value, proj));
          CHK(d <= 10 * eps);
        }
      }

      CHK(s3d_shape_ref_put(msh) == RES_OK);
      CHK(s3d_scene_view_ref_put(view) == RES_OK);
      CHK(s3d_scene_ref_put(scn) == RES_OK);
    }
  }

  /* =====================================================================
   * GPU-PARALLEL OPTIMIZED VERSION (batch ray trace via s3d_scene_view_trace_rays)
   *
   * Optimization strategy:
   * ---------------------------------------------------------------------------
   * The sequential version traces 1000 rays one-at-a-time per triangle.
   * All 1000 rays within a single triangle share the same BVH scene and are
   * fully independent -- the ideal case for GPU batch dispatch.
   *
   * Layer 3 parallelism (1000 rays/triangle -> 1 batch launch):
   *   - Pre-generate all 1000 random (origin, direction) pairs on the host
   *   - Submit them in one s3d_scene_view_trace_rays() call
   *   - The underlying GPU kernel (trace_rays_kernel) maps 1 CUDA thread per ray
   *   - Eliminates 999 redundant kernel launches per triangle
   *   - Reduces per-ray CPU overhead: arg validation, GPU alloc, H2D/D2H sync
   *
   * Layer 2 remains serial (BVH rebuild per triangle is unavoidable):
   *   - Each random triangle requires scene_create -> mesh_setup -> BVH build
   *   - Cannot batch across triangles without a multi-BVH or tagged-prim scheme
   *   - BVH build is ~1ms on GPU, amortized by 1000 parallel ray traces
   *
   * Layer 1 is independent but only 16 iterations -- not worth parallelizing.
   *
   * Expected speedup: ~50-200x over sequential version on RTX 4090
   *   - Sequential: 1.6M individual kernel launches (each ~5-10us overhead)
   *   - Parallel:   1,600 batch launches of 1000 rays each
   *   - Bottleneck shifts from CPU dispatch to GPU compute
   *
   * Verification is done on the CPU side after downloading batch results.
   * ===================================================================== */
  gpu_parallel:
#define BATCH_RAYS_PER_TRI 1000

  FOR_EACH(a, 0, 16) {
    const float amplitude = exp2f((float)a);
    const float eps = 5e-6f * amplitude;
    float vertices[9];
    struct s3d_vertex_data vdata = S3D_VERTEX_DATA_NULL;
    struct s3d_scene_view* view = NULL;
    struct s3d_shape* msh = NULL;

    float batch_origins[BATCH_RAYS_PER_TRI * 3];
    float batch_dirs[BATCH_RAYS_PER_TRI * 3];
    float batch_ranges[BATCH_RAYS_PER_TRI * 2];
    struct s3d_hit batch_hits[BATCH_RAYS_PER_TRI];
    float batch_u[BATCH_RAYS_PER_TRI];
    float batch_v[BATCH_RAYS_PER_TRI];
    float batch_w[BATCH_RAYS_PER_TRI];
    float batch_h[BATCH_RAYS_PER_TRI];
    float batch_proj[BATCH_RAYS_PER_TRI * 3];

    FOR_EACH(i, 0, 100) {
      float A[3], B[3], C[3], AB[3], AC[3], N_tri[3];
      int j, n;

      FOR_EACH(n, 0, 3)
        A[n] = (rand_canonic() - 0.5f) * amplitude;
      do {
        FOR_EACH(n, 0, 3) B[n] = (rand_canonic() - 0.5f) * amplitude;
      } while (f3_eq_eps(A, B, eps));
      do {
        FOR_EACH(n, 0, 3) C[n] = (rand_canonic() - 0.5f) * amplitude;
      } while (f3_eq_eps(A, C, eps) || f3_eq_eps(B, C, eps));

      f3_sub(AB, B, A);
      f3_sub(AC, C, A);
      f3_cross(N_tri, AB, AC);
      f3_normalize(N_tri, N_tri);

      f3_set(vertices + 0, A);
      f3_set(vertices + 3, B);
      f3_set(vertices + 6, C);

      CHK(s3d_scene_create(dev, &scn) == RES_OK);
      CHK(s3d_shape_create_mesh(dev, &msh) == RES_OK);
      CHK(s3d_scene_attach_shape(scn, msh) == RES_OK);

      vdata.usage = S3D_POSITION;
      vdata.type = S3D_FLOAT3;
      vdata.get = triangle_get_pos;
      CHK(s3d_mesh_setup_indexed_vertices
      (msh, 1, triangle_get_ids, 3, &vdata, 1, vertices) == RES_OK);

      CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);

      FOR_EACH(j, 0, BATCH_RAYS_PER_TRI) {
        float proj_j[3], tmp_j[3];
        float uj, vj, wj, hj;
        float dir_j[3];

        uj = 3 * rand_canonic() - 1;
        vj = 3 * rand_canonic() - 1;
        wj = 1 - uj - vj;
        hj = (2 * rand_canonic() - 1) * amplitude;

        /* proj = A + u*AB + v*AC */
        f3_add(proj_j, A, f3_add(proj_j,
          f3_mulf(proj_j, AB, uj),
          f3_mulf(tmp_j, AC, vj)));

        /* pos = proj + h*N */
        f3_add(tmp_j, proj_j, f3_mulf(tmp_j, N_tri, hj));

        /* dir = N * sign(-h), normalized */
        f3_mulf(dir_j, N_tri, (hj > 0 ? -1.f : 1.f));
        f3_normalize(dir_j, dir_j);

        f3_set(batch_origins + j * 3, tmp_j);
        f3_set(batch_dirs + j * 3, dir_j);
        batch_ranges[j * 2 + 0] = 0.f;
        batch_ranges[j * 2 + 1] = FLT_MAX;

        batch_u[j] = uj;
        batch_v[j] = vj;
        batch_w[j] = wj;
        batch_h[j] = hj;
        f3_set(batch_proj + j * 3, proj_j);
      }
      printf("Launch 1000 rays in parallel, it %d of %d\n", i + 100 * a, 100 * 16);
      CHK(s3d_scene_view_trace_rays(
        view,
        BATCH_RAYS_PER_TRI,
        0,
        batch_origins,
        batch_dirs,
        batch_ranges,
        NULL,
        0,
        batch_hits) == RES_OK);

      FOR_EACH(j, 0, BATCH_RAYS_PER_TRI) {
        float tmp_v[3];

        if(batch_u[j] < 0 || batch_v[j] < 0 || batch_w[j] < 0) {
          if(!S3D_HIT_NONE(&batch_hits[j]))
            CHK(batch_u[j] >= -FLT_EPSILON
             && batch_v[j] >= -FLT_EPSILON
             && batch_w[j] >= -FLT_EPSILON);
        } else {
          if(S3D_HIT_NONE(&batch_hits[j]))
            CHK(batch_u[j] <= FLT_EPSILON
             || batch_v[j] <= FLT_EPSILON
             || batch_w[j] <= FLT_EPSILON);
        }
        if(!S3D_HIT_NONE(&batch_hits[j])) {
          struct s3d_attrib attr;
          float d;
          CHK(eq_epsf(batch_hits[j].distance, fabsf(batch_h[j]), eps));
          CHK(s3d_primitive_get_attrib(
            &batch_hits[j].prim, S3D_POSITION, batch_hits[j].uv, &attr)
            == RES_OK);
          d = f3_len(f3_sub(tmp_v, attr.value, batch_proj + j * 3));
          CHK(d <= 10 * eps);
        }
      }

      CHK(s3d_shape_ref_put(msh) == RES_OK);
      CHK(s3d_scene_view_ref_put(view) == RES_OK);
      CHK(s3d_scene_ref_put(scn) == RES_OK);
    }
  }

#undef BATCH_RAYS_PER_TRI

  CHK(s3d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);

  return 0;
}

