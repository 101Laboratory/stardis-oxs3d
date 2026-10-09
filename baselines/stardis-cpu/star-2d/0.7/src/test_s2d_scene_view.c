/* Copyright (C) 2016-2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#include "s2d.h"
#include "test_s2d_utils.h"

#include <rsys/float2.h>

#include <string.h>

static void
test_miscellaneous
  (struct s2d_device* dev,
   struct s2d_shape* square,
   struct s2d_shape* line)
{
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  int mask;

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, square) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, line) == RES_OK);

  CHK(s2d_scene_view_create(NULL, 0, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_create(scn, 0, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_create(NULL, S2D_SAMPLE, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_create(scn, S2D_SAMPLE, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_create(NULL, 0, &scnview) == RES_BAD_ARG);
  CHK(s2d_scene_view_create(scn, 0, &scnview) == RES_BAD_ARG);
  CHK(s2d_scene_view_create(NULL, S2D_SAMPLE, &scnview) == RES_BAD_ARG);
  CHK(s2d_scene_view_create(scn, S2D_SAMPLE, &scnview) == RES_OK);

  CHK(s2d_scene_view_get_mask(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_get_mask(scnview, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_get_mask(NULL, &mask) == RES_BAD_ARG);
  CHK(s2d_scene_view_get_mask(scnview, &mask) == RES_OK);
  CHK(mask == S2D_SAMPLE);

  CHK(s2d_scene_view_ref_get(NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_ref_get(scnview) == RES_OK);
  CHK(s2d_scene_view_ref_put(NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_TRACE|S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_get_mask(scnview, &mask) == RES_OK);
  CHK((mask & S2D_TRACE) == S2D_TRACE);
  CHK((mask & S2D_GET_PRIMITIVE) == S2D_GET_PRIMITIVE);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_SAMPLE|S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_get_mask(scnview, &mask) == RES_OK);
  CHK((mask & S2D_SAMPLE) == S2D_SAMPLE);
  CHK((mask & S2D_GET_PRIMITIVE) == S2D_GET_PRIMITIVE);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_ref_put(scn) == RES_OK);
}

static void
test_trace_ray
  (struct s2d_device* dev,
   struct s2d_shape* square,
   struct s2d_shape* line)
{
  struct s2d_scene* scn;
  struct s2d_scene* scn2;
  struct s2d_scene_view* scnview;
  struct s2d_scene_view* scnview2;
  struct s2d_hit hit, hit2;
  float org[2], dir[2], range[2];
  unsigned isquare;
  unsigned iline;

  CHK(s2d_shape_get_id(square, &isquare) == RES_OK);
  CHK(s2d_shape_get_id(line, &iline) == RES_OK);

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_scene_create(dev, &scn2) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, line) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, square) == RES_OK);
  CHK(s2d_scene_attach_shape(scn2, square) == RES_OK);
  CHK(s2d_scene_attach_shape(scn2, line) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_SAMPLE|S2D_GET_PRIMITIVE, &scnview) == RES_OK);

  f2(org, 10.f, 9.75f);
  f2(dir, 0.f, 1.f);
  f2(range, 0.f, FLT_MAX);
  CHK(s2d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_BAD_OP);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &scnview2) == RES_OK);
  CHK(s2d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.geom_id == iline);
  CHK(hit.prim.prim_id == 0);

  f2(dir, 0.f, -1.f);
  CHK(s2d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.geom_id == isquare);
  CHK(hit.prim.prim_id == 0);
  f2(dir, 0.f, 1.f);

  CHK(s2d_shape_enable(line, 0) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_TRACE, &scnview) == RES_OK);

  CHK(s2d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.geom_id == iline);
  CHK(hit.prim.prim_id == 0);

  CHK(s2d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(S2D_HIT_NONE(&hit) == 0);
  CHK(hit.prim.geom_id == isquare);
  CHK(hit.prim.prim_id == 2);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview2) == RES_OK);
  CHK(s2d_shape_enable(line, 1) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &scnview) == RES_OK);
  CHK(s2d_scene_view_create(scn2, S2D_TRACE, &scnview2) == RES_OK);

  CHK(s2d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(s2d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit2) == RES_OK);
  CHK(f2_eq(hit.normal, hit2.normal));
  CHK(hit.u == hit2.u);
  CHK(hit.distance == hit2.distance);
  CHK(S2D_PRIMITIVE_EQ(&hit.prim, &hit2.prim) == 1);
  CHK(hit.prim.geom_id == iline);
  CHK(hit.prim.prim_id == 0);

  CHK(s2d_scene_detach_shape(scn2, line) == RES_OK);
  CHK(s2d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit2) == RES_OK);
  CHK(f2_eq(hit.normal, hit2.normal));
  CHK(hit.u == hit2.u);
  CHK(hit.distance == hit2.distance);
  CHK(S2D_PRIMITIVE_EQ(&hit.prim, &hit2.prim) == 1);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview2) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &scnview) == RES_OK);
  CHK(s2d_scene_view_create(scn2, S2D_TRACE, &scnview2) == RES_OK);

  CHK(s2d_scene_view_trace_ray(scnview, org, dir, range, NULL, &hit) == RES_OK);
  CHK(f2_eq(hit.normal, hit2.normal));
  CHK(hit.u == hit2.u);
  CHK(hit.distance == hit2.distance);
  CHK(S2D_PRIMITIVE_EQ(&hit.prim, &hit2.prim) == 1);
  CHK(hit.prim.geom_id == iline);
  CHK(hit.prim.prim_id == 0);

  CHK(s2d_scene_view_trace_ray(scnview2, org, dir, range, NULL, &hit2) == RES_OK);
  CHK(hit2.prim.geom_id == isquare);
  CHK(hit2.prim.prim_id == 2);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview2) == RES_OK);

  CHK(s2d_scene_ref_put(scn) == RES_OK);
  CHK(s2d_scene_ref_put(scn2) == RES_OK);
}

static void
test_sample
  (struct s2d_device* dev,
   struct s2d_shape* square,
   struct s2d_shape* line)
{
  #define NSAMPS 512
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  struct s2d_primitive prims[NSAMPS];
  float u, v, s;
  unsigned isquare;
  unsigned iline;
  int nsamps_square;
  int nsamps_line;
  int i;

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, square) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, line) == RES_OK);
  CHK(s2d_shape_get_id(square, &isquare) == RES_OK);
  CHK(s2d_shape_get_id(line, &iline) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &scnview) == RES_OK);
  CHK(s2d_scene_view_sample(scnview, 0.f, 0.f, &prims[0], &s) == RES_BAD_OP);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_SAMPLE, &scnview) == RES_OK);
  CHK(s2d_scene_view_sample(scnview, 0.f, 0.f, &prims[0], &s) == RES_OK);
  CHK(prims[0].geom_id == isquare || prims[0].geom_id == iline);

  nsamps_square = 0;
  nsamps_line = 0;
  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic();
    CHK(s2d_scene_view_sample(scnview, u, v, &prims[i], &s) == RES_OK);
    if(prims[i].geom_id == isquare) {
      ++nsamps_square;
    } else {
      CHK(prims[i].geom_id == iline);
      ++nsamps_line;
    }
  }
  CHK(nsamps_square != 0);
  CHK(nsamps_line != 0);

  CHK(s2d_shape_enable(square, 0) == RES_OK);
  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    struct s2d_primitive prim;
    u = rand_canonic(), v = rand_canonic();
    CHK(s2d_scene_view_sample(scnview, u, v, &prim, &s) == RES_OK);
    CHK(S2D_PRIMITIVE_EQ(&prim, &prims[i]) == 1);
  }

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_SAMPLE, &scnview) == RES_OK);

  srand(0);
  FOR_EACH(i, 0, NSAMPS) {
    u = rand_canonic(), v = rand_canonic();
    CHK(s2d_scene_view_sample(scnview, u, v, &prims[i], &s) == RES_OK);
    CHK(prims[i].geom_id == iline);
  }

  CHK(s2d_shape_enable(square, 1) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);
}

static void
test_get_primitive
  (struct s2d_device* dev,
   struct s2d_shape* square,
   struct s2d_shape* line)
{
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  struct s2d_primitive prim;
  size_t nprims;
  unsigned i;
  unsigned isquare;
  unsigned iline;
  int square_prims[4];
  int line_prims;

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, square) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, line) == RES_OK);
  CHK(s2d_shape_get_id(square, &isquare) == RES_OK);
  CHK(s2d_shape_get_id(line, &iline) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_TRACE, &scnview) == RES_OK);
  CHK(s2d_scene_view_get_primitive(scnview, 0, &prim) == RES_BAD_OP);
  CHK(s2d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 5);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 5);

  memset(square_prims, 0, sizeof(square_prims));
  line_prims = 0;
  FOR_EACH(i, 0, nprims) {
    CHK(s2d_scene_view_get_primitive(scnview, i, &prim) == RES_OK);
    CHK(prim.scene_prim_id == i);
    if(prim.geom_id == isquare) {
      square_prims[prim.prim_id] = 1;
    } else {
      CHK(prim.geom_id == iline);
      line_prims = 1;
    }
  }
  FOR_EACH(i, 0, 4) CHK(square_prims[i]);
  CHK(line_prims);

  CHK(s2d_scene_detach_shape(scn, square) == RES_OK);
  CHK(s2d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 5);
  memset(square_prims, 0, sizeof(square_prims));
  line_prims = 0;
  FOR_EACH(i, 0, nprims) {
    CHK(s2d_scene_view_get_primitive(scnview, i, &prim) == RES_OK);
    CHK(prim.scene_prim_id == i);
    if(prim.geom_id == isquare) {
      square_prims[prim.prim_id] = 1;
    } else {
      CHK(prim.geom_id == iline);
      line_prims = 1;
    }
  }
  FOR_EACH(i, 0, 4) CHK(square_prims[i]);
  CHK(line_prims);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 1);
  line_prims = 0;
  CHK(s2d_scene_view_get_primitive(scnview, 0, &prim) == RES_OK);
  CHK(prim.scene_prim_id == 0);
  CHK(prim.geom_id == iline);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, square) == RES_OK);
  CHK(s2d_shape_enable(line, 0) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_primitives_count(scnview, &nprims) == RES_OK);
  CHK(nprims == 4);
  memset(square_prims, 0, sizeof(square_prims));
  FOR_EACH(i, 0, nprims) {
    CHK(s2d_scene_view_get_primitive(scnview, i, &prim) == RES_OK);
    CHK(prim.scene_prim_id == i);
    CHK(prim.geom_id == isquare);
    square_prims[prim.prim_id] = 1;
  }
  FOR_EACH(i, 0, 4) CHK(square_prims[i]);

  CHK(s2d_shape_enable(line, 1) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_ref_put(scn) == RES_OK);
}

static void
test_contour_length
  (struct s2d_device* dev,
   struct s2d_shape* square,
   struct s2d_shape* line)
{
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  float length;

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, square) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, line) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_compute_contour_length(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_compute_contour_length(scnview, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_compute_contour_length(NULL, &length) == RES_BAD_ARG);
  CHK(s2d_scene_view_compute_contour_length(scnview, &length) == RES_OK);
  CHK(eq_epsf(length, 10.f, 1.e-6f));

  CHK(s2d_shape_enable(line, 0) == RES_OK);
  CHK(s2d_scene_view_compute_contour_length(scnview, &length) == RES_OK);
  CHK(eq_epsf(length, 10.f, 1.e-6f));

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_compute_contour_length(scnview, &length) == RES_OK);
  CHK(eq_epsf(length, 8.f, 1.e-6f));

  CHK(s2d_shape_enable(line, 1) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);

  CHK(s2d_scene_ref_put(scn) == RES_OK);
}

static void
test_area
  (struct s2d_device* dev, struct s2d_shape* square)
{
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  float area;

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, square) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_compute_area(NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_compute_area(scnview, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_compute_area(NULL, &area) == RES_BAD_ARG);
  CHK(s2d_scene_view_compute_area(scnview, &area) == RES_OK);
  CHK(eq_epsf(area, 4.f, 1.e-6f));

  CHK(s2d_shape_flip_contour(square) == RES_OK);
  CHK(s2d_scene_view_compute_area(scnview, &area) == RES_OK);
  CHK(eq_epsf(area, 4.f, 1.e-6f));

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_compute_area(scnview, &area) == RES_OK);
  CHK(eq_epsf(area, -4.f, 1.e-6f));

  CHK(s2d_shape_flip_contour(square) == RES_OK);
  CHK(s2d_shape_enable(square, 0) == RES_OK);
  CHK(s2d_scene_view_compute_area(scnview, &area) == RES_OK);
  CHK(eq_epsf(area, -4.f, 1.e-6f));

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_compute_area(scnview, &area) == RES_OK);
  CHK(area == 0.f);

  CHK(s2d_shape_enable(square, 1) == RES_OK);

  CHK(s2d_scene_ref_put(scn) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
}

static void
test_aabb
  (struct s2d_device* dev, struct s2d_shape* square, struct s2d_shape* line)
{
  struct s2d_scene* scn;
  struct s2d_scene_view* scnview;
  float lower[2];
  float upper[2];

  CHK(s2d_scene_create(dev, &scn) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, square) == RES_OK);
  CHK(s2d_scene_attach_shape(scn, line) == RES_OK);

  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_get_aabb(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_get_aabb(scnview, NULL, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_get_aabb(NULL, lower, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_get_aabb(scnview, lower, NULL) == RES_BAD_ARG);
  CHK(s2d_scene_view_get_aabb(NULL, lower, upper) == RES_BAD_ARG);
  CHK(s2d_scene_view_get_aabb(scnview, lower, upper) == RES_OK);
  CHK(lower[0] == 9.f && upper[0] == 11.f);
  CHK(lower[1] == 9.f && upper[1] == 11.f);

  CHK(s2d_shape_enable(square, 0) == RES_OK);
  CHK(s2d_scene_view_get_aabb(scnview, lower, upper) == RES_OK);
  CHK(lower[0] == 9.f && upper[0] == 11.f);
  CHK(lower[1] == 9.f && upper[1] == 11.f);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_get_aabb(scnview, lower, upper) == RES_OK);
  CHK(lower[0] == 9.f && upper[0] == 11.f);
  CHK(lower[1] == 10.f && upper[1] == 10.f);

  CHK(s2d_scene_detach_shape(scn, line) == RES_OK);
  CHK(s2d_scene_view_get_aabb(scnview, lower, upper) == RES_OK);
  CHK(lower[0] == 9.f && upper[0] == 11.f);
  CHK(lower[1] == 10.f && upper[1] == 10.f);

  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
  CHK(s2d_scene_view_create(scn, S2D_GET_PRIMITIVE, &scnview) == RES_OK);
  CHK(s2d_scene_view_get_aabb(scnview, lower, upper) == RES_OK);
  CHK(lower[0] > upper[0]);
  CHK(lower[1] > upper[1]);

  CHK(s2d_shape_enable(square, 1) == RES_OK);

  CHK(s2d_scene_ref_put(scn) == RES_OK);
  CHK(s2d_scene_view_ref_put(scnview) == RES_OK);
}

/*******************************************************************************
 * Main test function
 ******************************************************************************/
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct s2d_device* dev;
  struct s2d_shape* square;
  struct s2d_shape* line;
  struct s2d_vertex_data vdata;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(s2d_device_create(NULL, &allocator, 1, &dev) == RES_OK);

  vdata.type = S2D_FLOAT2;
  vdata.usage = S2D_POSITION;
  vdata.get = line_segments_get_position;

  CHK(s2d_shape_create_line_segments(dev, &square) == RES_OK);
  CHK(s2d_line_segments_setup_indexed_vertices(square, square_nsegs,
    line_segments_get_ids, square_nverts, &vdata, 1, (void*)&square_desc)
    == RES_OK);

  CHK(s2d_shape_create_line_segments(dev, &line) == RES_OK);
  CHK(s2d_line_segments_setup_indexed_vertices(line, line_nsegs,
    line_segments_get_ids, line_nverts, &vdata, 1, (void*)&line_desc)
    == RES_OK);

  test_miscellaneous(dev, square, line);
  test_trace_ray(dev, square, line);
  test_sample(dev, square, line);
  test_get_primitive(dev, square, line);
  test_contour_length(dev, square, line);
  test_area(dev, square);
  test_aabb(dev, square, line);

  CHK(s2d_shape_ref_put(square) == RES_OK);
  CHK(s2d_shape_ref_put(line) == RES_OK);
  CHK(s2d_device_ref_put(dev) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

