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
#include "suvm_volume.h"

#include <rsys/float2.h>
#include <rsys/float3.h>

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
suvm_primitive_setup_polyhedron
  (const struct suvm_primitive* prim,
   struct suvm_polyhedron* poly)
{
  if(!prim || !poly) return RES_BAD_ARG;
  tetrahedron_setup(prim->volume__, NULL, NULL, prim->iprim, poly);
  return RES_OK;
}

/* Define if an axis aligned bounding box and a tetrahedron are intersecting.
 * Its implementation follows the algorithms proposed by N. Greene in
 * "Detecting intersection of a rectangular solid and a convex polyhedron"
 * (Graphics Gems IV, 1994, p74--82) */
enum suvm_intersection_type
suvm_polyhedron_intersect_aabb
  (const struct suvm_polyhedron* tetra,
   const float low[3],
   const float upp[3])
{
  enum { X, Y, Z, AXES_COUNT }; /* Syntactic sugar */
  float intersect_aabb_low[AXES_COUNT];
  float intersect_aabb_upp[AXES_COUNT];
  int nplanes_including_aabb;
  int i;

  ASSERT(tetra && low && upp);
  ASSERT(low[0] < upp[0]);
  ASSERT(low[1] < upp[1]);
  ASSERT(low[2] < upp[2]);

  /* Check the intersection between the AABB and the tetra AABB */
  FOR_EACH(i, 0, AXES_COUNT) {
    intersect_aabb_low[i] = MMAX(low[i], tetra->lower[i]);
    intersect_aabb_upp[i] = MMIN(upp[i], tetra->upper[i]);
    if(intersect_aabb_low[i] > intersect_aabb_upp[i]) /* Do not intersect */
      return SUVM_INTERSECT_NONE;
  }

  /* Check if the tetrahedron is included into the aabb */
  if(intersect_aabb_low[X] == tetra->lower[X]
  && intersect_aabb_low[Y] == tetra->lower[Y]
  && intersect_aabb_low[Z] == tetra->lower[Z]
  && intersect_aabb_upp[X] == tetra->upper[X]
  && intersect_aabb_upp[Y] == tetra->upper[Y]
  && intersect_aabb_upp[Z] == tetra->upper[Z]) {
    return SUVM_INTERSECT_IS_INCLUDED;
  }

  /* #planes that totally includes the aabb on its positive side */
  nplanes_including_aabb = 0;

  /* Check the tetrahedron planes against the aabb */
  FOR_EACH(i, 0, 4) {
    float p[3], n[3];

    /* Define the aabb vertices that is the farthest in the positive/negative
     * direction of the face normal. */
    f3_set(p, upp);
    f3_set(n, low);
    if(tetra->N[i][X] < 0) SWAP(float, p[X], n[X]);
    if(tetra->N[i][Y] < 0) SWAP(float, p[Y], n[Y]);
    if(tetra->N[i][Z] < 0) SWAP(float, p[Z], n[Z]);

    /* Check that the box is totally outside the plane, To do this, check that
     * 'p', aka the farthest aabb vertex in the positive direction of the plane
     * normal, is not in the positive half space. In this case, the aabb is
     * entirely outside the tetrahedron */
    if((f3_dot(tetra->N[i], p) + tetra->D[i]) < 0) {
      return SUVM_INTERSECT_NONE;
    }

    /* Check if the box is totally inside the given plane. To do this, check
     * that 'n', aka the farthest aabb vertex in the negative direction of the
     * plane normal, is inside the positive half space */
    if((f3_dot(tetra->N[i], n) + tetra->D[i]) >= 0) {
      /* Register this plane as a plane including the aabb */
      nplanes_including_aabb += 1;
    }
  }

  /* Check if the aabb is entirely included into the tetrahedron */
  if(nplanes_including_aabb == 4) {
    return SUVM_INTERSECT_INCLUDE;
  }

  /* For each silhouette edge in each projection plane, check if it totally
   * exclude the projected aabb */
  FOR_EACH(i, 0, AXES_COUNT) {
    /* Compute the remaining axes.
     * On 1st iteration 'j' and 'k' define the YZ plane
     * On 2nd iteration 'j' and 'k' define the ZX plane
     * On 3rd ietration 'j' and 'k' define the XY plane */
    const int j = (i + 1) % AXES_COUNT;
    const int k = (j + 1) % AXES_COUNT;

    int iedge;
    FOR_EACH(iedge, 0, tetra->nEp[i]) {
      float p[2];

      /* Define the aabb vertices that is the farthest in the positive/negative
       * direction of the normal of the silhouette edge. */
      p[0] = tetra->Ep[i][iedge][0] > 0 ? upp[j] : low[j];
      p[1] = tetra->Ep[i][iedge][1] > 0 ? upp[k] : low[k];

      /* Check that the projected aabb along the 'i'th axis is totally outside
       * the current silhouette edge. That means that the aabb is entirely
       * outside the tetrahedron and thus that they do not intersect */
      if((f2_dot(tetra->Ep[i][iedge], p) + tetra->Ep[i][iedge][2]) < 0) {
        return SUVM_INTERSECT_NONE;
      }
    }
  }

  /* The tetrahedron and the aabb are partially intersecting */
  return SUVM_INTERSECT_PARTIAL;
}


/*******************************************************************************
 * Local functions
 ******************************************************************************/
void
tetrahedron_setup
  (const struct suvm_volume* vol,
   const float lower[3],
   const float upper[3],
   const size_t itetra,
   struct suvm_polyhedron* tetra)
{
  enum { X, Y, Z, AXES_COUNT }; /* Syntactic sugar */
  const float* low = lower;
  const float* upp = upper;
  float low__[3];
  float upp__[3];
  float center[AXES_COUNT]; /* Center of the tetrahedron */
  float e[6][AXES_COUNT];
  float (*v)[AXES_COUNT];
  float (*N)[AXES_COUNT];
  float (*Ep)[4][3];
  int i;
  ASSERT(vol && tetra);

  /* Fetch tetrahedron vertices */
  volume_primitive_get_vertex_position(vol, itetra, 0, tetra->v[0]);
  volume_primitive_get_vertex_position(vol, itetra, 1, tetra->v[1]);
  volume_primitive_get_vertex_position(vol, itetra, 2, tetra->v[2]);
  volume_primitive_get_vertex_position(vol, itetra, 3, tetra->v[3]);
  v = tetra->v;

  /* Compute the center of the tetrahedron */
  center[X] = (v[0][X] + v[1][X] + v[2][X] + v[3][X]) * 0.25f;
  center[Y] = (v[0][Y] + v[1][Y] + v[2][Y] + v[3][Y]) * 0.25f;
  center[Z] = (v[0][Z] + v[1][Z] + v[2][Z] + v[3][Z]) * 0.25f;

  /* Define the primitive AABB if necessary */
  if(!low) {
    low__[0] = MMIN(MMIN(v[0][X], v[1][X]), MMIN(v[2][X], v[3][X]));
    low__[1] = MMIN(MMIN(v[0][Y], v[1][Y]), MMIN(v[2][Y], v[3][Y]));
    low__[2] = MMIN(MMIN(v[0][Z], v[1][Z]), MMIN(v[2][Z], v[3][Z]));
    low = low__;
  }
  if(!upp) {
    upp__[0] = MMAX(MMAX(v[0][X], v[1][X]), MMAX(v[2][X], v[3][X]));
    upp__[1] = MMAX(MMAX(v[0][Y], v[1][Y]), MMAX(v[2][Y], v[3][Y]));
    upp__[2] = MMAX(MMAX(v[0][Z], v[1][Z]), MMAX(v[2][Z], v[3][Z]));
    upp = upp__;
  }

#ifndef NDEBUG
  /* Check argument consistency */
  if(lower) {
    ASSERT(MMIN(MMIN(v[0][X], v[1][X]), MMIN(v[2][X], v[3][X])) == lower[X]);
    ASSERT(MMIN(MMIN(v[0][Y], v[1][Y]), MMIN(v[2][Y], v[3][Y])) == lower[Y]);
    ASSERT(MMIN(MMIN(v[0][Z], v[1][Z]), MMIN(v[2][Z], v[3][Z])) == lower[Z]);
  }
  if(upper) {
    ASSERT(MMAX(MMAX(v[0][X], v[1][X]), MMAX(v[2][X], v[3][X])) == upper[X]);
    ASSERT(MMAX(MMAX(v[0][Y], v[1][Y]), MMAX(v[2][Y], v[3][Y])) == upper[Y]);
    ASSERT(MMAX(MMAX(v[0][Z], v[1][Z]), MMAX(v[2][Z], v[3][Z])) == upper[Z]);
  }
#endif

  /* Setup tetrahedron AABB */
  f3_set(tetra->lower, low);
  f3_set(tetra->upper, upp);

  /* Fetch tetrahedron normals */
  volume_primitive_get_facet_normal(vol, itetra, 0, tetra->N[0]);
  volume_primitive_get_facet_normal(vol, itetra, 1, tetra->N[1]);
  volume_primitive_get_facet_normal(vol, itetra, 2, tetra->N[2]);
  volume_primitive_get_facet_normal(vol, itetra, 3, tetra->N[3]);
  N = tetra->N;

  /* Compute the slope of the planes */
  tetra->D[0] = -f3_dot(tetra->N[0], v[0]);
  tetra->D[1] = -f3_dot(tetra->N[1], v[1]);
  tetra->D[2] = -f3_dot(tetra->N[2], v[2]);
  tetra->D[3] = -f3_dot(tetra->N[3], v[3]);

  /* Compute tetrahedron edges */
  f3_sub(e[0], v[1], v[0]);
  f3_sub(e[1], v[2], v[1]);
  f3_sub(e[2], v[0], v[2]);
  f3_sub(e[3], v[0], v[3]);
  f3_sub(e[4], v[1], v[3]);
  f3_sub(e[5], v[2], v[3]);

  /* Detect the silhouette edges of the tetrahedron once projected in the YZ,
   * ZX and XY planes, and compute their 2D equation in the projected plane. The
   * variable 'i' defines the projection axis which is successively X (YZ
   * plane), Y (ZX plane) and Z (XY plane). The axes of the corresponding 2D
   * repairs are defined by the 'j' and 'k' variables. */
  Ep = tetra->Ep;
  FOR_EACH(i, 0, AXES_COUNT) {
    float c[2]; /* Projected tetrahedron center */

    /* On 1st iteration 'j' and 'k' define the YZ plane
     * On 2nd iteration 'j' and 'k' define the ZX plane
     * On 3rd ietration 'j' and 'k' define the XY plane */
    const int j = (i + 1) % AXES_COUNT;
    const int k = (j + 1) % AXES_COUNT;

    /* Register the number of detected silhouette edges */
    int n = 0;

    int iedge;

    /* Project the tetrahedron center */
    c[0] = center[j];
    c[1] = center[k];

    /* To detect the silhouette edges, check the sign of the normals of two
     * adjacent facets for the coordinate of the projection axis. If the signs
     * are the same, the facets look at the same direction regarding the
     * projection axis. The other case means that one facet points toward the
     * projection axis whole the other one points to the opposite direction:
     * this is the definition of a silhouette edge.
     *
     * Once detected, we compute the equation of the silhouette edge:
     *
     *    Ax + By + C = 0
     *
     * with A and B the coordinates of the edge normals and C the slope of the
     * edge. Computing the normal is actually as simple as swizzling the
     * coordinates of the edges projected in 2D and sign tuning. Let the
     * projection axis X (i==X) and thus the YZ projection plane (j==Y &&
     * k==Z). Assuming that the first edge 'e[0]' is a silhouette edge, ie the
     * edge between the facet0 and the facet1 (refers to the suvm_volume.h for
     * the geometric layout of a tetrahedron) once projected in the YZ plane
     * the edge is {0, e[0][Y], e[0][Z]}. Its normal N can is defined as the
     * cross product between this projected edge ands the projection axis:
     *
     *        |    0    |   | 1 |   |    0    |
     *    N = | e[0][Y] | X | 0 | = | e[0][Z] |
     *        | e[0][Z] |   | 0 |   |-e[0][Y] |
     *
     * In the projection plane, the _unormalized_ normal of the {e[0][Y],
     * e[0][Z]} edge is thus {e[0][Z], -e[0][Y]}. More generally, the normal of
     * an edge 'I' {e[I][j], e[I][k]} projected along the 'i' axis in the 'jk'
     * plane is {e[I][k],-e[I]j]}. Anyway, one has to revert the normal
     * orientation to ensure a specific convention wrt the tetrahedron
     * footprint. In our case we ensure that the normal points toward the
     * footprint. Finalle the edge slope 'C' can be computed as:
     *         | A |
     *    C =- | B | . | Pj, Pk |
     *
     * with {Pj, Pk} a point belonging to the edge as for instance one of its
     * vertices. */
    if(signf(N[0][i]) != signf(N[1][i])) { /* The edge 0 is silhouette */
      f2_normalize(Ep[i][n], f2(Ep[i][n], e[0][k], -e[0][j]));
      Ep[i][n][2] = -(Ep[i][n][0]*v[0][j] + Ep[i][n][1]*v[0][k]);
      ++n;
    }
    if(signf(N[0][i]) != signf(N[2][i])) { /* The edge 1 is silhouette */
      f2_normalize(Ep[i][n], f2(Ep[i][n], e[1][k], -e[1][j]));
      Ep[i][n][2] = -(Ep[i][n][0]*v[1][j] + Ep[i][n][1]*v[1][k]);
      ++n;
    }
    if(signf(N[0][i]) != signf(N[3][i])) { /* The edge 2 is silhouette */
      f2_normalize(Ep[i][n], f2(Ep[i][n], e[2][k], -e[2][j]));
      Ep[i][n][2] = -(Ep[i][n][0]*v[2][j] + Ep[i][n][1]*v[2][k]);
      ++n;
    }
    if(signf(N[1][i]) != signf(N[3][i])) { /* The edge 3 is silhouette */
      f2_normalize(Ep[i][n], f2(Ep[i][n], e[3][k], -e[3][j]));
      Ep[i][n][2] = -(Ep[i][n][0]*v[0][j] + Ep[i][n][1]*v[0][k]);
      ++n;
    }
    if(signf(N[1][i]) != signf(N[2][i])) { /* The edge 4 is silhouette */
      f2_normalize(Ep[i][n], f2(Ep[i][n], e[4][k], -e[4][j]));
      Ep[i][n][2] = -(Ep[i][n][0]*v[1][j] + Ep[i][n][1]*v[1][k]);
      ++n;
    }
    if(signf(N[2][i]) != signf(N[3][i])) { /* The edge 5 is silhouette */
      f2_normalize(Ep[i][n], f2(Ep[i][n], e[5][k], -e[5][j]));
      Ep[i][n][2] = -(Ep[i][n][0]*v[2][j] + Ep[i][n][1]*v[2][k]);
      ++n;
    }
    /* A tetrahedron can have 3 or 4 silhouette edges once projected in 2D */
    ASSERT(n == 3 || n == 4);
    tetra->nEp[i] = n; /* Register the #silouhette edges for this project axis */

    /* Ensure that the edge normals point toward the tetrahedron center */
    FOR_EACH(iedge, 0, n) {
      if(f2_dot(Ep[i][iedge], c) + Ep[i][iedge][2] < 0)
        f3_minus(Ep[i][iedge], Ep[i][iedge]);
    }
  }
}

