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
 * along with this program. If not, see <http://www.gnu.r.org/licenses/>. */

#include "svx.h"
#include "test_svx_utils.h"

#include <rsys/image.h>
#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/math.h>

#include <rsys_math.h>

struct scene {
  enum svx_axis axis;
  double origin;
  double vxsz;
  double range_center;
  double range_half_len;
};

static double
rand_canonic(void)
{
  return (double)rand() / (double)((size_t)RAND_MAX + 1);
}

static void
voxel_get(const size_t xyz[3], const uint64_t mcode, void* dst, void* ctx)
{
  const struct scene* scn = ctx;
  char* val = dst;
  double low;
  double upp;
  double dst1, dst2;
  double min_dst, max_dst;
  CHK(xyz != NULL);
  CHK(dst != NULL);
  CHK(ctx != NULL);
  CHK(mcode == xyz[scn->axis]);

  /* Compute the range of the voxel */
  low = (double)xyz[scn->axis] * scn->vxsz + scn->origin;
  upp = low + scn->vxsz;

  /* Compute the distance from the voxel boundary to the range_center */
  dst1 = fabs(scn->range_center - low);
  dst2 = fabs(scn->range_center - upp);
  min_dst = MMIN(dst1, dst2);
  max_dst = MMAX(dst1, dst2);

  /* Define if the voxel intersects a range boundary */
  *val = min_dst <= scn->range_half_len
      && max_dst >= scn->range_half_len;
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

static int
hit_challenge_pass_through
  (const struct svx_hit* hit,
   const double ray_org[3],
   const double ray_dir[3],
   const double ray_range[2],
   void* context)
{
  const struct ray* ray = context;
  CHK(hit && ray_org && ray_dir && ray_range && context);
  CHK(!SVX_HIT_NONE(hit));
  CHK(d3_eq(ray->org, ray_org));
  CHK(d2_eq(ray->range, ray_range));

  /* The direction could be adjusted internally when crossing a binary tree.
   * This avoids numerical inaccuracies in the case of distant intersections,
   * i.e. when the ray direction is roughly aligned with the third dimension
   * (i.e. the one that goes to infinity). In this case, the ray direction is
   * forced to be 0 on this dimension before being renormalized. The ray
   * therefore never intersects the binary tree. But its direction has changed.
   * Hence the approximate equality on the direction */
  CHK(d3_eq_eps(ray->dir, ray_dir, 1.e-6));

  return 0;
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
  CHK(d2_eq(ray->range, ray_range));
  CHK(!SVX_HIT_NONE(hit));

  /* The direction could be adjusted internally when crossing a binary tree.
   * Refer to the hit_challenge_pass_through function */
  CHK(d3_eq_eps(ray->dir, ray_dir, 1.e-6));

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

static void
draw_image(struct image* img, struct svx_tree* btree)
{
  struct camera cam;
  unsigned char* pixels = NULL;
  const size_t width = 512;
  const size_t height = 512;
  const double pos[3] = { 0, 0, 0};
  const double tgt[3] = { 0, 0, 1};
  const double up[3] =  { 1, 0, 0};
  double pix[2];
  size_t ix, iy;

  CHK(btree && img);
  camera_init(&cam, pos, tgt, up, (double)width/(double)height);

  CHK(image_setup(img, width, height, sizeof_image_format(IMAGE_RGB8)*width,
    IMAGE_RGB8, NULL) == RES_OK);
  pixels = (unsigned char*)img->pixels;

  FOR_EACH(iy, 0, height) {
    pix[1] = (double)iy / (double)height;
    FOR_EACH(ix, 0, width) {
      struct svx_hit hit;
      struct ray r;
      size_t ipix = (iy*width + ix)*3/*#channels per pixel*/;
      pix[0] = (double)ix / (double)width;
      camera_ray(&cam, pix, &r);

      CHK(svx_tree_trace_ray(btree, r.org, r.dir, r.range,
        hit_challenge_pass_through, hit_filter, &r, &hit) == RES_OK);
      pixels[ipix+0] = 0;
      pixels[ipix+1] = 0;
      pixels[ipix+2] = 0;

      if(!SVX_HIT_NONE(&hit)) {
        double N[3] = {1, 0, 0};
        const double dot = d3_dot(N, r.dir);
        if(dot < 0) {
          pixels[ipix+0] =(unsigned char)(255 * -dot);
        } else {
          pixels[ipix+2] =(unsigned char)(255 * dot);
        }
      }
    }
  }
}

int
main(int argc, char** argv)
{
  struct image img, img2;
  FILE* stream = NULL;
  struct svx_device* dev = NULL;
  struct svx_tree* btree = NULL;
  struct svx_tree* btree2 = NULL;
  struct svx_voxel_desc voxel_desc = SVX_VOXEL_DESC_NULL;
  struct svx_voxel voxel = SVX_VOXEL_NULL;
  struct svx_hit hit;
  struct svx_hit hit2;
  struct scene scn;
  const double lower = -1;
  const double upper =  1;
  const size_t definition = 32;
  const double scnsz = upper - lower;
  struct ray r;
  int accum;
  (void)argc, (void)argv;

  scn.origin = lower;
  scn.vxsz = scnsz / (double)definition;
  scn.range_center = lower + scnsz*0.5;
  scn.range_half_len = scnsz*0.25;
  scn.axis = SVX_AXIS_X;

  CHK(svx_device_create(NULL, NULL, 1, &dev) == RES_OK);
  voxel_desc.get = voxel_get;
  voxel_desc.merge = voxels_merge;
  voxel_desc.challenge_merge = voxels_challenge_merge;
  voxel_desc.context = &scn;
  voxel_desc.size = sizeof(char);

  CHK(svx_bintree_create(dev, lower, upper, definition, scn.axis,
    &voxel_desc, &btree) == RES_OK);

  /* Duplicate the binary tree through serialization */
  CHK(stream = tmpfile());
  CHK(svx_tree_write(btree, stream) == RES_OK);
  CHK(svx_tree_create_from_stream(dev, stream, &btree2) == RES_BAD_ARG);
  rewind(stream);
  CHK(svx_tree_create_from_stream(dev, stream, &btree2) == RES_OK);
  fclose(stream);

  #define RT svx_tree_trace_ray
  d3(r.org, -1.01, 0, 0);
  d3(r.dir, -1, 0, 0);
  d2(r.range, 0, DBL_MAX);

  CHK(RT(NULL, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(btree, NULL, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(btree, r.org, NULL, r.range, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(btree, r.org, r.dir, NULL, NULL, NULL, NULL, &hit) == RES_BAD_ARG);
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, NULL) == RES_BAD_ARG);

  /* Check failed hits */
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));
  d3(r.org, 1.01, 0, 0);
  d3(r.dir, 1, 0, 0);
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));
  d3(r.dir, 0, 1, 0);
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));
  d3(r.dir, 0, 0, -1);
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));

  /* Check first hit */
  d3(r.org, -1.01, 0, 0);
  d3(r.dir, 1, 0, 0);
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(eq_eps(hit.distance[0], 0.01, 1.e-6));
  CHK(eq_eps(hit.distance[0], hit.voxel.lower[0] - r.org[0], 1.e-6));
  CHK(eq_eps(hit.distance[1], hit.voxel.upper[0] - r.org[0], 1.e-6));
  CHK(hit.voxel.is_leaf);
  CHK(hit.voxel.depth == 3);
  CHK(*(char*)hit.voxel.data == 0);
  CHK(IS_INF(hit.voxel.lower[1]));
  CHK(IS_INF(hit.voxel.lower[2]));
  CHK(IS_INF(hit.voxel.upper[1]));
  CHK(IS_INF(hit.voxel.upper[2]));

  /* Check first hit with negative ray */
  d3(r.org, 1.01, 1234, 10);
  d3(r.dir, -1, 0, 0);
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, &hit2) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit2));
  CHK(eq_eps(hit2.distance[0], 0.01, 1.e-6));
  CHK(eq_eps(hit2.distance[0], r.org[0] - hit2.voxel.upper[0], 1.e-6));
  CHK(eq_eps(hit2.distance[1], r.org[0] - hit2.voxel.lower[0], 1.e-6));
  CHK(hit2.voxel.is_leaf);
  CHK(*(char*)hit2.voxel.data == 0);
  CHK(eq_eps(hit2.distance[0], hit2.distance[0], 1.e-6));
  CHK(eq_eps(hit2.distance[1], hit2.distance[1], 1.e-6));

  /* Check first hit with negative ray and and a non orthognal r.dir */
  d3_normalize(r.dir, d3(r.dir, -1, -1, -1));
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(SVX_VOXEL_EQ(&hit.voxel, &hit2.voxel));
  CHK(eq_eps(hit.distance[0], (hit.voxel.upper[0]-r.org[0])/r.dir[0], 1.e-4));
  CHK(eq_eps(hit.distance[1], (hit.voxel.lower[0]-r.org[0])/r.dir[0], 1.e-4));
  CHK(eq_eps(hit.voxel.upper[0], 1, 1.e-6));
  CHK(eq_eps(hit.voxel.lower[0], 1-2*(1/(double)(1<<hit.voxel.depth)), 1.e-6));
  CHK(hit.voxel.is_leaf);
  CHK(hit.voxel.depth == 3);
  CHK(*(char*)hit.voxel.data == 0);

  /* Use challenge functor to intersect voxels that are note leaves */
  d3(r.org, 2, 1, 0);
  d3(r.dir, -rand_canonic(), rand_canonic()*2-1, rand_canonic()*2-1);
  d3_normalize(r.dir, r.dir);
  CHK(RT(btree, r.org, r.dir, r.range, hit_challenge, NULL, NULL, &hit)==RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(!SVX_VOXEL_EQ(&hit.voxel, &hit2.voxel));
  CHK(hit.voxel.is_leaf == 0);
  CHK(hit.voxel.depth < 3);
  CHK(*(char*)hit.voxel.data == 1);
  CHK(eq_eps(hit.distance[0], (hit.voxel.upper[0]-r.org[0])/r.dir[0], 1.e-6));
  CHK(eq_eps(hit.distance[1], (hit.voxel.lower[0]-r.org[0])/r.dir[0], 1.e-6));

  /* Use filter function to discard leaves with a value == 0 */
  CHK(RT(btree, r.org, r.dir, r.range, hit_challenge_pass_through, hit_filter,
    &r, &hit) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(hit.voxel.is_leaf == 1);
  CHK(hit.voxel.depth == 5);
  CHK(*(char*)hit.voxel.data == 1);
  CHK(eq_eps(hit.distance[0], (hit.voxel.upper[0]-r.org[0])/r.dir[0], 1.e-4));
  CHK(eq_eps(hit.distance[1], (hit.voxel.lower[0]-r.org[0])/r.dir[0], 1.e-4));
  CHK(eq_eps(hit.voxel.lower[0], 0.5, 1.e-6));
  CHK(eq_eps(hit.voxel.upper[0], 0.5+2.0/32.0, 1.e-6));

  /* Use the ray range to avoid the intersection with the closest voxel */
  r.range[1] = hit.distance[0] - 1.e-6;
  CHK(RT(btree, r.org, r.dir, r.range, NULL, hit_filter, &r, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));
  r.range[1] = INF;

  CHK(RT(btree, r.org, r.dir, r.range, NULL, hit_filter, &r, &hit) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));

  /* Use the ray range to discard the closest voxel */
  r.range[0] = hit.distance[1] + 1.e-6;
  CHK(RT(btree, r.org, r.dir, r.range, NULL, hit_filter, &r, &hit2) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit2));
  CHK(!SVX_VOXEL_EQ(&hit.voxel, &hit2.voxel));
  CHK(hit2.voxel.is_leaf == 1);
  CHK(hit2.voxel.depth == 5);
  CHK(*(char*)hit2.voxel.data == 1);
  CHK(eq_eps(hit2.distance[0], (hit2.voxel.upper[0]-r.org[0])/r.dir[0], 1.e-4));
  CHK(eq_eps(hit2.distance[1], (hit2.voxel.lower[0]-r.org[0])/r.dir[0], 1.e-4));
  CHK(eq_eps(hit2.voxel.lower[0], 0.5-2.0/32.0, 1.e-6));
  CHK(eq_eps(hit2.voxel.upper[0], 0.5, 1.e-6));

  /* Adjust the ray range to intersect the interior of the closest voxel */
  r.range[0] = hit.distance[0] + 1.e-6;
  CHK(RT(btree, r.org, r.dir, r.range, NULL, hit_filter, &r, &hit2) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit2));
  CHK(SVX_VOXEL_EQ(&hit.voxel, &hit2.voxel));
  CHK(eq_eps(hit2.distance[0], r.range[0], 1.e-6));
  CHK(eq_eps(hit2.distance[1], hit.distance[1], 1.e-6));

  /* Discard the closest voxel != 0 with the filter function */
  d2(r.range, 0, INF);
  CHK(RT(btree, r.org, r.dir, r.range, NULL, hit_filter2, &hit.voxel, &hit2) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit2));
  CHK(!SVX_VOXEL_EQ(&hit.voxel, &hit2.voxel));
  CHK(eq_eps(hit2.distance[0], (hit2.voxel.upper[0]-r.org[0])/r.dir[0], 1.e-4));
  CHK(eq_eps(hit2.distance[1], (hit2.voxel.lower[0]-r.org[0])/r.dir[0], 1.e-4));
  CHK(eq_eps(hit2.voxel.lower[0], 0.5-2.0/32.0, 1.e-6));
  CHK(eq_eps(hit2.voxel.upper[0], 0.5, 1.e-6));

  /* Use the filter functor to accumulate the leaves */
  accum = 0;
  CHK(RT(btree, r.org, r.dir, r.range, NULL, hit_filter3, &accum, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));
  CHK(accum == 4);

  /* Check ray with null dir along the btree axis */
  d2(r.range, 0, INF);
  d3(r.org, -0.875, 123, 456);
  d3(r.dir, 0, rand_canonic()*2-1, rand_canonic()*2-1);
  d3_normalize(r.dir, r.dir);
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(eq_eps(hit.distance[0], r.range[0], 1.e-6));
  CHK(IS_INF(hit.distance[1]));
  CHK(hit.voxel.is_leaf == 1);
  CHK(hit.voxel.depth == 3);
  CHK(svx_tree_at(btree, r.org, NULL, NULL, &voxel) == RES_OK);
  CHK(SVX_VOXEL_EQ(&voxel, &hit.voxel));
  CHK(*(char*)hit.voxel.data == 0);

  /* Use hit filter to discard the hit */
  CHK(RT(btree, r.org, r.dir, r.range, NULL, hit_filter, &r, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));

  /* Check ray with null dir along the btree axis */
  d3(r.org, -0.625, 456, 789);
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(eq_eps(hit.distance[0], r.range[0], 1.e-6));
  CHK(IS_INF(hit.distance[1]));
  CHK(hit.voxel.is_leaf == 1);
  CHK(svx_tree_at(btree, r.org, NULL, NULL, &voxel) == RES_OK);
  CHK(SVX_VOXEL_EQ(&voxel, &hit.voxel));
  CHK(*(char*)hit.voxel.data == 0);

  /* Use hit chalenge to intersect a non leaf node */
  CHK(RT(btree, r.org, r.dir, r.range, hit_challenge, NULL, NULL, &hit2) == RES_OK);
  CHK(eq_eps(hit.distance[0], r.range[0], 1.e-6));
  CHK(IS_INF(hit.distance[1]));
  CHK(!SVX_HIT_NONE(&hit2));
  CHK(!SVX_VOXEL_EQ(&hit2.voxel, &hit.voxel));
  CHK(*(char*)hit2.voxel.data == 1);
  CHK(hit2.voxel.depth < 3);

  /* Still check a ray with null dir along the tree axis */
  d3(r.org, -0.51, 31, 41);
  CHK(RT(btree, r.org, r.dir, r.range, NULL, NULL, NULL, &hit) == RES_OK);
  CHK(eq_eps(hit.distance[0], r.range[0], 1.e-6));
  CHK(IS_INF(hit.distance[1]));
  CHK(hit.voxel.is_leaf == 1);
  CHK(svx_tree_at(btree, r.org, NULL, NULL, &voxel) == RES_OK);
  CHK(SVX_VOXEL_EQ(&voxel, &hit.voxel));
  CHK(*(char*)hit.voxel.data == 1);
  CHK(eq_eps(hit.voxel.lower[0], -0.5-2.0/32.0, 1.e-6));
  CHK(eq_eps(hit.voxel.upper[0], -0.5, 1.e-6));
  CHK(IS_INF(hit.voxel.lower[1]));
  CHK(IS_INF(hit.voxel.lower[2]));
  CHK(IS_INF(hit.voxel.upper[1]));
  CHK(IS_INF(hit.voxel.upper[2]));

  /* Use hit filter to accum data along the ray */
  accum = 0;
  CHK(RT(btree, r.org, r.dir, r.range, NULL, hit_filter3, &accum, &hit) == RES_OK);
  CHK(SVX_HIT_NONE(&hit));
  CHK(accum == 1);

  /* Check the root node challenge */
  d3(r.org, 1.01, 1234, 10);
  d3_normalize(r.dir, d3(r.dir, -1, -1, -1));
  CHK(RT(btree, r.org, r.dir, r.range, hit_challenge_root, NULL, NULL, &hit)
    == RES_OK);
  CHK(!SVX_HIT_NONE(&hit));
  CHK(hit.voxel.lower[0] == -1);
  CHK(hit.voxel.upper[0] ==  1);
  CHK(IS_INF(hit.voxel.lower[1]));
  CHK(IS_INF(hit.voxel.upper[1]));
  CHK(IS_INF(hit.voxel.lower[2]));
  CHK(IS_INF(hit.voxel.upper[2]));
  CHK(hit.voxel.depth == 0);
  CHK(hit.voxel.is_leaf == 0);
  CHK(*((char*)hit.voxel.data) == 1);
  CHK(eq_eps(hit.distance[0], (hit.voxel.upper[0]-r.org[0])/r.dir[0], 1.e-4));
  CHK(eq_eps(hit.distance[1], (hit.voxel.lower[0]-r.org[0])/r.dir[0], 1.e-4));

  image_init(NULL, &img);
  image_init(NULL, &img2);
  draw_image(&img, btree);
  draw_image(&img2, btree2);

  /* Check that using oct or oct2 produces effectively the same image */
  check_img_eq(&img, &img2);

  /* Write the drawn image on stdout */
  CHK(image_write_ppm_stream(&img, 0/*binary*/, stdout) == RES_OK);
  image_release(&img);
  image_release(&img2);

  CHK(svx_tree_ref_put(btree) == RES_OK);
  CHK(svx_tree_ref_put(btree2) == RES_OK);
  CHK(svx_device_ref_put(dev) == RES_OK);
  CHK(mem_allocated_size() == 0);
  return 0;
}

