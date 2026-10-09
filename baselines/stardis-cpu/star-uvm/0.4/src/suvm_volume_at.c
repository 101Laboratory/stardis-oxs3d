/* Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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

#include "suvm.h"
#include "suvm_device.h"
#include "suvm_volume.h"

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static FINLINE int
pos_in_aabb
  (const float pos[3],
   const float low[3],
   const float upp[3])
{
  return low[0] <= pos[0] && upp[0] >= pos[0]
      && low[1] <= pos[1] && upp[1] >= pos[1]
      && low[2] <= pos[2] && upp[2] >= pos[2];
}

/* Return 1 if pos lies is included into the leaf or 0 otherwise */
static int
intersect_leaf
  (const struct suvm_volume* vol,
   const struct node_leaf* leaf,
   const float pos[3],
   float bcoords[4]) /* Barycentric coordinates of pos into the leaf */
{
  const uint32_t* tetra;
  const float *v0, *v1, *v2, *v3;
  float n0[3], n1[3], n2[3], n3[3];
  float p[3];
  float d0, d1, d2, d3;
  float h1, h2, h3;
  size_t itetra;
  ASSERT(vol && leaf && pos && bcoords);
  ASSERT(leaf->prim_id < volume_get_primitives_count(vol));

  itetra = leaf->prim_id;
  tetra = darray_u32_cdata_get(&vol->indices) + itetra*4/*#vertices per tetra*/;

  /* Fetch the first tetrahedron vertex */
  v0 = darray_float_cdata_get(&vol->positions) + tetra[0]*3;

  /* Compute the distance from pos to the bottom facet */
  volume_primitive_get_facet_normal(vol, itetra, 0, n0);
  d0 = f3_dot(n0, f3_sub(p, pos, v0));
  if(d0 < 0) return 0;

  /* Fetch the top tetrahedron vertex */
  v3 = darray_float_cdata_get(&vol->positions) + tetra[3]*3;
  f3_sub(p, pos, v3);

  /* Compute the distance from pos to the 'side' facets */
  volume_primitive_get_facet_normal(vol, itetra, 1, n1);
  d1 = f3_dot(n1, p);
  if(d1 < 0) return 0;
  volume_primitive_get_facet_normal(vol, itetra, 2, n2);
  d2 = f3_dot(n2, p);
  if(d2 < 0) return 0;
  volume_primitive_get_facet_normal(vol, itetra, 3, n3);
  d3 = f3_dot(n3, p);
  if(d3 < 0) return 0;

  /* Fetch the two remaining vertices (i.e. the second and third ones) */
  v1 = darray_float_cdata_get(&vol->positions) + tetra[1]*3;
  v2 = darray_float_cdata_get(&vol->positions) + tetra[2]*3;

  /* Distance of tetrahedron corners to their opposite face */
  h1 = f3_dot(n1, f3_sub(p, v2, v1));
  h2 = f3_dot(n2, f3_sub(p, v0, v2));
  h3 = f3_dot(n3, f3_sub(p, v1, v3));

  /* Compute the barycentric coordinates, i.e. ratio of distances */
  bcoords[0] = d2 / h2;
  bcoords[1] = d3 / h3;
  bcoords[2] = d1 / h1;

#if 1
  /* Do not use d0 and h0 to calculate the last barycentric coordinate but
   * calculate it from the 3 others. This gets around the numerical imprecision
   * and ensures that the sum of the bcoords is indeed 1 */
  bcoords[3] = 1.f - bcoords[0] - bcoords[1] - bcoords[2];
#else
  {
    const float h0 = f3_dot(n0, f3_sub(p, v3, v0));
    bcoords[3] = d0 / h0;
  }
#endif

  return 1;
}

/*******************************************************************************
 * Exported function
 ******************************************************************************/
res_T
suvm_volume_at
  (struct suvm_volume* vol,
   const double pos_in[3],
   struct suvm_primitive* prim,
   double barycentric_coords[4])
{
  struct darray_node stack;
  struct node* node = NULL;
  struct node_leaf* leaf = NULL;
  struct node_inner* inner = NULL;
  float pos[3];
  size_t iprim = SIZE_MAX; /* Index of the hit primitive */
  int stack_is_init = 0;
  res_T res = RES_OK;

  if(!vol || !pos_in || !prim || !barycentric_coords) {
    res = RES_BAD_ARG;
    goto error;
  }
  *prim = SUVM_PRIMITIVE_NULL;
  pos[0] = (float)pos_in[0];
  pos[1] = (float)pos_in[1];
  pos[2] = (float)pos_in[2];
  darray_node_init(vol->dev->allocator, &stack);
  stack_is_init = 1;

  node = vol->bvh_root;
  ASSERT(node);

  /* Is the position included in the AABB of the volume */
  if(!pos_in_aabb(pos, vol->low, vol->upp)) {
    goto exit;
  }

  res = darray_node_reserve(&stack, 32);
  if(res != RES_OK) goto error;

  for(;;) {
    size_t istack;

    if(node->type == NODE_LEAF) {
      float bcoords[4];
      leaf = CONTAINER_OF(node, struct node_leaf, node);
      if(intersect_leaf(vol, leaf, pos, bcoords)) {
        iprim = leaf->prim_id;
        barycentric_coords[0] = (double)bcoords[0];
        barycentric_coords[1] = (double)bcoords[1];
        barycentric_coords[2] = (double)bcoords[2];
        barycentric_coords[3] = (double)bcoords[3];
        break;
      }

    } else {
      int traverse_child[2] = {0,0};
      inner = CONTAINER_OF(node, struct node_inner, node);

      /* Check pos against the children's AABBs */
      traverse_child[0] = pos_in_aabb(pos, inner->low[0], inner->upp[0]);
      traverse_child[1] = pos_in_aabb(pos, inner->low[1], inner->upp[1]);

      if(traverse_child[0] && traverse_child[1]) { /* Traverse both children */
        res = darray_node_push_back(&stack, &inner->children[1]);
        if(UNLIKELY(res != RES_OK)) goto error;
        node = inner->children[0];
        continue;

      } else if(traverse_child[0]) { /* Traverse only child 0 */
        node = inner->children[0];
        continue;

      } else if(traverse_child[1]) { /* Traverse only child 1 */
        node = inner->children[1];
        continue;
      }
    }

    istack = darray_node_size_get(&stack);
    if(!istack) break; /* No more stack entry: stop traversal */

    /* Pop the stack */
    node = darray_node_data_get(&stack)[istack-1];
    darray_node_pop_back(&stack);
  }

  /* pos lies into a tetrahedron */
  if(iprim != SIZE_MAX) {
    volume_primitive_setup(vol, iprim, prim);
  }

exit:
  if(stack_is_init) darray_node_release(&stack);
  return res;
error:
  goto exit;
}

