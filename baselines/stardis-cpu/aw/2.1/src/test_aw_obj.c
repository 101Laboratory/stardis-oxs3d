/* Copyright (C) 2014-2017, 2020-2023 Vincent Forest (vaplv@free.fr)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#include "aw.h"

#include <rsys/double3.h>
#include <rsys/double4.h>
#include <rsys/logger.h>
#include <rsys/mem_allocator.h>

#include <string.h>

static void
test_plane(struct aw_obj* obj)
{
  static const char* plane_obj =
    "mtllib master.mtl"
    "\n"
    "g\n"
    "o plane\n"
    "v 0.0000 2.0000 0.0000\n"
    "v 0.0000 0.0000 0.0000\n"
    "v 2.0000 0.0000 0.0000\n"
    "v 2.0000 2.0000 0.0000\n"
    "vt 0.0000 1.0000 0.0000\n"
    "vt 0.0000 0.0000 0.0000\n"
    "vt 1.0000 0.0000 0.0000\n"
    "vt 1.0000 1.0000 0.0000\n"
    "# 4 vertices\n"
    "\n"
    "usemtl wood\n"
    "f 1/1 2/2 3/3 4/4\n"
    "# 1 element\n";
  double v4[4];
  struct aw_obj_desc desc;
  struct aw_obj_face face;
  struct aw_obj_named_group group;
  struct aw_obj_named_group mtl;
  struct aw_obj_vertex vertex;
  struct aw_obj_vertex_data vdata;
  const double* data = NULL;
  FILE* file;
  const char* mtllib;

  CHK(obj != NULL);

  file = fopen("test_obj_plane.obj", "w");
  CHK(file != NULL);
  fwrite(plane_obj, sizeof(char), strlen(plane_obj), file);
  CHK(fclose(file) == 0);

  CHK(aw_obj_load(obj, NULL) == RES_BAD_ARG);
  CHK(aw_obj_load(NULL, "test_obj_plane.obj") == RES_BAD_ARG);
  CHK(aw_obj_load(obj, "none.obj") == RES_IO_ERR);
  CHK(aw_obj_load(obj, "test_obj_plane.obj") == RES_OK);

  CHK(aw_obj_get_desc(NULL, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_desc(obj, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_desc(NULL, &desc) == RES_BAD_ARG);
  CHK(aw_obj_get_desc(obj, &desc) == RES_OK);
  CHK(desc.faces_count == 1);
  CHK(desc.positions_count == 4);
  CHK(desc.texcoords_count == 4);
  CHK(desc.normals_count == 0);
  CHK(desc.groups_count == 1);
  CHK(desc.smooth_groups_count == 0);
  CHK(desc.usemtls_count == 1);
  CHK(desc.mtllibs_count == 1);

  CHK(aw_obj_get_face(NULL, 0, &face) == RES_BAD_ARG);
  CHK(aw_obj_get_face(obj, AW_ID_NONE, &face) == RES_BAD_ARG);
  CHK(aw_obj_get_face(obj, 0, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_face(obj, 0, &face) == RES_OK);
  CHK(face.vertex_id == 0);
  CHK(face.vertices_count == 4);
  CHK(face.group_id == 0);
  CHK(face.smooth_group_id == AW_ID_NONE);
  CHK(face.mtl_id == 0);

  CHK(aw_obj_get_mtl(NULL, 0, &mtl) == RES_BAD_ARG);
  CHK(aw_obj_get_mtl(obj, AW_ID_NONE, &mtl) == RES_BAD_ARG);
  CHK(aw_obj_get_mtl(obj, 0, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_mtl(obj, 0, &mtl) == RES_OK);
  CHK(!strcmp(mtl.name, "wood"));
  CHK(mtl.face_id == 0);
  CHK(mtl.faces_count == 1);

  CHK(aw_obj_get_mtllib(NULL, 0, &mtllib) == RES_BAD_ARG);
  CHK(aw_obj_get_mtllib(obj, AW_ID_NONE, &mtllib) == RES_BAD_ARG);
  CHK(aw_obj_get_mtllib(obj, 0, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_mtllib(obj, 0, &mtllib) == RES_OK);
  CHK(strcmp(mtllib, "master.mtl") == 0);

  CHK(aw_obj_get_vertex(NULL, 0, &vertex) == RES_BAD_ARG);
  CHK(aw_obj_get_vertex(obj, AW_ID_NONE, &vertex) == RES_BAD_ARG);
  CHK(aw_obj_get_vertex(obj, 0, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_vertex(obj, 0, &vertex) == RES_OK);

  CHK(aw_obj_get_vertex_data(NULL, &vertex, &vdata) == RES_BAD_ARG);
  CHK(aw_obj_get_vertex_data(obj, NULL, &vdata) == RES_BAD_ARG);
  CHK(aw_obj_get_vertex_data(obj, &vertex, NULL) == RES_BAD_ARG);

  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 0, 2, 0, 1)));
  CHK(d3_eq(vdata.texcoord, d3(v4, 0, 1, 0)));

  vertex.position_id = 4;
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_BAD_ARG);
  vertex.position_id = 0;
  vertex.texcoord_id = 4;
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_BAD_ARG);
  vertex.texcoord_id = 0;
  vertex.normal_id = 0;
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_BAD_ARG);

  CHK(aw_obj_get_vertex(obj, 1, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 0, 0, 0, 1)));
  CHK(d3_eq(vdata.texcoord, d3(v4, 0, 0, 0)));

  CHK(aw_obj_get_vertex(obj, 2, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 2, 0, 0, 1)));
  CHK(d3_eq(vdata.texcoord, d3(v4, 1, 0, 0)));

  CHK(aw_obj_get_vertex(obj, 3, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 2, 2, 0, 1)));
  CHK(d3_eq(vdata.texcoord, d3(v4, 1, 1, 0)));

  CHK(aw_obj_get_group(NULL, 0, &group) == RES_BAD_ARG);
  CHK(aw_obj_get_group(obj, AW_ID_NONE, &group) == RES_BAD_ARG);
  CHK(aw_obj_get_group(obj, 0, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_group(obj, 0, &group) == RES_OK);
  CHK(!strcmp(group.name, "default"));
  CHK(group.face_id == 0);
  CHK(group.faces_count == 1);

  CHK(aw_obj_get_positions(NULL, &data) == RES_BAD_ARG);
  CHK(aw_obj_get_positions(obj, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_positions(obj, &data) == RES_OK);
  CHK(d4_eq(data+0, d4(v4,0,2,0,1)));
  CHK(d4_eq(data+4, d4(v4,0,0,0,1)));
  CHK(d4_eq(data+8, d4(v4,2,0,0,1)));
  CHK(d4_eq(data+12, d4(v4,2,2,0,1)));

  CHK(aw_obj_get_texcoords(NULL, &data) == RES_BAD_ARG);
  CHK(aw_obj_get_texcoords(obj, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_texcoords(obj, &data) == RES_OK);
  CHK(d3_eq(data+0, d3(v4,0,1,0)));
  CHK(d3_eq(data+3, d3(v4,0,0,0)));
  CHK(d3_eq(data+6, d3(v4,1,0,0)));
  CHK(d3_eq(data+9, d3(v4,1,1,0)));
}

static void
test_squares(struct aw_obj* obj)
{
  static const char* squares_obj =
    "v 0.000000 2.000000 0.000000\n"
    "v 0.000000 0.000000 0.000000\n"
    "v 2.000000 0.000000 0.000000\n"
    "v 2.000000 2.000000 0.000000\n"
    "v 4.000000 0.000000 -1.255298\n"
    "v 4.000000 2.000000 -1.255298\n"
    "vn 0.000000 0.000000 1.000000\n"
    "vn 0.000000 0.000000 1.000000\n"
    "vn 0.276597 0.000000 0.960986\n"
    "vn 0.276597 0.000000 0.960986\n"
    "vn 0.531611 0.000000 0.846988\n"
    "vn 0.531611 0.000000 0.846988\n"
    "# 6 vertices\n"
    "\n"
    "# 6 normals\n"
    "\n"
    "g all\n"
    "s 1\n"
    "f 1//1 2//2 3//3 4//4\n"
    "f 4//4 3//3 5//5 6//6\n"
    "# 2 elements\n";
  double v4[4];
  struct aw_obj_desc desc;
  struct aw_obj_face face;
  struct aw_obj_named_group group;
  struct aw_obj_named_group mtl;
  struct aw_obj_smooth_group sgroup;
  struct aw_obj_vertex vertex;
  struct aw_obj_vertex_data vdata;
  const double* data = NULL;
  FILE* file;

  CHK(obj != NULL);

  file = fopen("test_obj_squares.obj", "w");
  CHK(file != NULL);
  fwrite(squares_obj, sizeof(char), strlen(squares_obj), file);
  CHK(fclose(file) == 0);
  CHK(aw_obj_load(obj, "test_obj_squares.obj") == RES_OK);

  CHK(aw_obj_get_desc(obj, &desc) == RES_OK);
  CHK(desc.faces_count == 2);
  CHK(desc.positions_count == 6);
  CHK(desc.texcoords_count == 0);
  CHK(desc.normals_count == 6);
  CHK(desc.groups_count == 1);
  CHK(desc.smooth_groups_count == 1);
  CHK(desc.usemtls_count == 0);
  CHK(desc.mtllibs_count == 0);

  CHK(aw_obj_get_face(obj, 0, &face) == RES_OK);
  CHK(face.vertex_id == 0);
  CHK(face.vertices_count == 4);
  CHK(face.group_id == 0);
  CHK(face.smooth_group_id == 0);
  CHK(face.mtl_id == AW_ID_NONE);

  CHK(aw_obj_get_vertex(obj, 0, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 0.0, 2.0, 0.0, 1.0)));
  CHK(d3_eq(vdata.normal, d3(v4, 0.0, 0.0, 1.0)));
  CHK(aw_obj_get_vertex(obj, 1, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 0.0, 0.0, 0.0, 1.0)));
  CHK(d3_eq(vdata.normal, d3(v4, 0.0, 0.0, 1.0)));
  CHK(aw_obj_get_vertex(obj, 2, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 2.0, 0.0, 0.0, 1.0)));
  CHK(d3_eq(vdata.normal, d3(v4, 0.276597, 0.0, 0.960986)));
  CHK(aw_obj_get_vertex(obj, 3, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 2.0, 2.0, 0.0, 1.0)));
  CHK(d3_eq(vdata.normal, d3(v4, 0.276597, 0.0, 0.960986)));

  CHK(aw_obj_get_face(obj, 1, &face) == RES_OK);
  CHK(face.vertex_id == 4);
  CHK(face.vertices_count == 4);
  CHK(face.group_id == 0);
  CHK(face.smooth_group_id == 0);
  CHK(face.mtl_id == AW_ID_NONE);

  CHK(aw_obj_get_vertex(obj, 4, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 2.0, 2.0, 0.0, 1.0)));
  CHK(d3_eq(vdata.normal, d3(v4, 0.276597, 0.0, 0.960986)));
  CHK(aw_obj_get_vertex(obj, 5, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 2.0, 0.0, 0.0, 1.0)));
  CHK(d3_eq(vdata.normal, d3(v4, 0.276597, 0.0, 0.960986)));
  CHK(aw_obj_get_vertex(obj, 6, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 4.0, 0.0, -1.255298, 1.0)));
  CHK(d3_eq(vdata.normal, d3(v4, 0.531611, 0.0, 0.8469880)));
  CHK(aw_obj_get_vertex(obj, 7, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d4_eq(vdata.position, d4(v4, 4.0, 2.0, -1.255298, 1.0)) == 1);
  CHK(d3_eq(vdata.normal, d3(v4, 0.531611, 0.0, 0.846988)) == 1);

  CHK(aw_obj_get_group(NULL, 0, &group) == RES_BAD_ARG);
  CHK(aw_obj_get_group(obj, AW_ID_NONE, &group) == RES_BAD_ARG);
  CHK(aw_obj_get_group(obj, 0, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_group(obj, 0, &group) == RES_OK);
  CHK(strcmp(group.name, "all") == 0);
  CHK(group.face_id == 0);
  CHK(group.faces_count == 2);

  CHK(aw_obj_get_smooth_group(NULL, 0, &sgroup) == RES_BAD_ARG);
  CHK(aw_obj_get_smooth_group(obj, AW_ID_NONE, &sgroup) == RES_BAD_ARG);
  CHK(aw_obj_get_smooth_group(obj, 0, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_smooth_group(obj, 0, &sgroup) == RES_OK);
  CHK(sgroup.is_smoothed == 1);
  CHK(sgroup.face_id == 0);
  CHK(sgroup.faces_count == 2);

  CHK(aw_obj_get_mtl(obj, 0, &mtl) == RES_BAD_ARG);

  CHK(aw_obj_get_positions(obj, &data) == RES_OK);
  CHK(d4_eq(data+0, d4(v4,0,2,0,1)));
  CHK(d4_eq(data+4, d4(v4,0,0,0,1)));
  CHK(d4_eq(data+8, d4(v4,2,0,0,1)));
  CHK(d4_eq(data+12, d4(v4,2,2,0,1)));
  CHK(d4_eq(data+16, d4(v4,4,0,-1.255298,1)));
  CHK(d4_eq(data+20, d4(v4,4,2,-1.255298,1)));

  CHK(aw_obj_get_normals(NULL, &data) == RES_BAD_ARG);
  CHK(aw_obj_get_normals(obj, NULL) == RES_BAD_ARG);
  CHK(aw_obj_get_normals(obj, &data) == RES_OK);
  CHK(d3_eq(data+0, d3(v4,0,0,1)));
  CHK(d3_eq(data+3, d3(v4,0,0,1)));
  CHK(d3_eq(data+6, d3(v4,0.276597,0,0.960986)));
  CHK(d3_eq(data+9, d3(v4,0.276597,0,0.960986)));
  CHK(d3_eq(data+12, d3(v4,0.531611,0,0.846988)));
  CHK(d3_eq(data+15, d3(v4,0.531611,0,0.846988)));
}

static void
test_cube(struct aw_obj* obj)
{
  static const char* cube_obj =
    "# Cube with a different material applied to each of its faces\n"
    "mtllib master.mtl hop.mtl\n"
    "mtllib my.mtl\n"
    "v\t0.0000 2.0000 2.0000\n"
    "v 0.0000 0.0000 2.0000\n"
    "v 2.0000 0.0000 2.0000\n"
    "v 2.0000 2.0000 2.0000\n"
    "v 0.0000 2.0000 0.0000\n"
    "v 0.0000 0.0000 0.0000\n"
    "v 2.0000 0.0000 0.0000\n"
    "v 2.0000 2.0000 0.0000\n"
    "# 8 vertices\n"
    "g front\n"
    "usemtl red\n"
    "f 1\t\t\t 2 3 4\n"
    "g back\n"
    "usemtl blue\n"
    "f 8 7 6 5\n"
    "g right\n"
    "usemtl green\n"
    "f 4 3 7 8\n"
    "g top\n"
    "usemtl\tgold\n"
    "f 5 1\t4 8\n"
    "g left\n"
    "usemtl orange\n"
    "f 5 6 2 1\n"
    "g bottom\n"
    "usemtl purple\n"
    "f 2 6 7 3\n"
    "# 6 elements\n";
  double v4[4];
  const char* group_names[6] =
    { "front", "back", "right", "top", "left", "bottom" };
  const char* mtl_names[6] =
    { "red", "blue", "green", "gold", "orange", "purple" };
  struct aw_obj_desc desc;
  FILE* file;
  const char* mtllib;
  const double* data;
  size_t i;

  CHK(obj != NULL);

  file = fopen("test_obj_cube.obj", "w+");
  CHK(file != NULL);
  fwrite(cube_obj, sizeof(char), strlen(cube_obj), file);
  CHK(fseek(file, 0, SEEK_SET) == 0);

  CHK(aw_obj_load_stream(obj, NULL, NULL) == RES_BAD_ARG);
  CHK(aw_obj_load_stream(NULL, file, NULL) == RES_BAD_ARG);
  CHK(aw_obj_load_stream(obj, file, NULL) == RES_OK);

  CHK(aw_obj_get_desc(obj, &desc) == RES_OK);
  CHK(desc.faces_count == 6);
  CHK(desc.positions_count == 8);
  CHK(desc.texcoords_count == 0);
  CHK(desc.normals_count == 0);
  CHK(desc.groups_count == 6);
  CHK(desc.smooth_groups_count == 0);
  CHK(desc.usemtls_count == 6);
  CHK(desc.mtllibs_count == 3);

  CHK(aw_obj_clear(NULL) == RES_BAD_ARG);
  CHK(aw_obj_clear(obj) == RES_OK);
  CHK(aw_obj_get_desc(obj, &desc) == RES_OK);
  CHK(desc.faces_count == 0);
  CHK(desc.groups_count == 0);
  CHK(desc.smooth_groups_count == 0);
  CHK(desc.usemtls_count == 0);
  CHK(desc.mtllibs_count == 0);

  CHK(fseek(file, 0, SEEK_SET) == 0);
  CHK(aw_obj_load_stream(obj, file, "cube") == RES_OK);
  CHK(aw_obj_get_desc(obj, &desc) == RES_OK);

  FOR_EACH(i, 0, 6) {
    struct aw_obj_face face;
    CHK(aw_obj_get_face(obj, i, &face) == RES_OK);
    CHK(face.vertex_id == i*4);
    CHK(face.vertices_count == 4);
    CHK(face.group_id == i);
    CHK(face.smooth_group_id == AW_ID_NONE);
    CHK(face.mtl_id == i);
  }

  FOR_EACH(i, 0, 6) {
    struct aw_obj_named_group group;
    CHK(aw_obj_get_group(obj, i, &group) == RES_OK);
    CHK(!strcmp(group.name, group_names[i]));
    CHK(group.face_id == i);
    CHK(group.faces_count == 1);
  }

  FOR_EACH(i, 0, 6) {
    struct aw_obj_named_group mtl;
    CHK(aw_obj_get_mtl(obj, i, &mtl) == RES_OK);
    CHK(!strcmp(mtl.name, mtl_names[i]));
    CHK(mtl.face_id == i);
    CHK(mtl.faces_count == 1);
  }

  CHK(aw_obj_get_mtllib(obj, 0, &mtllib) == RES_OK);
  CHK(!strcmp(mtllib, "master.mtl"));
  CHK(aw_obj_get_mtllib(obj, 1, &mtllib) == RES_OK);
  CHK(!strcmp(mtllib, "hop.mtl"));
  CHK(aw_obj_get_mtllib(obj, 2, &mtllib) == RES_OK);
  CHK(!strcmp(mtllib, "my.mtl"));
  CHK(aw_obj_get_mtllib(obj, 3, &mtllib) == RES_BAD_ARG);

  CHK(aw_obj_get_positions(obj, &data) == RES_OK);
  CHK(d4_eq(data+0, d4(v4,0,2,2,1)));
  CHK(d4_eq(data+4, d4(v4,0,0,2,1)));
  CHK(d4_eq(data+8, d4(v4,2,0,2,1)));
  CHK(d4_eq(data+12, d4(v4,2,2,2,1)));
  CHK(d4_eq(data+16, d4(v4,0,2,0,1)));
  CHK(d4_eq(data+20, d4(v4,0,0,0,1)));
  CHK(d4_eq(data+24, d4(v4,2,0,0,1)));
  CHK(d4_eq(data+28, d4(v4,2,2,0,1)));

  CHK(aw_obj_purge(NULL) == RES_BAD_ARG);
  CHK(aw_obj_purge(obj) == RES_OK);
  CHK(aw_obj_get_desc(obj, &desc) == RES_OK);
  CHK(desc.faces_count == 0);
  CHK(desc.groups_count == 0);
  CHK(desc.smooth_groups_count == 0);
  CHK(desc.usemtls_count == 0);
  CHK(desc.mtllibs_count == 0);

  CHK(fclose(file) == 0);
}

static void
test_cbox(struct aw_obj* obj)
{
  static const char* cbox_obj =
    "mtllib cbox.mtl\n"
    "v -1.01 0 0.99\n"
    "v 1 0 0.99\n"
    "v 1 0 -1.04\n"
    "v -0.99  0 -1.04\n"
    "g floor\n"
    "usemtl floor\n"
    "f -4 -3 -2 -1\n"

    "v -1.02 1.99 0.99\n"
    "v -1.02 1.99 -1.04\n"
    "v 1 1.99 -1.04\n"
    "v 1 1.99 0.99\n"
    "g ceiling\n"
    "usemtl ceiling\n"
    "f -4 -3 -2 -1\n"

    "v -0.99 0 -1.04\n"
    "v 1 0 -1.04\n"
    "v 1 1.99 -1.04\n"
    "v -1.02 1.99 -1.04\n"
    "g back\n"
    "usemtl back\n"
    "f -4 -3 -2 -1\n"

    "v 1 0 -1.04\n"
    "v 1 0 0.99\n"
    "v 1 1.99 0.99\n"
    "v 1 1.99 -1.04\n"
    "g right\n"
    "usemtl right\n"
    "f -4 -3 -2 -1\n"

    "v -1.01 0 0.99\n"
    "v -0.99 0 -1.04\n"
    "v -1.02 1.99 -1.04\n"
    "v -1.02 1.99 0.99\n"
    "g left\n"
    "usemtl left\n"
    "f -4 -3 -2 -1";
  struct aw_obj_desc desc;
  struct aw_obj_face face;
  struct aw_obj_vertex vertex;
  struct aw_obj_vertex_data vdata;
  struct aw_obj_named_group group;
  struct aw_obj_named_group mtl;
  double v4[4];
  double tmp[3];
  const double* data;
  FILE* file;

  CHK(obj != NULL);

  file = fopen("test_cbox.obj", "w+");
  CHK(file != NULL);
  fwrite(cbox_obj, sizeof(char), strlen(cbox_obj), file);
  CHK(fseek(file, 0, SEEK_SET) == 0);
  CHK(aw_obj_load_stream(obj, file, "cbox") == RES_OK);
  CHK(fclose(file) == 0);

  CHK(aw_obj_get_desc(obj, &desc) == RES_OK);
  CHK(desc.faces_count == 5);
  CHK(desc.positions_count == 20);
  CHK(desc.texcoords_count == 0);
  CHK(desc.normals_count == 0);
  CHK(desc.groups_count == 5);
  CHK(desc.smooth_groups_count == 0);
  CHK(desc.usemtls_count == 5);
  CHK(desc.mtllibs_count == 1);

  CHK(aw_obj_get_face(obj, 0, &face) == RES_OK);
  CHK(face.vertex_id == 0);
  CHK(face.vertices_count == 4);
  CHK(face.group_id == 0);
  CHK(face.mtl_id == 0);

  CHK(aw_obj_get_group(obj, 0, &group) == RES_OK);
  CHK(!strcmp(group.name, "floor"));
  CHK(group.face_id == 0);
  CHK(group.faces_count == 1);

  CHK(aw_obj_get_mtl(obj, 0, &mtl) == RES_OK);
  CHK(!strcmp(mtl.name, "floor"));
  CHK(mtl.face_id == 0);
  CHK(mtl.faces_count == 1);

  CHK(aw_obj_get_vertex(obj, 0, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, -1.01, 0.0, 0.99), 1.e-6));
  CHK(aw_obj_get_vertex(obj, 1, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, 1.0, 0.0, 0.99), 1.e-6));
  CHK(aw_obj_get_vertex(obj, 2, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, 1.f, 0.0, -1.04), 1.e-6));
  CHK(aw_obj_get_vertex(obj, 3, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, -0.99, 0.0, -1.04), 1.e-6));

  CHK(aw_obj_get_face(obj, 1, &face) == RES_OK);
  CHK(face.vertex_id == 4);
  CHK(face.vertices_count == 4);
  CHK(face.group_id == 1);
  CHK(face.mtl_id == 1);

  CHK(aw_obj_get_group(obj, 1, &group) == RES_OK);
  CHK(!strcmp(group.name, "ceiling"));
  CHK(group.face_id == 1);
  CHK(group.faces_count == 1);

  CHK(aw_obj_get_mtl(obj, 1, &mtl) == RES_OK);
  CHK(!strcmp(mtl.name, "ceiling"));
  CHK(mtl.face_id == 1);
  CHK(mtl.faces_count == 1);

  CHK(aw_obj_get_vertex(obj, 4, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, -1.02, 1.99, 0.99), 1.e-6));
  CHK(aw_obj_get_vertex(obj, 5, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, -1.02, 1.99, -1.04), 1.e-6f));
  CHK(aw_obj_get_vertex(obj, 6, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, 1.0, 1.99, -1.04), 1.e-6f));
  CHK(aw_obj_get_vertex(obj, 7, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, 1.0, 1.99, 0.99), 1.e-6f));

  CHK(aw_obj_get_face(obj, 4, &face) == RES_OK);
  CHK(face.vertex_id == 16);
  CHK(face.vertices_count == 4);
  CHK(face.group_id == 4);
  CHK(face.mtl_id == 4);

  CHK(aw_obj_get_group(obj, 4, &group) == RES_OK);
  CHK(!strcmp(group.name, "left"));
  CHK(group.face_id == 4);
  CHK(group.faces_count == 1);

  CHK(aw_obj_get_mtl(obj, 4, &mtl) == RES_OK);
  CHK(!strcmp(mtl.name, "left"));
  CHK(mtl.face_id == 4);
  CHK(mtl.faces_count == 1);

  CHK(aw_obj_get_vertex(obj, 16, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, -1.01, 0.0, 0.99), 1.e-6));
  CHK(aw_obj_get_vertex(obj, 17, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, -0.99, 0.0, -1.04), 1.e-6));
  CHK(aw_obj_get_vertex(obj, 18, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, -1.02, 1.99, -1.04), 1.e-6));
  CHK(aw_obj_get_vertex(obj, 19, &vertex) == RES_OK);
  CHK(aw_obj_get_vertex_data(obj, &vertex, &vdata) == RES_OK);
  CHK(d3_eq_eps(vdata.position, d3(tmp, -1.02, 1.99, 0.99), 1.e-6));

  CHK(aw_obj_get_positions(obj, &data) == RES_OK);
  CHK(d4_eq(data+0, d4(v4,-1.01,0,0.99,1)));
  CHK(d4_eq(data+4, d4(v4,1,0,0.99,1)));
  CHK(d4_eq(data+8, d4(v4,1,0,-1.04,1)));
  CHK(d4_eq(data+12, d4(v4,-0.99,0,-1.04,1)));
  CHK(d4_eq(data+16, d4(v4,-1.02,1.99,0.99,1)));
  CHK(d4_eq(data+20, d4(v4,-1.02,1.99,-1.04,1)));
  CHK(d4_eq(data+24, d4(v4,1,1.99,-1.04,1)));
  CHK(d4_eq(data+28, d4(v4,1,1.99,0.99,1)));
  CHK(d4_eq(data+32, d4(v4,-0.99,0,-1.04,1)));
  CHK(d4_eq(data+36, d4(v4,1,0,-1.04,1)));
  CHK(d4_eq(data+40, d4(v4,1,1.99,-1.04,1)));
  CHK(d4_eq(data+44, d4(v4,-1.02,1.99,-1.04,1)));
  CHK(d4_eq(data+48, d4(v4,1,0,-1.04,1)));
  CHK(d4_eq(data+52, d4(v4,1,0,0.99,1)));
  CHK(d4_eq(data+56, d4(v4,1,1.99,0.99,1)));
  CHK(d4_eq(data+60, d4(v4,1,1.99,-1.04,1)));
  CHK(d4_eq(data+64, d4(v4,-1.01,0,0.99,1)));
  CHK(d4_eq(data+68, d4(v4,-0.99,0,-1.04,1)));
  CHK(d4_eq(data+72, d4(v4,-1.02,1.99,-1.04,1)));
  CHK(d4_eq(data+76, d4(v4,-1.02,1.99,0.99,1)));
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct aw_obj* obj;
  int i;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(aw_obj_create(LOGGER_DEFAULT, NULL, 1, NULL) == RES_BAD_ARG);
  CHK(aw_obj_create(LOGGER_DEFAULT, &allocator, 1, NULL) == RES_BAD_ARG);
  CHK(aw_obj_create(LOGGER_DEFAULT, NULL, 1, &obj) == RES_OK);

  CHK(aw_obj_ref_get(NULL) == RES_BAD_ARG);
  CHK(aw_obj_ref_get(obj) == RES_OK);
  CHK(aw_obj_ref_put(NULL) == RES_BAD_ARG);
  CHK(aw_obj_ref_put(obj) == RES_OK);
  CHK(aw_obj_ref_put(obj) == RES_OK);

  CHK(aw_obj_create(NULL, &allocator, 1, &obj) == RES_OK);

  test_plane(obj);
  test_squares(obj);
  test_cube(obj);
  test_cbox(obj);
  FOR_EACH(i, 1, argc)
    CHK(aw_obj_load(obj, argv[i]) == RES_OK);

  CHK(aw_obj_ref_put(obj) == RES_OK);

  if(MEM_ALLOCATED_SIZE(&allocator)) {
    char dump[512];
    MEM_DUMP(&allocator, dump, sizeof(dump)/sizeof(char));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks\n");
  }
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

