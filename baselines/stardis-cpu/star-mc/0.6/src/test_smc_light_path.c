/* Copyright (C) 2015-2018, 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#include "smc.h"
#include "test_smc_utils.h"

#include <star/s3d.h>
#include <star/ssp.h>

#include <rsys/float2.h>
#include <rsys/float3.h>
#include <rsys/image.h>
#include <rsys/math.h>
#include<rsys/mem_allocator.h>

#define IMG_WIDTH 640
#define IMG_HEIGHT 480
#define LIGHT_PATH_DEPTH 16
#define ALBEDO 0.6f
#define EPSILON 1.e-1f
#define GAMMA 2.2

struct cbox_desc{
  const float* vertices;
  const unsigned* indices;
};

/*******************************************************************************
 * Box walls data
 ******************************************************************************/
static const float cbox_walls[] = {
  552.f, 0.f,   0.f,
  0.f,   0.f,   0.f,
  0.f,   559.f, 0.f,
  552.f, 559.f, 0.f,
  552.f, 0.f,   548.f,
  0.f,   0.f,   548.f,
  0.f,   559.f, 548.f,
  552.f, 559.f, 548.f
};

const unsigned cbox_walls_ids[] = {
  0, 1, 2, 2, 3, 0, /* Bottom */
  4, 5, 6, 6, 7, 4, /* Top */
  1, 2, 6, 6, 5, 1, /* Left */
  0, 3, 7, 7, 4, 0, /* Right */
  2, 3, 7, 7, 6, 2  /* Back */
};

static const unsigned cbox_walls_ntris = sizeof(cbox_walls_ids)/(3*sizeof(unsigned));
static const unsigned cbox_walls_nverts = sizeof(cbox_walls)/(3*sizeof(float));
static struct cbox_desc cbox_walls_desc = { cbox_walls, cbox_walls_ids };

/*******************************************************************************
 * Short/tall blocks data
 ******************************************************************************/
static const float cbox_short_block[] = {
  130.f, 65.f,  0.f,
  82.f,  225.f, 0.f,
  240.f, 272.f, 0.f,
  290.f, 114.f, 0.f,
  130.f, 65.f,  165.f,
  82.f,  225.f, 165.f,
  240.f, 272.f, 165.f,
  290.f, 114.f, 165.f
};

static const float cbox_tall_block[] = {
  423.0f, 247.0f, 0.f,
  265.0f, 296.0f, 0.f,
  314.0f, 456.0f, 0.f,
  472.0f, 406.0f, 0.f,
  423.0f, 247.0f, 330.f,
  265.0f, 296.0f, 330.f,
  314.0f, 456.0f, 330.f,
  472.0f, 406.0f, 330.f
};

static const unsigned cbox_block_ids[] = {
  4, 5, 6, 6, 7, 4,
  1, 2, 6, 6, 5, 1,
  0, 3, 7, 7, 4, 0,
  2, 3, 7, 7, 6, 2,
  0, 1, 5, 5, 4, 0
};

static const unsigned cbox_block_ntris = sizeof(cbox_block_ids)/(3*sizeof(unsigned));

static const unsigned cbox_short_block_nverts =
  sizeof(cbox_short_block)/(3*sizeof(float));
static struct cbox_desc cbox_short_block_desc =
  { cbox_short_block, cbox_block_ids };

static const unsigned cbox_tall_block_nverts =
  sizeof(cbox_tall_block)/(3*sizeof(float));
static struct cbox_desc cbox_tall_block_desc =
  { cbox_tall_block, cbox_block_ids };

/*******************************************************************************
 * Cornell box callbacks
 ******************************************************************************/
static INLINE void
cbox_get_ids(const unsigned itri, unsigned ids[3], void* data)
{
  const unsigned id = itri * 3;
  struct cbox_desc* desc = data;
  CHK(desc != NULL);
  ids[0] = desc->indices[id + 0];
  ids[1] = desc->indices[id + 1];
  ids[2] = desc->indices[id + 2];
}

static INLINE void
cbox_get_position(const unsigned ivert, float position[3], void* data)
{
  struct cbox_desc* desc = data;
  CHK(desc != NULL);
  position[0] = desc->vertices[ivert*3 + 0];
  position[1] = desc->vertices[ivert*3 + 1];
  position[2] = desc->vertices[ivert*3 + 2];
}

/*******************************************************************************
 * Camera
 ******************************************************************************/
struct camera {
  float pos[3];
  float x[3], y[3], z[3]; /* Basis */
};

static void
camera_init(struct camera* cam)
{
  const float pos[3] = { 178.f, -1000.f, 273.f };
  const float tgt[3] = { 178.f, 0.f, 273.f };
  const float up[3] = { 0.f, 0.f, 1.f };
  const float proj_ratio = (float)IMG_WIDTH/(float)IMG_HEIGHT;
  const float fov_x = (float)PI * 0.25f;
  float f = 0.f;
  ASSERT(cam);

  f3_set(cam->pos, pos);
  f = f3_normalize(cam->z, f3_sub(cam->z, tgt, pos)); CHK(f != 0);
  f = f3_normalize(cam->x, f3_cross(cam->x, cam->z, up)); CHK(f != 0);
  f = f3_normalize(cam->y, f3_cross(cam->y, cam->z, cam->x)); CHK(f != 0);
  f3_divf(cam->z, cam->z, (float)tan(fov_x*0.5f));
  f3_divf(cam->y, cam->y, proj_ratio);
}

static void
camera_ray
  (const struct camera* cam,
   const float pixel[2],
   float org[3],
   float dir[3])
{
  float x[3], y[3], f;
  ASSERT(cam && pixel && org && dir);

  f3_mulf(x, cam->x, pixel[0]*2.f - 1.f);
  f3_mulf(y, cam->y, pixel[1]*2.f - 1.f);
  f3_add(dir, f3_add(dir, x, y), cam->z);
  f = f3_normalize(dir, dir); CHK(f != 0);
  f3_set(org, cam->pos);
}

/*******************************************************************************
 * Integrator
 ******************************************************************************/
struct integrator_context {
  struct s3d_scene_view* view;
  struct camera* cam;
  float pixel_size[2]; /* Normalized pixel size */
  size_t ipixel[2]; /* Image space pixel coordinates */
};

static float
direct_lighting
  (struct s3d_scene_view* view, const float pos[3], const float N[3])
{
  const float light_pos[3] = { 200.f, 200.f, 400.f };
  const float flux = 60.0; /* Radiant flux in watt */
  const float intensity = (float)(flux/(4.0*PI)); /*Radiant intensity in W/sr*/
  struct s3d_hit hit;
  float len;
  float wi[3];
  float range[2];

  CHK(view != NULL);
  CHK(pos != NULL);
  CHK(N != NULL);
  CHK(f3_is_normalized(N) == 1);

  f3_sub(wi, light_pos, pos);
  len = f3_normalize(wi, wi);

  /* Trace shadow ray */
  range[0] = EPSILON;
  range[1] = len;
  CHK(s3d_scene_view_trace_ray(view, pos, wi, range, NULL, &hit) == RES_OK);
  if(!S3D_HIT_NONE(&hit)) return 0.f; /* Light is occluded */

  len *= 0.01f; /* Transform len from centimer to meter */
  return
    intensity / (len*len)  /* radiance in W/(sr.m^2) */
  * (float)(ALBEDO / PI) /* BRDF */
  * f3_dot(wi, N); /* cos(wi,N) */
}

static float
skydome_lighting(const float wi[3])
{
  const float ground_irradiance = 0.01f;
  const float sky_irradiance = 12.0f;
  float u;

  CHK(wi != NULL);
  u = CLAMP(wi[2], 0.f, 0.05f) * 1.f;
  return u * sky_irradiance + (1.f - u) * ground_irradiance;
}

static res_T
light_path_integrator
  (void* value,
   struct ssp_rng* rng,
   const unsigned ithread,
   const uint64_t irealisation,
   void* data)
{
  struct integrator_context* ctx = data;
  float pix_lower[2], pix_upper[2]; /* Pixel AABB */
  float pix_samp[2]; /* Pixel sample */
  float ray_org[3], ray_dir[3], ray_range[2];
  float L = 0.f;
  float throughput = 1.f;
  int idepth;

  (void)ithread, (void)irealisation;

  CHK(value != NULL);
  CHK(rng != NULL);
  CHK(data != NULL);

  /* Compute the pixel bound */
  pix_lower[0] = (float)ctx->ipixel[0] / (float)IMG_WIDTH;
  pix_lower[1] = (float)ctx->ipixel[1] / (float)IMG_HEIGHT;
  f2_add(pix_upper, pix_lower, ctx->pixel_size);

  /* Randomly sample the pixel quad */
  pix_samp[0] = (float)ssp_rng_uniform_double(rng, pix_lower[0], pix_upper[0]);
  pix_samp[1] = (float)ssp_rng_uniform_double(rng, pix_lower[1], pix_upper[1]);

  /* Build a camera ray */
  camera_ray(ctx->cam, pix_samp, ray_org, ray_dir);
  ray_range[0] = 0.f;
  ray_range[1] = FLT_MAX;

  FOR_EACH(idepth, 0, LIGHT_PATH_DEPTH) {
    struct s3d_hit hit = S3D_HIT_NULL;
    float cos_theta;
    float pdf;
    float pos[3];
    float N[3] = {0, 0, 0};

    CHK(s3d_scene_view_trace_ray
      (ctx->view, ray_org, ray_dir, ray_range, NULL, &hit) == RES_OK);

    if(S3D_HIT_NONE(&hit)) { /* Skydome lighting */
      L += throughput * skydome_lighting(ray_dir);
      break;
    }

    f3_normalize(N, hit.normal);
    cos_theta = f3_dot(N, ray_dir);
    if(cos_theta >= 0.f) f3_minus(N, N);
    f3_add(pos, f3_mulf(pos, ray_dir, hit.distance), ray_org);

    /* Direct lighting */
    L += throughput * direct_lighting(ctx->view, pos, N);

    /* New ray */
    ssp_ran_hemisphere_cos_float(rng, N, ray_dir, &pdf);
    cos_theta = f3_dot(N, ray_dir);
    f3_set(ray_org, pos);
    ray_range[0] = EPSILON;
    throughput *= (float)(ALBEDO / PI) / pdf * cos_theta;
  }
  *(float*)value = L;
  return RES_OK;
}

/*******************************************************************************
 * Application
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct image img;
  FILE* fp = NULL;
  struct integrator_context* contexts;
  struct s3d_device* dev;
  struct s3d_scene* scn;
  struct s3d_scene_view* view;
  struct s3d_shape* shape;
  struct s3d_vertex_data attrib;
  struct smc_device_create_args args = SMC_DEVICE_CREATE_ARGS_DEFAULT;
  struct smc_device* smc;
  struct smc_integrator integrator = SMC_INTEGRATOR_NULL;
  struct smc_estimator** estimators;
  struct camera cam;
  size_t ix, iy;
  float pos[3];
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(image_init(&allocator, &img) == RES_OK);
  CHK(image_setup
    (&img, IMG_WIDTH, IMG_HEIGHT, 3*IMG_WIDTH, IMAGE_RGB8, NULL) == RES_OK);

  CHK(s3d_device_create(NULL, &allocator, 0, &dev) == RES_OK);
  CHK(s3d_scene_create(dev, &scn) == RES_OK);

  attrib.usage = S3D_POSITION;
  attrib.type = S3D_FLOAT3;
  attrib.get = cbox_get_position;

  CHK(s3d_shape_create_mesh(dev, &shape) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(shape, cbox_walls_ntris, cbox_get_ids,
    cbox_walls_nverts, &attrib, 1, &cbox_walls_desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);

  CHK(s3d_shape_create_mesh(dev, &shape) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(shape, cbox_block_ntris, cbox_get_ids,
    cbox_short_block_nverts, &attrib, 1, &cbox_short_block_desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);

  CHK(s3d_shape_create_mesh(dev, &shape) == RES_OK);
  CHK(s3d_mesh_setup_indexed_vertices(shape, cbox_block_ntris, cbox_get_ids,
    cbox_tall_block_nverts, &attrib, 1, &cbox_tall_block_desc) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);

  CHK(s3d_scene_instantiate(scn, &shape) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(s3d_instance_set_position(shape, f3(pos, -100.f, 0.f, -2.f)) == RES_OK);

  CHK(s3d_scene_create(dev, &scn) == RES_OK);
  CHK(s3d_scene_attach_shape(scn, shape) == RES_OK);
  CHK(s3d_shape_ref_put(shape) == RES_OK);

  CHK(s3d_scene_view_create(scn, S3D_TRACE, &view) == RES_OK);

  args.allocator = &allocator;
  args.verbose = 1;
  CHK(smc_device_create(&args, &smc) == RES_OK);

  integrator.integrand = light_path_integrator;
  integrator.type = &smc_float;
  integrator.max_realisations = 64;

  contexts = MEM_CALLOC
    (&allocator, IMG_WIDTH*IMG_HEIGHT, sizeof(struct integrator_context));
  CHK(contexts != NULL);
  estimators = MEM_CALLOC
    (&allocator, IMG_WIDTH*IMG_HEIGHT, sizeof(struct smc_estimator*));
  CHK(estimators != NULL);

  camera_init(&cam);

  FOR_EACH(iy, 0, IMG_HEIGHT) {
  FOR_EACH(ix, 0, IMG_WIDTH) {
    const size_t ictx = iy * IMG_WIDTH + ix;
    contexts[ictx].view = view;
    contexts[ictx].cam = &cam;
    contexts[ictx].pixel_size[0] = 1.f / (float)IMG_WIDTH;
    contexts[ictx].pixel_size[1] = 1.f / (float)IMG_HEIGHT;
    contexts[ictx].ipixel[0] = ix;
    contexts[ictx].ipixel[1] = iy;
  }}

  CHK(smc_solve_N(smc, &integrator, IMG_WIDTH * IMG_HEIGHT, contexts,
    sizeof(struct integrator_context), estimators) == RES_OK);

  FOR_EACH(iy, 0, IMG_HEIGHT) {
  FOR_EACH(ix, 0, IMG_WIDTH) {
    const size_t iestimator = (iy*IMG_WIDTH + ix);
    uint8_t* pix = (uint8_t*)(img.pixels + iy*img.pitch + ix*3/*RGB*/);
    struct smc_estimator_status status;
    float col;
    unsigned char colu;

    CHK(smc_estimator_get_status(estimators[iestimator], &status) == RES_OK);
    col = (float)pow(SMC_FLOAT(status.E), 1.0/GAMMA); /* Gamma correction */
    colu = (uint8_t)(CLAMP(col, 0.f, 1.f) * 255.f); /* Float to U8 */
    pix[0] = pix[1] = pix[2] = colu;
    CHK(smc_estimator_ref_put(estimators[iestimator]) == RES_OK);
  }}

  CHK(fp = fopen("image.ppm", "w"));
  image_write_ppm_stream(&img, 0, fp);
  CHK(fclose(fp) == 0);

  CHK(image_release(&img) == RES_OK);
  MEM_RM(&allocator, contexts);
  MEM_RM(&allocator, estimators);

  CHK(s3d_scene_view_ref_put(view) == RES_OK);

  CHK(s3d_device_ref_put(dev) == RES_OK);
  CHK(s3d_scene_ref_put(scn) == RES_OK);
  CHK(smc_device_ref_put(smc) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
