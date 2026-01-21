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

/* This test has been created using the sg3_geometry_dump_as_C_code feature
 * of star-geometry. It uses output from test_sg3_unspecified_properties.
 * This test is similar to test_senc3d_many_enclosures that creates a huge
 * geometry by program. */

#include "senc3d.h"
#include "test_senc3d_utils.h"

#include <rsys/double3.h>

#define SG3D_UNSPECIFIED_PROPERTY UINT_MAX

/* Dump of star-geometry 'front_unspecified'. */
static const unsigned front_unspecified_vertices_count = 8;
static const double front_unspecified_vertices[24] =
{
   0.1, 0, 0,
   1, 0, 0,
   0, 1, 0,
   1, 1, 0,
   0, 0, 1.1,
   1, 0, 1,
   0, 1, 1,
   1, 1.1, 1
};
static const unsigned front_unspecified_triangles_count = 12;
static const unsigned front_unspecified_triangles[36] =
{
   0, 2, 1,
   1, 2, 3,
   0, 4, 2,
   2, 4, 6,
   4, 5, 6,
   6, 5, 7,
   3, 7, 1,
   1, 7, 5,
   2, 6, 3,
   3, 6, 7,
   0, 1, 4,
   4, 1, 5
};
static const unsigned front_unspecified_properties[36] =
{
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY
};
/* Dump of star-geometry 'front_half_unspecified'. */
static const unsigned front_half_unspecified_vertices_count = 8;
static const double front_half_unspecified_vertices[24] =
{
   0.1, 0, 0,
   1, 0, 0,
   0, 1, 0,
   1, 1, 0,
   0, 0, 1.1,
   1, 0, 1,
   0, 1, 1,
   1, 1.1, 1
};
static const unsigned front_half_unspecified_triangles_count = 12;
static const unsigned front_half_unspecified_triangles[36] =
{
   0, 2, 1,
   1, 2, 3,
   0, 4, 2,
   2, 4, 6,
   4, 5, 6,
   6, 5, 7,
   3, 7, 1,
   1, 7, 5,
   2, 6, 3,
   3, 6, 7,
   0, 1, 4,
   4, 1, 5
};
static const unsigned front_half_unspecified_properties[36] =
{
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   0, 1, 0,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   0, 1, 0,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   0, 1, 0,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   0, 1, 0,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   0, 1, 0,
   SG3D_UNSPECIFIED_PROPERTY, 1, SG3D_UNSPECIFIED_PROPERTY,
   0, 1, 0
};
/* Dump of star-geometry 'all_defined'. */
static const unsigned all_defined_vertices_count = 8;
static const double all_defined_vertices[24] =
{
   0.1, 0, 0,
   1, 0, 0,
   0, 1, 0,
   1, 1, 0,
   0, 0, 1.1,
   1, 0, 1,
   0, 1, 1,
   1, 1.1, 1
};
static const unsigned all_defined_triangles_count = 12;
static const unsigned all_defined_triangles[36] =
{
   0, 2, 1,
   1, 2, 3,
   0, 4, 2,
   2, 4, 6,
   4, 5, 6,
   6, 5, 7,
   3, 7, 1,
   1, 7, 5,
   2, 6, 3,
   3, 6, 7,
   0, 1, 4,
   4, 1, 5
};
static const unsigned all_defined_properties[36] =
{
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0
};

#undef SG3D_UNSPECIFIED_PROPERTY

static void
test(const int convention)
{
  struct mem_allocator allocator;
  struct senc3d_device* dev = NULL;
  struct senc3d_scene* scn = NULL;
  struct senc3d_enclosure* enclosure;
  struct senc3d_enclosure_header header;
  unsigned medium, expected_external_medium, expected_internal_medium;
  unsigned gid;
  enum senc3d_side side;
  struct context ctx = CONTEXT_NULL__;
  unsigned i, t, ecount;
  const int conv_front = (convention & SENC3D_CONVENTION_NORMAL_FRONT) != 0;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc3d_device_create(NULL, &allocator, SENC3D_NTHREADS_DEFAULT, 1, &dev));

  /* Geometry with no media information on both sides */
  ctx.positions = front_unspecified_vertices;
  ctx.indices = front_unspecified_triangles;
  ctx.properties = front_unspecified_properties;
  OK(senc3d_scene_create(dev, convention, front_unspecified_triangles_count,
    get_indices, get_media_from_properties, front_unspecified_vertices_count,
    get_position, &ctx, &scn));

  OK(senc3d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 2);

  FOR_EACH(i, 0, ecount) {
    struct senc3d_enclosure* ee;
    struct senc3d_enclosure_header hh;
    unsigned cc;
    OK(senc3d_scene_get_enclosure(scn, i, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));

    CHK(header.enclosure_id == i);
    CHK(header.enclosed_media_count == 1);

    OK(senc3d_enclosure_get_medium(enclosure, 0, &medium));
    /* Geometrical normals point outside the cube in input triangles:
     * if convention is front, front medium (unspecified) is outside,
     * that is medium 0's enclosure is infinite */
    expected_external_medium = conv_front ? SENC3D_UNSPECIFIED_MEDIUM : 1;
    expected_internal_medium = conv_front ? 1 : SENC3D_UNSPECIFIED_MEDIUM;

    CHK(medium == (header.is_infinite
      ? expected_external_medium : expected_internal_medium));
    CHK(header.primitives_count == ntriangles);
    CHK(header.unique_primitives_count == ntriangles);
    CHK(header.vertices_count == nvertices);
    CHK(header.is_infinite == (i == 0));

    OK(senc3d_scene_get_enclosure_count_by_medium(scn, medium, &cc));
    CHK(cc == 1);
    OK(senc3d_scene_get_enclosure_by_medium(scn, medium, 0, &ee));
    OK(senc3d_enclosure_get_header(ee, &hh));
    CHK(header.enclosure_id == hh.enclosure_id);
    OK(senc3d_enclosure_ref_put(ee));

    FOR_EACH(t, 0, header.primitives_count) {
      unsigned ind[3];
      OK(senc3d_enclosure_get_triangle_id(enclosure, t, &gid, &side));
      CHK(gid == t);
      CHK(side == (medium == 1) ? SENC3D_BACK : SENC3D_FRONT);
      OK(senc3d_enclosure_get_triangle(enclosure, t, ind));
    }
    OK(senc3d_enclosure_ref_put(enclosure));
  }
  OK(senc3d_scene_ref_put(scn));

  /* Same geometry, front media are defined for odd triangles */
  ctx.positions = front_half_unspecified_vertices;
  ctx.indices = front_half_unspecified_triangles;
  ctx.properties = front_half_unspecified_properties;
  OK(senc3d_scene_create(dev, convention, front_half_unspecified_triangles_count,
    get_indices, get_media_from_properties, front_half_unspecified_vertices_count,
    get_position, &ctx, &scn));

  OK(senc3d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 2);

  FOR_EACH(i, 0, ecount) {
    unsigned expected_external_media_count, expected_internal_media_count,
      expected_media_count;
    OK(senc3d_scene_get_enclosure(scn, i, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));

    CHK(header.enclosure_id == i);

    OK(senc3d_enclosure_get_medium(enclosure, 0, &medium));
    /* Geometrical normals point outside the cube in input triangles:
     * if convention is front, front medium is outside and the enclosure
     * contains 2 media */
    expected_external_media_count = conv_front ? 2 : 1;
    expected_internal_media_count = conv_front ? 1 : 2;
    expected_media_count = header.is_infinite
      ? expected_external_media_count : expected_internal_media_count;
    CHK(header.enclosed_media_count == expected_media_count);
    OK(senc3d_enclosure_ref_put(enclosure));
  }
  OK(senc3d_scene_ref_put(scn));

  /* Same geometry, all media are defined */
  ctx.positions = all_defined_vertices;
  ctx.indices = all_defined_triangles;
  ctx.properties = all_defined_properties;
  OK(senc3d_scene_create(dev, convention, all_defined_triangles_count,
    get_indices, get_media_from_properties, all_defined_vertices_count,
    get_position, &ctx, &scn));

  OK(senc3d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 2);

  FOR_EACH(i, 0, ecount) {
    struct senc3d_enclosure* ee;
    struct senc3d_enclosure_header hh;
    unsigned cc;
    OK(senc3d_scene_get_enclosure(scn, i, &enclosure));
    OK(senc3d_enclosure_get_header(enclosure, &header));

    CHK(header.enclosure_id == i);
    CHK(header.enclosed_media_count == 1);
    OK(senc3d_enclosure_get_medium(enclosure, 0, &medium));
    /* Geometrical normals point outside the cube in input triangles:
     * if convention is front, front medium (0) is outside,
     * that is medium 0's enclosure is infinite */
    CHK(conv_front == ((medium == 0) == header.is_infinite));
    CHK(header.primitives_count == ntriangles);
    CHK(header.unique_primitives_count == ntriangles);
    CHK(header.vertices_count == nvertices);
    CHK(header.is_infinite == (i == 0));

    OK(senc3d_scene_get_enclosure_count_by_medium(scn, medium, &cc));
    CHK(cc == 1);
    OK(senc3d_scene_get_enclosure_by_medium(scn, medium, 0, &ee));
    OK(senc3d_enclosure_get_header(ee, &hh));
    CHK(header.enclosure_id == hh.enclosure_id);
    OK(senc3d_enclosure_ref_put(ee));

    FOR_EACH(t, 0, header.primitives_count) {
      OK(senc3d_enclosure_get_triangle_id(enclosure, t, &gid, &side));
      CHK(gid == t);
      CHK(side == (medium == 1) ? SENC3D_BACK : SENC3D_FRONT);
    }
    OK(senc3d_enclosure_ref_put(enclosure));
  }

  SENC3D(scene_ref_put(scn));
  SENC3D(device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
}

int
main(int argc, char** argv)
{
  (void) argc, (void) argv;
  test(SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_INSIDE);
  test(SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_INSIDE);
  test(SENC3D_CONVENTION_NORMAL_FRONT | SENC3D_CONVENTION_NORMAL_OUTSIDE);
  test(SENC3D_CONVENTION_NORMAL_BACK | SENC3D_CONVENTION_NORMAL_OUTSIDE);
  return 0;
}
