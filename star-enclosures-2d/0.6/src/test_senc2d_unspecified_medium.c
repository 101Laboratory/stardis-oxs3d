/* Copyright (C) 2018-2021, 2023, 2024 |Méso|Star> (contact@meso-star.com)
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

/* This test has been created using the sg2_geometry_dump_as_C_code feature
 * of star-geometry-2D. It uses output from test_sg2_unspecified_properties.
 * This test is similar to test_senc2d_many_enclosures that creates a huge
 * geometry by program. */

#include "senc2d.h"
#include "senc2d_sXd_helper.h"
#include "test_senc2d_utils.h"

#include <rsys/double2.h>

#define SG2_UNSPECIFIED_PROPERTY UINT_MAX

/* Dump of star-geometry-2D 'front_unspecified'. */
static const unsigned front_unspecified_vertices_count = 4;
static const double front_unspecified_vertices[8] =
{
   0.1, 0,
   1, 0,
   0, 1.1,
   1, 1
};
static const unsigned front_unspecified_segments_count = 4;
static const unsigned front_unspecified_segments[8] =
{
   0, 2,
   2, 3,
   3, 1,
   1, 0
};
static const unsigned front_unspecified_properties[12] =
{
   SG2_UNSPECIFIED_PROPERTY, 1, SG2_UNSPECIFIED_PROPERTY,
   SG2_UNSPECIFIED_PROPERTY, 1, SG2_UNSPECIFIED_PROPERTY,
   SG2_UNSPECIFIED_PROPERTY, 1, SG2_UNSPECIFIED_PROPERTY,
   SG2_UNSPECIFIED_PROPERTY, 1, SG2_UNSPECIFIED_PROPERTY
};
/* Dump of star-geometry-2D 'front_half_unspecified'. */
static const unsigned front_half_unspecified_vertices_count = 4;
static const double front_half_unspecified_vertices[8] =
{
   0.1, 0,
   1, 0,
   0, 1.1,
   1, 1
};
static const unsigned front_half_unspecified_segments_count = 4;
static const unsigned front_half_unspecified_segments[8] =
{
   0, 2,
   2, 3,
   3, 1,
   1, 0
};
static const unsigned front_half_unspecified_properties[12] =
{
   SG2_UNSPECIFIED_PROPERTY, 1, SG2_UNSPECIFIED_PROPERTY,
   0, 1, 0,
   SG2_UNSPECIFIED_PROPERTY, 1, SG2_UNSPECIFIED_PROPERTY,
   0, 1, 0
};
/* Dump of star-geometry-2D 'all_defined'. */
static const unsigned all_defined_vertices_count = 4;
static const double all_defined_vertices[8] =
{
   0.1, 0,
   1, 0,
   0, 1.1,
   1, 1
};
static const unsigned all_defined_segments_count = 4;
static const unsigned all_defined_segments[8] =
{
   0, 2,
   2, 3,
   3, 1,
   1, 0
};
static const unsigned all_defined_properties[12] =
{
   0, 1, 0,
   0, 1, 0,
   0, 1, 0,
   0, 1, 0
};

static void
test(const int convention)
{
  struct mem_allocator allocator;
  struct senc2d_device* dev = NULL;
  struct senc2d_scene* scn = NULL;
  struct senc2d_enclosure* enclosure;
  struct senc2d_enclosure_header header;
  unsigned medium, expected_external_medium, expected_internal_medium;
  unsigned gid;
  enum senc2d_side side;
  struct context ctx = CONTEXT_NULL__;
  unsigned i, s, ecount;
  const int conv_front = (convention & SENC2D_CONVENTION_NORMAL_FRONT) != 0;

  OK(mem_init_proxy_allocator(&allocator, &mem_default_allocator));
  OK(senc2d_device_create(NULL, &allocator, SENC2D_NTHREADS_DEFAULT, 1, &dev));
  
  /* Geometry with no media information on both sides */
  ctx.positions = front_unspecified_vertices;
  ctx.indices = front_unspecified_segments;
  ctx.properties = front_unspecified_properties;
  OK(senc2d_scene_create(dev, convention, front_unspecified_segments_count,
    get_indices, get_media_from_properties, front_unspecified_vertices_count,
    get_position, &ctx, &scn));

  OK(senc2d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 2);

  FOR_EACH(i, 0, ecount) {
    struct senc2d_enclosure* ee;
    struct senc2d_enclosure_header hh;
    unsigned cc;
    OK(senc2d_scene_get_enclosure(scn, i, &enclosure));
    OK(senc2d_enclosure_get_header(enclosure, &header));

    CHK(header.enclosure_id == i);
    CHK(header.enclosed_media_count == 1);

    OK(senc2d_enclosure_get_medium(enclosure, 0, &medium));
    /* Geometrical normals point outside the square in input segments:
     * if convention is front, front medium (undef) is outside,
     * that is medium 0's enclosure is infinite */
    expected_external_medium = conv_front ? SENC2D_UNSPECIFIED_MEDIUM : 1;
    expected_internal_medium = conv_front ? 1 : SENC2D_UNSPECIFIED_MEDIUM;

    CHK(medium == (header.is_infinite
      ? expected_external_medium : expected_internal_medium));
    CHK(header.primitives_count == nsegments);
    CHK(header.unique_primitives_count == nsegments);
    CHK(header.vertices_count == nvertices);
    CHK(header.is_infinite == (i == 0));

    OK(senc2d_scene_get_enclosure_count_by_medium(scn, medium, &cc));
    CHK(cc == 1);
    OK(senc2d_scene_get_enclosure_by_medium(scn, medium, 0, &ee));
    OK(senc2d_enclosure_get_header(ee, &hh));
    CHK(header.enclosure_id == hh.enclosure_id);
    OK(senc2d_enclosure_ref_put(ee));

    FOR_EACH(s, 0, header.primitives_count) {
      unsigned ind[2];
      OK(senc2d_enclosure_get_segment_id(enclosure, s, &gid, &side));
      CHK(gid == s);
      CHK(side == (medium == 1) ? SENC2D_BACK : SENC2D_FRONT);
      OK(senc2d_enclosure_get_segment(enclosure, s, ind));
    }
    OK(senc2d_enclosure_ref_put(enclosure));
  }
  OK(senc2d_scene_ref_put(scn));

  /* Same geometry, front media are defined for odd segments */
  ctx.positions = front_half_unspecified_vertices;
  ctx.indices = front_half_unspecified_segments;
  ctx.properties = front_half_unspecified_properties;
  OK(senc2d_scene_create(dev, convention, front_half_unspecified_segments_count,
    get_indices, get_media_from_properties, front_half_unspecified_vertices_count,
    get_position, &ctx, &scn));

  OK(senc2d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 2);

  FOR_EACH(i, 0, ecount) {
    unsigned expected_external_media_count, expected_internal_media_count,
      expected_media_count;
    OK(senc2d_scene_get_enclosure(scn, i, &enclosure));
    OK(senc2d_enclosure_get_header(enclosure, &header));

    CHK(header.enclosure_id == i);

    OK(senc2d_enclosure_get_medium(enclosure, 0, &medium));
    /* Geometrical normals point outside the square in input segments:
     * if convention is front, front medium is outside and the enclosure
     * contains 2 media */
    expected_external_media_count = conv_front ? 2 : 1;
    expected_internal_media_count = conv_front ? 1 : 2;
    expected_media_count = header.is_infinite
      ? expected_external_media_count : expected_internal_media_count;
    CHK(header.enclosed_media_count == expected_media_count);
    OK(senc2d_enclosure_ref_put(enclosure));
  }
  OK(senc2d_scene_ref_put(scn));

  /* Same geometry, all media are defined */
  ctx.positions = all_defined_vertices;
  ctx.indices = all_defined_segments;
  ctx.properties = all_defined_properties;
  OK(senc2d_scene_create(dev, convention, all_defined_segments_count,
    get_indices, get_media_from_properties, all_defined_vertices_count,
    get_position, &ctx, &scn));

  OK(senc2d_scene_get_enclosure_count(scn, &ecount));
  CHK(ecount == 2);

  FOR_EACH(i, 0, ecount) {
    struct senc2d_enclosure* ee;
    struct senc2d_enclosure_header hh;
    unsigned cc;
    OK(senc2d_scene_get_enclosure(scn, i, &enclosure));
    OK(senc2d_enclosure_get_header(enclosure, &header));

    CHK(header.enclosure_id == i);
    CHK(header.enclosed_media_count == 1);
    OK(senc2d_enclosure_get_medium(enclosure, 0, &medium));
    /* Geometrical normals point outside the square in input segments:
     * if convention is front, front medium (0) is outside,
     * that is medium 0's enclosure is infinite */
    CHK(conv_front == ((medium == 0) == header.is_infinite));
    CHK(header.primitives_count == nsegments);
    CHK(header.unique_primitives_count == nsegments);
    CHK(header.vertices_count == nvertices);
    CHK(header.is_infinite == (i == 0));

    OK(senc2d_scene_get_enclosure_count_by_medium(scn, medium, &cc));
    CHK(cc == 1);
    OK(senc2d_scene_get_enclosure_by_medium(scn, medium, 0, &ee));
    OK(senc2d_enclosure_get_header(ee, &hh));
    CHK(header.enclosure_id == hh.enclosure_id);
    OK(senc2d_enclosure_ref_put(ee));

    FOR_EACH(s, 0, header.primitives_count) {
      OK(senc2d_enclosure_get_segment_id(enclosure, s, &gid, &side));
      CHK(gid == s);
      CHK(side == (medium == 1) ? SENC2D_BACK : SENC2D_FRONT);
    }
    OK(senc2d_enclosure_ref_put(enclosure));
  }

  SENC2D(scene_ref_put(scn));
  SENC2D(device_ref_put(dev));

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
}

int
main(int argc, char** argv)
{
  (void)argc, (void)argv;
  test(SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_INSIDE);
  test(SENC2D_CONVENTION_NORMAL_BACK | SENC2D_CONVENTION_NORMAL_INSIDE);
  test(SENC2D_CONVENTION_NORMAL_FRONT | SENC2D_CONVENTION_NORMAL_OUTSIDE);
  test(SENC2D_CONVENTION_NORMAL_BACK | SENC2D_CONVENTION_NORMAL_OUTSIDE);
  return 0;
}
