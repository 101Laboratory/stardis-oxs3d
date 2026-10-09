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

#include <rsys/float2.h>
#include <rsys/float3.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
aabb_intersect_aabb
  (const float low0[3],
   const float upp0[3],
   const float low1[3],
   const float upp1[3])
{
  ASSERT(low0 && upp0 && low1 && upp1);
  ASSERT(low0[0] < upp0[0] && low0[1] < upp0[1] && low0[2] < upp0[2]);
  ASSERT(low1[0] < upp1[0] && low1[1] < upp1[1] && low1[2] < upp1[2]);
  return low0[0] < upp1[0] && upp0[0] > low1[0]
      && low0[1] < upp1[1] && upp0[1] > low1[1]
      && low0[2] < upp1[2] && upp0[2] > low1[2];
}

/*******************************************************************************
 * Exported function
 ******************************************************************************/
res_T
suvm_volume_intersect_aabb
  (struct suvm_volume* vol,
   const double low[3], /* AABB lower bound */
   const double upp[3], /* AABB upper bound */
   suvm_primitive_intersect_aabb_T clbk,
   void* context)
{
  struct darray_node stack;
  struct node* node = NULL;
  struct node_leaf* leaf = NULL;
  struct node_inner* inner = NULL;
  int stack_is_init = 0;
  float lowf[3];
  float uppf[3];
  res_T res = RES_OK;

  if(!vol || !low || !upp || !clbk) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Convert the input AABB in single precision */
  lowf[0] = (float)low[0];
  lowf[1] = (float)low[1];
  lowf[2] = (float)low[2];
  uppf[0] = (float)upp[0];
  uppf[1] = (float)upp[1];
  uppf[2] = (float)upp[2];

  /* Check AABB consistency */
  if(lowf[0] >= uppf[0] || lowf[1] >= uppf[1] || lowf[2] >= uppf[2]) {
    log_err(vol->dev, "%s: invalid AABB {%g, %g, %g}, {%g, %g, %g}.\n",
      FUNC_NAME, SPLIT3(low), SPLIT3(upp));
    res = RES_BAD_ARG;
    goto error;
  }

  darray_node_init(vol->dev->allocator, &stack);
  stack_is_init = 1;

  node = vol->bvh_root;
  ASSERT(node);

  if(!aabb_intersect_aabb(lowf, uppf, vol->low, vol->upp)) {
    goto exit;
  }

  res = darray_node_reserve(&stack, 32);
  if(res != RES_OK) goto error;

  for(;;) {
    size_t istack;

    if(node->type == NODE_LEAF) {
      struct suvm_polyhedron tetra;
      enum suvm_intersection_type intersect;

      leaf = CONTAINER_OF(node, struct node_leaf, node);

      /* Check the intersection between the tetrahedron and the AABB */
      tetrahedron_setup(vol, leaf->low, leaf->upp, leaf->prim_id, &tetra);
      intersect = suvm_polyhedron_intersect_aabb(&tetra, lowf, uppf);

      if(intersect != SUVM_INTERSECT_NONE) {
        struct suvm_primitive prim = SUVM_PRIMITIVE_NULL;
        /* Setup the intersected primitive */
        volume_primitive_setup(vol, leaf->prim_id, &prim);
        /* Invoke the user callback on the intersected primitive */
        clbk(&prim, low, upp, context);
      }

    } else {
      int traverse_child[2] = {0,0};
      inner = CONTAINER_OF(node, struct node_inner, node);

      /* Check the provided  AABB agains the children's AABBs */
      traverse_child[0] = aabb_intersect_aabb
        (lowf, uppf, inner->low[0], inner->upp[0]);
      traverse_child[1] = aabb_intersect_aabb
        (lowf, uppf, inner->low[1], inner->upp[1]);

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

exit:
  if(stack_is_init) darray_node_release(&stack);
  return res;
error:
  goto exit;
}
