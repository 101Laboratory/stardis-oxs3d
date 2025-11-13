/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#include "sdis.h"

#include <rsys/double2.h>
#include <rsys/double3.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
cmp_dbl2(const double v0[2], const double v1[2])
{
  if(v0[0] < v1[0]) return -1;
  if(v0[0] > v1[0]) return +1;
  if(v0[1] < v1[1]) return -1;
  if(v0[1] > v1[1]) return +1;
  return 0;
}

static INLINE int
cmp_dbl3(const double v0[3], const double v1[3])
{
  if(v0[0] < v1[0]) return -1;
  if(v0[0] > v1[0]) return +1;
  if(v0[1] < v1[1]) return -1;
  if(v0[1] > v1[1]) return +1;
  if(v0[2] < v1[2]) return -1;
  if(v0[2] > v1[2]) return +1;
  return 0;
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
void
sdis_primkey_2d_setup
  (struct sdis_primkey* key,
   const double node0[2],
   const double node1[2])
{
  double *v0, *v1;
  ASSERT(key && node0 && node1);
  v0 = d2_set(key->nodes+0, node0);
  v1 = d2_set(key->nodes+2, node1);
  if(cmp_dbl2(v0, v1) > 0) {
    SWAP(double, v0[0], v1[0]);
    SWAP(double, v0[1], v1[1]);
  }
  key->ncoords = 4;
}

void
sdis_primkey_setup
  (struct sdis_primkey* key,
   const double node0[3],
   const double node1[3],
   const double node2[3])
{
  double *v0, *v1, *v2;
  ASSERT(key && node0 && node1 && node2);
  v0 = d3_set(key->nodes+0, node0);
  v1 = d3_set(key->nodes+3, node1);
  v2 = d3_set(key->nodes+6, node2);

  /* Bubble sort */
  #define SWAP_DBL3(V0, V1) {                                                  \
    SWAP(double, (V0)[0], (V1)[0]);                                            \
    SWAP(double, (V0)[1], (V1)[1]);                                            \
    SWAP(double, (V0)[2], (V1)[2]);                                            \
  } (void)0
  if(cmp_dbl3(v0, v1) > 0) SWAP_DBL3(v0, v1);
  if(cmp_dbl3(v1, v2) > 0) SWAP_DBL3(v1, v2);
  if(cmp_dbl3(v0, v1) > 0) SWAP_DBL3(v0, v1);
  #undef SWAP_DBL3
  key->ncoords = 9;
}

size_t
sdis_primkey_hash(const struct sdis_primkey* key)
{
  return hash_fnv64(key->nodes, sizeof(double)*key->ncoords);
}

char
sdis_primkey_eq
  (const struct sdis_primkey* key0,
   const struct sdis_primkey* key1)
{
  unsigned i = 0;
  ASSERT(key0 && key1);

  if(key0->ncoords != key1->ncoords) return 0;
  if(key0->ncoords != 4 && key0->ncoords != 9) return 0;
  if(key1->ncoords != 4 && key1->ncoords != 9) return 0;
  FOR_EACH(i, 0, key0->ncoords) {
    if(key0->nodes[i] != key1->nodes[i]) return 0;
  }
  return 1;
}
