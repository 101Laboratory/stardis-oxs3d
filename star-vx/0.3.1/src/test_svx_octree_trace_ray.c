/* Copyright (C) 2018, 2020-2025 |Méso|Star> (contact@meso-star.com)
 * Copyright (C) 2018 Université Paul Sabatier
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

#define _POSIX_C_SOURCE 200112L /* nextafter function */

#include "svx.h"
#include "test_svx_utils.h"

#include <rsys_math.h>

#include <rsys/clock_time.h>
#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/image.h>
#include <rsys/math.h>
#include <rsys/morton.h>

struct scene {
  double origin[3];
  double vxsz[3];
  double sphere_pos[3];
  double sphere_radius;
};

static char
sphere_intersect_aabb
  (const double pos[3], /* Sphere position */
   const double radius, /* Sphere radius */
   const double low[3], /* AABB lower bound */
   const double upp[3]) /* AABB upper bound */
{
  double v[3];
  double sqr_dst;

  CHK(pos != NULL);
  CHK(low != NULL);
  CHK(upp != NULL);
  CHK(radius > 0);

  v[0] = pos[0]<low[0] ? low[0]-pos[0] : (pos[0]>upp[0] ? pos[0]-upp[0] : 0);
  v[1] = pos[1]<low[1] ? low[1]-pos[1] : (pos[1]>upp[1] ? pos[1]-upp[1] : 0);
  v[2] = pos[2]<low[2] ? low[2]-pos[2] : (pos[2]>upp[2] ? pos[2]-upp[2] : 0);

  sqr_dst = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];

  return sqr_dst <= radius*radius;
}

static void
voxel_get(const size_t xyz[3], const uint64_t mcode, void* dst, void* ctx)
{
  const struct scene* scn = ctx;
  char* val = dst;
  double low[3];
  double upp[3];
  uint32_t ui3[3];
  CHK(xyz != NULL);
  CHK(dst != NULL);
  CHK(ctx != NULL);

  /* Compute the AABB of the voxel */
  low[0] = (double)xyz[0] * scn->vxsz[0] + scn->origin[0];
  low[1] = (double)xyz[1] * scn->vxsz[1] + scn->origin[1];
  low[2] = (double)xyz[2] * scn->vxsz[2] + scn->origin[2];
  upp[0] = low[0] + scn->vxsz[0];
  upp[1] = low[1] + scn->vxsz[1];
  upp[2] = low[2] + scn->vxsz[2];

  ui3[0] = (uint32_t)xyz[0];
  ui3[1] = (uint32_t)xyz[1];
  ui3[2] = (uint32_t)xyz[2];
  CHK(mcode == morton_xyz_encode_u21(ui3));

  /* Binary octree, i.e. it stores if the voxel intersect the sphere or not */
  *val = sphere_intersect_aabb(scn->sphere_pos, scn->sphere_radius, low, upp);
}

static void
voxels_merge(void* dst, const void* voxels[], const size_t nvoxels, void* ctx)
{
  size_t ivoxel;
  char tmp = 0;
  char* val = dst;

  CHK(dst && voxels && nvoxels && ctx);

  for(ivoxel=0; !tmp && ivoxel<nvoxels; ++ivoxel) {
    const char* voxel_data = voxels[ivoxel];
    tmp = *voxel_data;
  }
  *val = tmp;
}

static int
voxels_challenge_merge
  (const struct svx_voxel voxels[], const size_t nvoxels, void* ctx)
{
  size_t ivoxel;
  int merge = 1;
  char ref;

  CHK(voxels && nvoxels && ctx);

  ref = *(char*)(voxels[0].data);

  for(ivoxel=1; merge && ivoxel<nvoxels; ++ivoxel) {
    const char* voxel_data = voxels[ivoxel].data;
    merge = (ref == *voxel_data);
  }
  return merge;
}

static INLINE void
write_scalars
  (const struct svx_voxel* leaf,
   const size_t ileaf,
   void* context)
{
  FILE* stream = context;
  (void)ileaf;
  CHK(stream != NULL);
  CHK(leaf != NULL);
  fprintf(stream, "%d\n", *(char*)leaf->data);
}

static int
hit_filter
  (const struct svx_hit* hit,
   const double ray_org[3],
   const double ray_dir[3],
   const double ray_range[3],
   void* context)
{
  const struct ray* ray = context;

  CHK(hit && ray_org && ray_dir && ray_range && context);
  CHK(d3_eq(ray->org, ray_org));
  CHK(d3_eq(ray->dir, ray_dir));
  CHK(d2_eq(ray->range, ray_range));
  CHK(!SVX_HIT_NONE(hit));

  return *((char*)hit->voxel.data) == 0;
}

static int
hit_filter2
  (const struct svx_hit* hit,
   const double ray_org[3],
   const double ray_dir[3],
   const double ray_range[3],
   void* context)
{
  const struct svx_voxel* voxel = context;

  CHK(hit && ray_org && ray_dir && ray_range && context);
  CHK(!SVX_HIT_NONE(hit));
  return SVX_VOXEL_EQ(&hit->voxel, voxel)
      || *((char*)hit->voxel.data) == 0;
}

static int
hit_filter3
  (const struct svx_hit* hit,
   const double ray_org[3],
   const double ray_dir[3],
   const double ray_range[3],
   void* context)
{
  int* accum = context;
  CHK(hit && ray_org && ray_dir && ray_range && context);
  CHK(!SVX_HIT_NONE(hit));
  *accum += *(char*)hit->voxel.data;
  return 1;
}

static int
hit_challenge
  (const struct svx_hit* hit,
   const double ray_org[3],
   const double ray_dir[3],
   const double ray_range[2],
   void* context)
{
  (void)context;
  CHK(hit && ray_org && ray_dir && ray_range);
  CHK(!SVX_HIT_NONE(hit));
  return 1;
}

static int
hit_challenge_pass_through
  (const struct svx_hit* hit,
   const double ray_org[3],
   const double ray_dir[3],
   const double ray_range[2],
   void* context)
{
  const struct ray* ray = context;
  CHK(hit && ray_org && ray_dir && ray_range);
  CHK(!SVX_HIT_NONE(hit));
  CHK(d3_eq(ray->org, ray_org));
  CHK(d3_eq(ray->dir, ray_dir));
  CHK(d2_eq(ray->range, ray_range));
  return 0;
}

static int
hit_challenge_root
  (const struct svx_hit* hit,
   const double ray_org[3],
   const double ray_dir[3],
   const double ray_range[2],
   void* context)
{
  (void)hit, (void)ray_org, (void)ray_dir, (void)ray_range, (void)context;
  return hit->voxel.depth == 0;
}

static void
draw_image(struct image* img, struct svx_tree* oct, const struct scene* scn)
{
  char buf[32];
  struct time t0, t1;
  struct camera cam;
  unsigned char* pixels = NULL;
  const size_t width = 512;
  const size_t height = 512;
  const double pos[3] = { 0,-1.5, 0};
  const double tgt[3] = { 0, 0, 0};
  const double up[3] =  { 0, 0, 1};
  double pix[2];
  size_t ix, iy;

  CHK(oct && img);
  camera_init(&cam, pos, tgt, up, (double)width/(double)height);

  CHK(image_setup(img, width, height, sizeof_image_format(IMAGE_RGB8)*width,
    IMAGE_RGB8, NULL) == RES_OK);
  pixels = (unsigned char*)img->pixels;

  time_current(&t0);
  FOR_EACH(iy, 0, height) {
    pix[1] = (double)iy / (double)height;
    FOR_EACH(ix, 0, width) {
      struct svx_hit hit;
      struct ray r;
      size_t ipix = (iy*width + ix)*3/*#channels per pixel*/;
      pix[0] = (double)ix / (double)width;
      camera_ray(&cam, pix, &r);

      CHK(svx_tree_trace_ray(oct, r.org, r.dir, r.range,
        hit_challenge_pass_through, hit_filter, &r, &hit) == RES_OK);
      if(SVX_HIT_NONE(&hit)) {
        pixels[ipix+0] = 0;
        pixels[ipix+1] = 0;
        pixels[ipix+2] = 0;
      } else {
        double N[3];
        N[0] = (r.org[0] + hit.distance[0] * r.dir[0]) - scn->sphere_pos[0];
        N[1] = (r.org[1] + hit.distance[0] * r.dir[1]) - scn->sphere_pos[1];
        N[2] = (r.org[2] + hit.distance[0] * r.dir[2]) - scn->sphere_pos[2];
        CHK(d3_normalize(N, N) != 0);
        N[0] = fabs(N[0]);
        N[1] = fabs(N[1]);
        N[2] = fabs(N[2]);
        pixels[ipix+0] = (unsigned char)(N[0] * 255.0);
        pixels[ipix+1] = (unsigned char)(N[1] * 255.0);
        pixels[ipix+2] = (unsigned char)(N[2] * 255.0);
      }
    }
  }
  time_sub(&t0, time_current(&t1), &t0);
  time_dump(&t0, TIME_ALL, NULL, buf, sizeof(buf));
  fprintf(stderr, "Render time: %s\n", buf);
}

int
main(int argc, char** argv)
{
  struct scene scn;
  struct ray r;
  struct image img, img2;
  FILE* stream = NULL;
  struct svx_device* dev = NULL;
  struct svx_tree* oct = NULL;
  struct svx_tree* oct2 = NULL;
  struct svx_tree_desc tree_desc = SVX_TREE_DESC_NULL;
  struct svx_voxel_desc voxel_desc = SVX_VOXEL_DESC_NULL;
  struct svx_hit hit = SVX_HIT_NULL;
  struct svx_hit hit2 = SVX_HIT_NULL;
  const double lower[3] = {-1,-1,-1};
  const double upper[3] = { 1, 1, 1};
  const size_t def[3] = {32,32,32};
  double scnsz[3];
  double vxsz;
  double dst;
  int accum;
  (void)argc, (void)argv;

  CHK(svx_device_create(NULL, NULL, 1, &dev) == RES_OK);

  scnsz[0] = upper[0] - lower[0];
  scnsz[1] = upper[1] - lower[1];
  scnsz[2] = upper[2] - lower[2];

  scn.origin[0] = lower[0];
  scn.origin[1] = lower[1];
  scn.origin[2] = lower[2];
  scn.vxsz[0] = scnsz[0] / (double)def[0];
  scn.vxsz[1] = scnsz[1] / (double)def[1];
  scn.vxsz[2] = scnsz[2] / (double)def[2];
  scn.sphere_pos[0] = lower[0] + scnsz[0] * 0.5;
  scn.sphere_pos[1] = lower[1] + scnsz[1] * 0.5;
  scn.sphere_pos[2] = lower[2] + scnsz[2] * 0.5;
  scn.sphere_radius = MMIN(MMIN(scnsz[0], scnsz[1]), scnsz[2]) * 0.25;

  voxel_desc.get = voxel_get;
  voxel_desc.merge = voxels_merge;
  voxel_desc.challenge_merge = voxels_challenge_merge;
  voxel_desc.context = &scn;
  voxel_desc.size = sizeof(char);

  CHK(svx_octree_create(dev, lower, upper, def, &voxel_desc, &oct) == RES_OK);

  /* Duplicate the octree through serialization */
  CHK(stream = tmpfile());
  CHK(svx_tree_write(oct, stream) == RES_OK);
  CHK(svx_tree_create_from_stream(dev, stream, &oct2) == RES_BAD_ARG);
  rewind(stream);
  CHK(svx_tree_create_from_stream(dev, stream, &oct2) == RES_OK);
  fclose(stream);

  /*dump_data(stdout, oct, CHAR__, 1, write_scalars);*/

  #define RT svx_tree_trace_ray
  d3(r.org, -5,-5, 0);
  d3(r.dir,  0, 1, 0);
  d2(r.range, 0, INF);

  CHK(RT(NULL, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(oct, NULL, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(oct, r.org, NULL, r.range, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(oct, r.org, r.dir, NULL, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(oct, r.org, r.dir, r.range, NULL, NULL, NULL, NULL) == RES_BAD_ARG);

  CHK(RT(oct, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));

  r.org[0] = 0;
  CHK(RT(oct, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(eq_eps(hit.distance[0], hit.voxel.lower[1] - r.org[1], 1.e-6));
  CHK(eq_eps(hit.distance[1], hit.voxel.upper[1] - r.org[1], 1.e-6));
  CHK(hit.voxel.is_leaf);
  CHK(*(char*)hit.voxel.data == 0);

  /* Use challenge functor to intersect voxels that are not leaves */
  CHK(svx_tree_get_desc(oct, &tree_desc) == RES_OK);
  CHK(RT(oct, r.org, r.dir, r.range, hit_challenge, NULL, NULL, &hit2) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit2));
  CHK(!SVX_VOXEL_EQ(&hit.voxel, &hit2.voxel));
  CHK(hit2.voxel.is_leaf == 0);
  CHK(*(char*)hit2.voxel.data == 1);
  CHK(eq_eps(hit.distance[0], hit2.distance[0], 1.e-6));

  /* Use filter function to discard leaves with a value == 0 */
  CHK(RT(oct, r.org, r.dir, r.range, hit_challenge_pass_through, hit_filter,
    &r, &hit) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(eq_eps(hit.distance[0], hit.voxel.lower[1] - r.org[1], 1.e-6));
  CHK(eq_eps(hit.distance[1], hit.voxel.upper[1] - r.org[1], 1.e-6));
  CHK(hit.voxel.is_leaf);
  CHK(*(char*)hit.voxel.data == 1);

  /* Use the ray range to avoid the intersection with the closest voxel */
  r.range[1] = nextafter(hit.distance[0], -1);
  CHK(RT(oct, r.org, r.dir, r.range, hit_challenge_pass_through, hit_filter,
    &r, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));

  r.range[1] = INF;
  CHK(RT(oct, r.org, r.dir, r.range, NULL, hit_filter, &r, &hit) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(*(char*)hit.voxel.data == 1);
  CHK(hit.voxel.is_leaf);

  /* Use the ray range to discard the closest voxel */
  dst = hit.voxel.upper[1] - r.org[1];
  r.range[0] = nextafter(dst, DBL_MAX);
  CHK(RT(oct, r.org, r.dir, r.range, NULL, hit_filter, &r, &hit2) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit2));
  CHK(!SVX_VOXEL_EQ(&hit.voxel, &hit2.voxel));
  vxsz = hit2.voxel.upper[1] - hit2.voxel.lower[1];
  CHK(eq_eps(hit2.distance[0], r.range[0], 1.e-6));
  CHK(eq_eps(hit2.distance[1], dst + vxsz, 1.e-6));
  CHK(*(char*)hit.voxel.data == 1);
  CHK(hit.voxel.is_leaf);

  /* Adjust the ray range to hit the interior of the closest voxel */
  r.range[0] = nextafter(hit.distance[0], DBL_MAX);
  CHK(RT(oct, r.org, r.dir, r.range, NULL, hit_filter, &r, &hit2) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit2));
  CHK(SVX_VOXEL_EQ(&hit.voxel, &hit2.voxel));
  CHK(eq_eps(hit2.distance[0], r.range[0], 1.e-6));
  CHK(eq_eps(hit2.distance[1], dst, 1.e-6));
  CHK(*(char*)hit.voxel.data == 1);
  CHK(hit.voxel.is_leaf);

  /* Discard the closest voxel with the filter function */
  r.range[0] = 0;
  CHK(RT(oct, r.org, r.dir, r.range, NULL, hit_filter2, &hit.voxel, &hit2) == RES_OK);
  CHK(eq_eps(hit2.distance[0], hit2.voxel.lower[1] - r.org[1], 1.e-6));
  CHK(eq_eps(hit2.distance[1], hit2.voxel.upper[1] - r.org[1], 1.e-6));
  CHK(!SVX_HIT_NONE(&hit2));
  CHK(!SVX_VOXEL_EQ(&hit.voxel, &hit2.voxel));
  CHK(eq_eps(hit2.distance[0], dst, 1.e-6));
  CHK(*(char*)hit.voxel.data == 1);
  CHK(hit.voxel.is_leaf);

  /* Use the filter functor to accumulate the leaves */
  accum = 0;
  CHK(RT(oct, r.org, r.dir, r.range, NULL, hit_filter3, &accum, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));
  CHK(accum != 0);

  /* Check the root node challenge */
  CHK(RT(oct, r.org, r.dir, r.range, hit_challenge_root, NULL, NULL, &hit)
    == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(d3_eq_eps(hit.voxel.lower, lower, 1.e-6));
  CHK(d3_eq_eps(hit.voxel.upper, upper, 1.e-6));
  CHK(hit.voxel.depth == 0);
  CHK(hit.voxel.is_leaf == 0);
  CHK(*(char*)hit.voxel.data == 1);
  CHK(eq_eps(hit.distance[0], hit.voxel.lower[1] - r.org[1], 1.e-6));
  CHK(eq_eps(hit.distance[1], hit.voxel.upper[1] - r.org[1], 1.e-6));

  image_init(NULL, &img);
  image_init(NULL, &img2);
  draw_image(&img, oct, &scn);
  draw_image(&img2, oct2, &scn);

  /* Check that using oct or oct2 produces effectively the same image */
  check_img_eq(&img, &img2);

  /* Write the drawn image on stdout */
  CHK(image_write_ppm_stream(&img, 0/*binary*/, stdout) == RES_OK);
  image_release(&img);
  image_release(&img2);

  CHK(svx_tree_ref_put(oct) == RES_OK);
  CHK(svx_tree_ref_put(oct2) == RES_OK);
  CHK(svx_device_ref_put(dev) == RES_OK);
  CHK(mem_allocated_size() == 0);
  return 0;
}

