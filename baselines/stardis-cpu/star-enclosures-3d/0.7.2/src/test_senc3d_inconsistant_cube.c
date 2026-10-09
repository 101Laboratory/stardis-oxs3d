/* Copyright (C) 2018-2020, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

#include "senc3d.h"
#include "test_senc3d_utils.h"

#include <rsys/double3.h>

/* The following array lists the indices toward the 3D vertices of each
* triangle.
*        ,6---,7           ,6----7
*      ,' | ,'/|         ,' | \  |      Y   Z
*    2----3' / |       2',  |  \ |      | ,'
*    |',  | / ,5       |  ',4---,5      0----X
*    |  ',|/,'         | ,' | ,'
*    0----1'           0----1'
*  Front, right      Back, left and
* and Top faces       bottom faces */
/* Triangle #0 rotation order is not consistant with other triangles */
static unsigned
inconsistant_box_indices[12/*#triangles*/ * 3/*#indices per triangle*/] = {
  0, 1, 2, 1, 2, 3, /* Front face */
  0, 4, 2, 2, 4, 6, /* Left face*/
  4, 5, 6, 6, 5, 7, /* Back face */
  3, 7, 1, 1, 7, 5, /* Right face */
  2, 6, 3, 3, 6, 7, /* Top face */
  0, 1, 4, 4, 1, 5  /* Bottom face */
};
static const unsigned inconsistant_box_ntriangles
= sizeof(inconsistant_box_indices) / (3 * sizeof(*inconsistant_box_indices));

/* Media definitions reflect triangle #0 inconsistancy */
static const unsigned
inconsistant_medium_front[12] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const unsigned
inconsistant_medium_back[12] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static const double inconsistant_cube_vertices[8/*#vertices*/ * 3/*#coords per vertex*/] = {
  10.0, 10.0, 10.0,
  11.0, 10.0, 10.0,
  10.0, 11.0, 10.0,
  11.0, 11.0, 10.0,
  10.0, 10.0, 11.0,
  11.0, 10.0, 11.0,
  10.0, 11.0, 11.0,
  11.0, 11.0, 11.0
};

static void
test(const int convention)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct senc3d_enclosure* enclosure;
  struct senc3d_enclosure_header header;
  int conv;
  int conv_front, conv_in;
  struct context ctx = CONTEXT_NULL__;
  unsigned i, e, ecount;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* A 3D cube.
   * 2 enclosures (inside, outside) sharing the same triangles,
   * but opposite sides.
   * What differs in this test is that triangle #0 vertices are given
   * in the opposite rotation order. */
  ctx.positions = inconsistant_cube_vertices;
  ctx.indices = inconsistant_box_indices;
  ctx.front_media = inconsistant_medium_front;
  ctx.back_media = inconsistant_medium_back;

  OK(senc3d_scene_create(dev, convention, inconsistant_box_ntriangles,
    get_indices, get_media, nvertices, get_position, &ctx, &scn));

  OK(senc3d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 2);

  OK(senc3d_scene_get_convention(scn, &conv));
  CHK(conv == convention);
  conv_front = (conv & SENC3D_CONVENTION_NORMAL_FRONT) != 0;
  conv_in = (conv & SENC3D_CONVENTION_NORMAL_INSIDE) != 0;

  FOR_EACH(e, 0, ecount) {
    unsigned medium, expected_external_medium, expected_medium;
    char name[128];
    enum senc3d_side side, expected_side;
    unsigned gid;
    (void)name;
    OK(senc3d_scene_get_enclosure(scn, e, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));
    CHK(header.enclosed_media_count == 1);
    CHK(header.area == 6);
    CHK(header.volume == (header.is_infinite ? -1 : 1));
    OK(senc3d_enclosure_get_medium(enclosure, 0, &medium));
    /* If NORMAL_FRONT the external enclosure's medium should be 0,
     * 1 if NORMAL_BACK */
    expected_external_medium = conv_front ? 0 : 1;
    expected_medium = (header.is_infinite ?
      expected_external_medium : !expected_external_medium);
    CHK(medium == expected_medium);

#ifdef DUMP_ENCLOSURES
    sprintf(name, "test_inconsistant_cube_%s_%s_%u.obj",
      conv_front ? "front" : "back", conv_in ? "in" : "out", e);
    dump_enclosure(scn, e, name);
#endif

    FOR_EACH(i, 0, header.primitives_count) {
      int same, reversed, fst_reversed;
      fst_reversed = ((e == 0) == conv_in);
      expected_side = (inconsistant_medium_front[i] == expected_medium)
        ? SENC3D_FRONT : SENC3D_BACK;
      cmp_trg(i, enclosure,
        inconsistant_box_indices + (3 * i), inconsistant_cube_vertices,
        &same, &reversed);
      /* Should be made of the same triangles */
      CHK(same);
      CHK(i ? reversed != fst_reversed : reversed == fst_reversed);
      OK(senc3d_enclosure_get_triangle_id(enclosure, i, &gid, &side));
      CHK(side == expected_side);
    }
    SENC3D(enclosure_ref_put(enclosure));
  }

  SENC3D(scene_ref_put(scn));
  SENC3D(device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
}

int main(int argc, char** argv)
{
  (void) argc, (void) argv;

  test(SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE);
  test(SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_INSIDE);
  test(SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_OUTSIDE);
  test(SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_OUTSIDE);

  return 0;
}
