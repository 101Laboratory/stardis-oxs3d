/* Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#include "scam.h"
#include "test_scam_cbox.h"
#include "test_scam_utils.h"

#include <star/s3d.h>

#include <rsys/double3.h>
#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/image.h>
#include<rsys/mem_allocator.h>

#include <string.h>

#define IMG_WIDTH 640
#define IMG_HEIGHT 480
#define IMG_SPP 16

enum lens_model {
  PINHOLE,
  THIN_LENS
};

enum camera_model {
  ORTHOGRAPHIC,
  PERSPECTIVE
};

static struct s3d_scene_view*
create_s3d_scene_view(void)
{
  struct s3d_vertex_data vdata;
  struct s3d_device* s3d = NULL;
  struct s3d_scene* scn = NULL;
  struct s3d_shape* shape = NULL;
  struct s3d_scene_view* view = NULL;

  CHK(s3d_device_create(NULL, NULL, 0, &s3d) == RES_OK);

  vdata.usage = S3D_POSITION;
  vdata.type = S3D_FLOAT3;
  vdata.get = cbox_get_vtx;
  CHK(s3d_shape_create_mesh(s3d, &shape) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(shape, cbox_ntris, cbox_get_tri,
    cbox_nvtxs, &vdata, 1, NULL) == RES_OK);

  CHK(s3d_scene_create(s3d, &scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);

  CHK(s3d_device_ref_put(s3d) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);

  return view;
}

static void
create_perspective(const enum lens_model lens_model, struct scam** cam)
{
  struct scam_perspective_args args = SCAM_PERSPECTIVE_ARGS_DEFAULT;
  CHK(cam);

  args.position[0] = 89.66;
  args.position[1] = 21.89;
  args.position[2] = 202.22;
  args.target[0] = 150.03;
  args.target[1] = 135;
  args.target[2] = 196.87;
  args.up[0] = 0;
  args.up[1] = 0;
  args.up[2] = 1;
  args.field_of_view = 1.22173047639603070383; /* ~70 degrees */
  args.aspect_ratio = (double)IMG_WIDTH/(double)IMG_HEIGHT;

  switch(lens_model) {
    case PINHOLE: 
      args.lens_radius = 0;
      break;
    case THIN_LENS:
      args.lens_radius = 5;
      args.focal_distance = 300;
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

  CHK(scam_create_perspective(NULL, NULL, 1, &args, cam) == RES_OK);
}

static void
create_orthographic(const enum lens_model lens_model, struct scam** cam)
{
  struct scam_orthographic_args args = SCAM_ORTHOGRAPHIC_ARGS_DEFAULT;
  CHK(cam);

  args.position[0] = 89.66;
  args.position[1] = 21.89;
  args.position[2] = 202.22;
  args.target[0] = 150.03;
  args.target[1] = 135;
  args.target[2] = 196.87;
  args.up[0] = 0;
  args.up[1] = 0;
  args.up[2] = 1;
  args.height = 500;
  args.aspect_ratio = (double)IMG_WIDTH/(double)IMG_HEIGHT;

  /* No thin-lens model for orthographic projection */
  CHK(lens_model == PINHOLE);

  CHK(scam_create_orthographic(NULL, NULL, 1, &args, cam) == RES_OK);
}


static void
draw
  (const struct scam* cam,
   struct s3d_scene_view* view,
   struct image* img)
{
  int x, y, s;
  double pixsz[2];
  CHK(img);

  pixsz[0] = 1.0 / (double)IMG_WIDTH;
  pixsz[1] = 1.0 / (double)IMG_HEIGHT;

  FOR_EACH(y, 0, IMG_HEIGHT) {
    FOR_EACH(x, 0, IMG_WIDTH) {
      double sum[3] = {0,0,0};
      double E[3] = {0,0,0};
      int ipix;

      FOR_EACH(s, 0, IMG_SPP) {
        struct s3d_hit hit = S3D_HIT_NULL;
        struct scam_sample sample = SCAM_SAMPLE_NULL;
        struct scam_ray ray = SCAM_RAY_NULL;
        double col[3];
        float org[3];
        float dir[3];
        float range[2];
        float N[3];
        float cos_N_dir;

        /* Generate the camera ray */
        sample.film[0] = ((double)x + rand_canonical()) * pixsz[0];
        sample.film[1] = ((double)y + rand_canonical()) * pixsz[1];
        sample.lens[0] = rand_canonical();
        sample.lens[1] = rand_canonical();
        CHK(scam_generate_ray(cam, &sample, &ray) == RES_OK);

        /* Trace the ray */
        f3_set_d3(org, ray.org);
        f3_normalize(dir, f3_set_d3(dir, ray.dir));
        f2(range, 0, (float)INF);
        CHK(s3d_scene_view_trace_ray(view, org, dir, range, NULL, &hit) == RES_OK);

        if(S3D_HIT_NONE(&hit)) continue;

        /* Shade */
        cbox_get_col(hit.prim.prim_id, col);
        f3_normalize(N, hit.normal);
        cos_N_dir = absf(f3_dot(N, dir));

        sum[0] += (float)(cos_N_dir * col[0]);
        sum[1] += (float)(cos_N_dir * col[1]);
        sum[2] += (float)(cos_N_dir * col[2]);
      }
      /* Compute pixel color */
      E[0] = sum[0] / (double)IMG_SPP;
      E[1] = sum[1] / (double)IMG_SPP;
      E[2] = sum[2] / (double)IMG_SPP;

      /* Write pixel color */
      ipix = (y*IMG_WIDTH + x) * 3/*RGB*/;
      ((uint8_t*)img->pixels)[ipix+0] = (uint8_t)(E[0] * 255);
      ((uint8_t*)img->pixels)[ipix+1] = (uint8_t)(E[1] * 255);
      ((uint8_t*)img->pixels)[ipix+2] = (uint8_t)(E[2] * 255);
    }
  }
}

int
main(int argc, char** argv)
{
  struct s3d_scene_view* view = NULL;
  struct scam* cam = NULL;
  struct image img;
  enum camera_model cam_model;
  enum lens_model lens_model;
  int err = 0;

  if(argc <= 2) {
    fprintf(stderr,
      "Usage: %s <orthographic|perspective> <pinhole|thin-lens>\n",
      argv[0]);
    goto error;
  }

  if(!strcmp(argv[1], "orthographic")) {
    cam_model = ORTHOGRAPHIC;
  } else if(!strcmp(argv[1], "perspective")) {
    cam_model = PERSPECTIVE;
  } else {
    fprintf(stderr, "Invalid camera model `%s'.\n", argv[1]);
    goto error;
  }

  if(!strcmp(argv[2], "pinhole")){
    lens_model = PINHOLE;
  } else if(!strcmp(argv[2], "thin-lens")) {
    lens_model = THIN_LENS;
  } else {
    fprintf(stderr, "Invalid lens model `%s'.\n", argv[2]);
    goto error;
  }

  view = create_s3d_scene_view();

  switch(cam_model) {
    case ORTHOGRAPHIC: create_orthographic(lens_model, &cam); break;
    case PERSPECTIVE: create_perspective(lens_model, &cam); break;
    default: FATAL("Unreachable code.\n"); break;
  }

  image_init(NULL, &img);
  CHK(image_setup
    (&img, IMG_WIDTH, IMG_HEIGHT, IMG_WIDTH*3, IMAGE_RGB8, NULL) == RES_OK);

  draw(cam, view, &img);

  CHK(image_write_ppm_stream(&img, 0, stdout) == RES_OK);

  CHK(s3d_scene_view_ref_put(view) == RES_OK);
  CHK(scam_ref_put(cam) == RES_OK);
  image_release(&img);

exit:
  CHK(mem_allocated_size() == 0);
  return err;
error:
  err = 1;
  goto exit;
}
