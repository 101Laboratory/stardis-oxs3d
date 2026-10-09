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

#include "svx.h"
#include "svx_tree.h"

#include <rsys/double3.h>

struct ray {
  /* Ray parameters in the normalized octree space [1, 2]^3 whose ray direction
   * is assumed to be always negative. octant_mask defines which dimension was
   * reverted to ensure the negative ray direction: the bit 1, 2 and 3 are set if
   * the Z, Y and X dimension was reverted, respectively. */
  double org[3];
  double range[2];
  double ts[3]; /* 1 / -abs(dir) */
  uint32_t octant_mask;

  /* Ray origin and direction in world space. Note that the ray range is
   * independent of the octree space. Let a ray `r' in world space defined as
   * `r = O + tD'; one can transform the ray in another space by the Matrix M
   * as `r' = M.O + t*(M.D)' without any impact on the t parameter and thus on
   * its range. Only the norm of ray direction might be updated. */
  double orgws[3];
  double dirws[3];
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static FINLINE uint32_t
ftoui(const float f)
{
  union { uint32_t ui; float f; } ucast;
  ucast.f = f;
  return ucast.ui;
}

static FINLINE float
uitof(const uint32_t ui)
{
  union { uint32_t ui; float f; } ucast;
  ucast.ui = ui;
  return ucast.f;
}

static FINLINE void
setup_hit
  (struct svx_tree* oct,
   const struct buffer_index iattr, /* Index toward the voxel attributes */
   const double distance_min, /* Dst from ray org to the voxel entry point */
   const double distance_max, /* Dst from ray_org to the voxel exit point */
   const float corner[3], /* Corner of the current voxel in [1, 2]^3 space */
   const double scale_exp2, /* Size of the voxel in [1, 2]^3 space */
   const size_t depth, /* Depth of the voxel in the octree hierarchy */
   const int is_leaf, /* Define if the voxel is a leaf or not */
   const uint32_t octant_mask, /* bitmask of reverted dimensions */
   struct svx_hit* hit)
{
  double low[3], upp[3];
  ASSERT(oct && corner && hit);
  ASSERT(distance_min >= 0 && distance_min <= distance_max);

  hit->distance[0] = distance_min;
  hit->distance[1] = distance_max;
  hit->voxel.data = buffer_get_attr(&oct->buffer, iattr);
  hit->voxel.depth = depth,
  hit->voxel.id = buffer_absolute_attr_index(&oct->buffer, iattr);
  hit->voxel.is_leaf = is_leaf;

  /* Transform the voxel aabb in the [0, 1]^3 normalized space and flip it wrt
   * to the octant mask of the ray */
  low[0] = corner[0] - 1;
  low[1] = corner[1] - 1;
  low[2] = corner[2] - 1;
  if(octant_mask & 4) low[0] = 1 - low[0] - scale_exp2;
  if(octant_mask & 2) low[1] = 1 - low[1] - scale_exp2;
  if(octant_mask & 1) low[2] = 1 - low[2] - scale_exp2;
  upp[0] = low[0] + scale_exp2;
  upp[1] = low[1] + scale_exp2;
  upp[2] = low[2] + scale_exp2;

  /* Transform the voxel AABB in world space */
  hit->voxel.lower[0] = low[0] * oct->tree_size[0] + oct->tree_low[0];
  hit->voxel.lower[1] = low[1] * oct->tree_size[1] + oct->tree_low[1];
  hit->voxel.lower[2] = low[2] * oct->tree_size[2] + oct->tree_low[2];
  hit->voxel.upper[0] = upp[0] * oct->tree_size[0] + oct->tree_low[0];
  hit->voxel.upper[1] = upp[1] * oct->tree_size[1] + oct->tree_low[1];
  hit->voxel.upper[2] = upp[2] * oct->tree_size[2] + oct->tree_low[2];
}

/* Return RES_BAD_OP if the ray cannot intersect the scene */
static res_T
setup_ray
  (const struct svx_tree* oct,
   const double org[3],
   const double dir[3],
   const double range[2],
   struct ray* ray)
{
  double rcp_ocsz[3]; /* Reciprocal size of the World space octree AABB */
  double dir_adjusted[3];
  ASSERT(oct);

  if(range[0] >= range[1]) return RES_BAD_OP; /* Disabled ray */

  /* Ray paralelle to an axis and that does not intersect the scene AABB */
  if((!dir[0] && (org[0] < oct->lower[0] || org[0] > oct->upper[0]))
  || (!dir[1] && (org[1] < oct->lower[1] || org[1] > oct->upper[1]))
  || (!dir[2] && (org[2] < oct->lower[2] || org[2] > oct->upper[2]))) {
    return RES_BAD_OP;
  }

  /* Compute reciprocal size of the world space octree AABB */
  rcp_ocsz[0] = 1.0 / oct->tree_size[0];
  rcp_ocsz[1] = 1.0 / oct->tree_size[1];
  rcp_ocsz[2] = 1.0 / oct->tree_size[2];

  /* Transform the ray origin in the [1, 2] space */
  ray->org[0] = (org[0] - oct->tree_low[0]) * rcp_ocsz[0] + 1.0;
  ray->org[1] = (org[1] - oct->tree_low[1]) * rcp_ocsz[1] + 1.0;
  ray->org[2] = (org[2] - oct->tree_low[2]) * rcp_ocsz[2] + 1.0;

  /* Setup the ray range */
  ray->range[0] = range[0];
  ray->range[1] = range[1];

  /* Transform the direction in the normalized octree space */
  dir_adjusted[0] = dir[0] * rcp_ocsz[0];
  dir_adjusted[1] = dir[1] * rcp_ocsz[1];
  dir_adjusted[2] = dir[2] * rcp_ocsz[2];

  /* Let a ray defined as org + t*dir and X the coordinate of an axis aligned
   * plane. The ray intersects X at t = (X - org)/dir = (X - org) * ts; with ts
   * = 1/dir. Note that one assume that dir is always negative. */
  ray->ts[0] = 1.0 / -fabs(dir_adjusted[0]);
  ray->ts[1] = 1.0 / -fabs(dir_adjusted[1]);
  ray->ts[2] = 1.0 / -fabs(dir_adjusted[2]);

  /* Mirror rays with position directions */
  ray->octant_mask = 0;
  if(dir[0] > 0) { ray->octant_mask ^= 4; ray->org[0] = 3.0 - ray->org[0]; }
  if(dir[1] > 0) { ray->octant_mask ^= 2; ray->org[1] = 3.0 - ray->org[1]; }
  if(dir[2] > 0) { ray->octant_mask ^= 1; ray->org[2] = 3.0 - ray->org[2]; }

  /* Save the world space ray origin */
  ray->orgws[0] = org[0];
  ray->orgws[1] = org[1];
  ray->orgws[2] = org[2];

  /* Save the world space ray direction */
  ray->dirws[0] = dir[0];
  ray->dirws[1] = dir[1];
  ray->dirws[2] = dir[2];

  return RES_OK;
}

static res_T
trace_ray
  (struct svx_tree* oct,
   const struct ray* ray,
   const svx_hit_challenge_T challenge,
   const svx_hit_filter_T filter,
   void* context,
   struct svx_hit* hit)
{
  #define SCALE_MAX 23 /* #mantisse bits */
  struct stack_entry {
    struct buffer_index inode;
    double t_max;
  } stack[SCALE_MAX + 1/*Dummy entry use to avoid invalid read*/];
  struct buffer_index inode;
  double t_min, t_max;
  float corner[3];
  float scale_exp2;
  uint32_t scale_max;
  uint32_t scale; /* stack index */
  uint32_t ichild;
  ASSERT(oct && ray && hit && oct->type == SVX_OCTREE);

  *hit = SVX_HIT_NULL; /* Initialise the hit to "no intersection" */

  /* Compute the min/max ray intersection with the octree AABB in normalized
   * space. Note that in this space, the octree AABB is in [1, 2]^3 */
  t_min =      (2 - ray->org[0]) * ray->ts[0];
  t_min = MMAX((2 - ray->org[1]) * ray->ts[1], t_min);
  t_min = MMAX((2 - ray->org[2]) * ray->ts[2], t_min);
  t_max =      (1 - ray->org[0]) * ray->ts[0];
  t_max = MMIN((1 - ray->org[1]) * ray->ts[1], t_max);
  t_max = MMIN((1 - ray->org[2]) * ray->ts[2], t_max);
  t_min = MMAX(ray->range[0], t_min);
  t_max = MMIN(ray->range[1], t_max);
  if(t_min >= t_max) return RES_OK; /* No intersection */

  /* Challenge the root */
  if(challenge) {
    struct svx_hit hit_root;
    struct buffer_index iattr_dummy = buffer_get_child_attr_index
      (&oct->buffer, oct->root, 0/*arbitrarly child index*/);

    /* Lower left corner of the root node in the [1, 2]^3 space */
    corner[0] = 1.f;
    corner[1] = 1.f;
    corner[2] = 1.f;

    /* Use the regular setup_hit procedure by providing a dummy attribute
     * index, and then overwrite the voxel data with the root one */
    setup_hit(oct, iattr_dummy, t_min, t_max, corner, 1.f/*scale_exp2*/,
      0/*depth*/, 0/*is_leaf*/, ray->octant_mask, &hit_root);
    hit_root.voxel.data = oct->root_attr;

    if(challenge(&hit_root, ray->orgws, ray->dirws, ray->range, context)) {
      if(!filter /* By default, i.e. with no filter, stop the traversal */
      || !filter(&hit_root, ray->orgws, ray->dirws, ray->range, context)) {
        *hit = hit_root;
        return RES_OK; /* Do not traverse the octree */
      }
    }
  }

  /* Traversal initialisation */
  inode = oct->root;
  scale_exp2 = 0.5f;
  scale = SCALE_MAX - 1;

  /* Define the first child id and the position of its lower left corner in the
   * normalized octree space, i.e. in [1, 2]^3 */
  ichild = 0;
  corner[0] = 1.f;
  corner[1] = 1.f;
  corner[2] = 1.f;
  if((1.5 - ray->org[0])*ray->ts[0] > t_min) { ichild ^= 4; corner[0] = 1.5f; }
  if((1.5 - ray->org[1])*ray->ts[1] > t_min) { ichild ^= 2; corner[1] = 1.5f; }
  if((1.5 - ray->org[2])*ray->ts[2] > t_min) { ichild ^= 1; corner[2] = 1.5f; }

  /* Octree traversal */
  scale_max = scale + 1;
  while(scale < scale_max && t_min < t_max) {
    const struct buffer_xnode* node = buffer_get_node(&oct->buffer, inode);
    double t_corner[3];
    double t_max_corner;
    double t_max_child;
    uint32_t ichild_adjusted = ichild ^ ray->octant_mask;
    uint32_t ichild_flag = (uint32_t)BIT(ichild_adjusted);
    uint32_t istep;

    /* Compute the exit point of the ray in the current child node */
    t_corner[0] = (corner[0] - ray->org[0])*ray->ts[0];
    t_corner[1] = (corner[1] - ray->org[1])*ray->ts[1];
    t_corner[2] = (corner[2] - ray->org[2])*ray->ts[2];
    t_max_corner = MMIN(MMIN(t_corner[0], t_corner[1]), t_corner[2]);

    /* Traverse the current child */
    if((node->is_valid & ichild_flag)
    && t_min <= (t_max_child = MMIN(t_max, t_max_corner))) {
      const int is_leaf = (node->is_leaf & ichild_flag) != 0;
      int go_deeper = 1;

      /* If the current voxel is a leaf or if a challenge function is set,
       * check the current hit */
      if(is_leaf || challenge) {
        struct svx_hit hit_tmp;
        const size_t depth = SCALE_MAX - scale;
        const struct buffer_index iattr = buffer_get_child_attr_index
          (&oct->buffer, inode, (int)ichild_adjusted);

        setup_hit(oct, iattr, t_min, t_max_child, corner, scale_exp2, depth,
          is_leaf, ray->octant_mask, &hit_tmp);

        if(is_leaf
        || challenge(&hit_tmp, ray->orgws, ray->dirws, ray->range, context)) {
          go_deeper = 0;
          /* Stop the traversal if no filter is defined or if the filter
           * function returns 0 */
          if(!filter /* By default, i.e. with no filter, stop the traversal */
          || !filter(&hit_tmp, ray->orgws, ray->dirws, ray->range, context)) {
            *hit = hit_tmp;
            break;
          }
        }
      }

      if(go_deeper) {
        double t_max_parent;
        double t_center[3];
        float scale_exp2_child;

        t_max_parent = t_max;
        t_max = t_max_child;

        scale_exp2_child = scale_exp2 * 0.5f;

        /* center = corner - scale_exp2_child =>
         * t_center = ts*(corner + scale_exp2_child - org)
         * t_center = t_corner + ts*scale_exp2_child
         * Anyway we perforrm the whole computation to avoid numerical issues */
        t_center[0] = (corner[0] - ray->org[0] + scale_exp2_child) * ray->ts[0];
        t_center[1] = (corner[1] - ray->org[1] + scale_exp2_child) * ray->ts[1];
        t_center[2] = (corner[2] - ray->org[2] + scale_exp2_child) * ray->ts[2];

        /* Push the parent node */
        stack[scale].t_max = t_max_parent;
        stack[scale].inode = inode;

        /* Get the node index of the traversed child */
        inode = buffer_get_child_node_index
          (&oct->buffer, inode, (int)ichild_adjusted);

        /* Define the id and the lower left corner of the first grand child */
        ichild = 0;
        if(t_center[0] > t_min) { ichild ^= 4; corner[0] += scale_exp2_child; }
        if(t_center[1] > t_min) { ichild ^= 2; corner[1] += scale_exp2_child; }
        if(t_center[2] > t_min) { ichild ^= 1; corner[2] += scale_exp2_child; }

        --scale;
        scale_exp2 = scale_exp2_child;
        continue;
      }
    }

    /* Define the id and the lower left corner of the next child */
    istep = 0;
    if(t_corner[0] <= t_max_corner) { istep ^= 4; corner[0] -= scale_exp2; }
    if(t_corner[1] <= t_max_corner) { istep ^= 2; corner[1] -= scale_exp2; }
    if(t_corner[2] <= t_max_corner) { istep ^= 1; corner[2] -= scale_exp2; }
    ichild ^= istep;

    t_min = t_max_corner; /* Adjust the ray range */

    if((ichild & istep) != 0) { /* The ray exits the child. Pop the stack. */
      uint32_t diff_bits = 0;
      uint32_t shift[3];

      /* The IEEE-754 encoding of a single precision floating point number `f'
       * whose binary representation is `F' is defined as:
       *    f = (-1)^S * 2^(E-127)
       *      * (1 + For(i in [0..22]) { M & BIT(22-i) ? 2^i : 0 })
       * with S = F / 2^31; E = F / 2^23 and M = F & (2^23 - 1).
       *
       * We transformed the SVO in a normalized translated space of [1, 2]^3.
       * As a consequence, the coordinates of the lower left `corner' of a node
       * have always a null exponent (i.e. E = 127). In addition, note that for
       * each dimension, the i^th bit of the mantissa M is set if the corner is
       * greater or equal to the median split of the node at the (23-i)^th
       * level.
       *
       * For instance, considering the mantissa M=0x480000 of a X coordinate of
       * a node lower left corner. According to its binary encoding, i.e.
       * `100 1000 0000 0000 0000 0000', one can assume that:
       *    - X >= 1 + 2^-1
       *    - X <  1 + 2^-1 + 2^-2
       *    - X <  1 + 2^-1 + 2^-3
       *    - X >= 1 + 2^-1 + 2^-4
       *
       * Note that we ensure that the traversal along each dimension is from 2
       * to 1, i.e.  the ray direction are always negative. To define if the
       * median split of a node is traversed by a ray, it is thus sufficient to
       * track the update of its corresponding bit from 1 to 0.
       *
       * In the following code we use this property to find the highest level
       * into the node hierarchy whose median split was traversed by the ray.
       * This is particularly usefull since, thanks to this information, one
       * can pop the traversal stack directly up to this level. This remove the
       * recursive popping and thus drastically reduced the computation cost of
       * the 'stack pop' procedure. */
      if(istep & 4) diff_bits |= ftoui(corner[0]) ^ ftoui(corner[0]+scale_exp2);
      if(istep & 2) diff_bits |= ftoui(corner[1]) ^ ftoui(corner[1]+scale_exp2);
      if(istep & 1) diff_bits |= ftoui(corner[2]) ^ ftoui(corner[2]+scale_exp2);
      scale = (ftoui((float)diff_bits) >> 23) - 127;
      scale_exp2 = uitof(((scale - SCALE_MAX) + 127) << 23);

      inode = stack[scale].inode;
      t_max = stack[scale].t_max;

      /* Compute the lower corner of the popped node */
      shift[0] = ftoui(corner[0]) >> scale;
      shift[1] = ftoui(corner[1]) >> scale;
      shift[2] = ftoui(corner[2]) >> scale;
      corner[0] = uitof(shift[0] << scale);
      corner[1] = uitof(shift[1] << scale);
      corner[2] = uitof(shift[2] << scale);

      /* Define the index of the popped node */
      ichild = ((shift[0] & 1) << 2) | ((shift[1] & 1) << 1) | (shift[2] & 1);
    }
  }

  return RES_OK;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
res_T
octree_trace_ray
  (struct svx_tree* oct,
   const double org[3],
   const double dir[3],
   const double range[2],
   const svx_hit_challenge_T challenge,
   const svx_hit_filter_T filter,
   void* context,
   struct svx_hit* hit)
{
  struct ray ray;
  res_T res = RES_OK;

  if(!oct || !org || !dir || !range || !hit || oct->type != SVX_OCTREE
  || !d3_is_normalized(dir)) {
    res = RES_BAD_ARG;
    goto error;
  }

  *hit = SVX_HIT_NULL;

  res = setup_ray(oct, org, dir, range, &ray);
  if(res == RES_BAD_OP) { /* The ray cannot intersect the scene. */
    res = RES_OK;
    goto exit;
  }

  res = trace_ray(oct, &ray, challenge, filter, context, hit);
  if(res != RES_OK) goto error;

exit:
  return res;
error:
  goto exit;
}


