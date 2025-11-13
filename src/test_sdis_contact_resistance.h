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


/*
 * The scene is composed of a solid cube/square of size L. The cube/square
 * is made of 2 solids that meet at x=e in ]0 L[.
 * 
 *             3D                    2D
 *
 *          /////////(L,L,L)     /////////(L,L)
 *          +-------+            +-------+
 *         /'  /   /|            |   !   |
 *        +-------+ TL          T0   r   TL
 *        | | !   | |            |   !   |
 *       T0 +.r...|.+            +-------+
 *        |,  !   |/         (0,0)///x=X0///
 *        +-------+
 * (0,0,0)///x=X0///
 */

#define L 4.0
#define X0 3.0

 /*******************************************************************************
  * Box geometry
  ******************************************************************************/
static const double model3d_vertices[12/*#vertices*/*3/*#coords per vertex*/] = {
  0, 0, 0,
  X0, 0, 0,
  L, 0, 0,
  0, L, 0,
  X0, L, 0,
  L, L, 0,
  0, 0, L,
  X0, 0, L,
  L, 0, L,
  0, L, L,
  X0, L, L,
  L, L, L
};
static const size_t model3d_nvertices = sizeof(model3d_vertices)/(sizeof(double)*3);

/* The following array lists the indices toward the 3D vertices of each
 * triangle.
 *        ,3---,4---,5          ,3----4----5        ,4
 *      ,' | ,' | ,'/|        ,'/| \  | \  |      ,'/|
 *    9----10---11 / |      9' / |  \ |  \ |    10 / |          Y
 *    |',  |',  | / ,2      | / ,0---,1---,2    | / ,1          |
 *    |  ',|  ',|/,'        |/,' | ,' | ,'      |/,'         o--X
 *    6----7----8'          6----7'---8'        7              /
 *  Front, right         Back, left and       Internal        Z
 * and Top faces          bottom faces         face */
static const size_t model3d_indices[22/*#triangles*/*3/*#indices per triangle*/] = {
  0, 3, 1, 1, 3, 4,     1, 4, 2, 2, 4, 5,    /* -Z */
  0, 6, 3, 3, 6, 9,                          /* -X */
  6, 7, 9, 9, 7, 10,    7, 8, 10, 10, 8, 11, /* +Z */
  5, 11, 8, 8, 2, 5,                         /* +X */
  3, 9, 10, 10, 4, 3,   4, 10, 11, 11, 5, 4, /* +Y */
  0, 1, 7, 7, 6, 0,     1, 2, 8, 8, 7, 1,    /* -Y */
  4, 10, 7, 7, 1, 4                          /* Inside */
};
static const size_t model3d_ntriangles = sizeof(model3d_indices)/(sizeof(size_t)*3);

static INLINE void
model3d_get_indices(const size_t itri, size_t ids[3], void* context)
{
  (void)context;
  CHK(ids);
  CHK(itri < model3d_ntriangles);
  ids[0] = model3d_indices[itri * 3 + 0];
  ids[1] = model3d_indices[itri * 3 + 1];
  ids[2] = model3d_indices[itri * 3 + 2];
}

static INLINE void
model3d_get_position(const size_t ivert, double pos[3], void* context)
{
  (void)context;
  CHK(pos);
  CHK(ivert < model3d_nvertices);
  pos[0] = model3d_vertices[ivert * 3 + 0];
  pos[1] = model3d_vertices[ivert * 3 + 1];
  pos[2] = model3d_vertices[ivert * 3 + 2];
}

static INLINE void
model3d_get_interface(const size_t itri, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  CHK(itri < model3d_ntriangles);
  *bound = interfaces[itri];
}

/*******************************************************************************
 * Square geometry
 ******************************************************************************/
static const double model2d_vertices[6/*#vertices*/*2/*#coords per vertex*/] = {
  L, 0,
  X0, 0,
  0, 0,
  0, L,
  X0, L,
  L, L
};
static const size_t model2d_nvertices = sizeof(model2d_vertices)/(sizeof(double)*2);

static const size_t model2d_indices[7/*#segments*/ * 2/*#indices per segment*/] = {
  0, 1, 1, 2, /* Bottom */
  2, 3,       /* Left */
  3, 4, 4, 5, /* Top */
  5, 0,       /* Right */
  4, 1        /* Inside */
};
static const size_t model2d_nsegments = sizeof(model2d_indices) / (sizeof(size_t)*2);

static INLINE void
model2d_get_indices(const size_t iseg, size_t ids[2], void* context)
{
  (void)context;
  CHK(ids);
  CHK(iseg < model2d_nsegments);
  ids[0] = model2d_indices[iseg * 2 + 0];
  ids[1] = model2d_indices[iseg * 2 + 1];
}

static INLINE void
model2d_get_position(const size_t ivert, double pos[2], void* context)
{
  (void)context;
  CHK(pos);
  CHK(ivert < model2d_nvertices);
  pos[0] = model2d_vertices[ivert * 2 + 0];
  pos[1] = model2d_vertices[ivert * 2 + 1];
}

static INLINE void
model2d_get_interface
(const size_t iseg, struct sdis_interface** bound, void* context)
{
  struct sdis_interface** interfaces = context;
  CHK(context && bound);
  CHK(iseg < model2d_nsegments);
  *bound = interfaces[iseg];
}
