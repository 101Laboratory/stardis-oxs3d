/* Copyright (C) 2015, 2016, 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#include "s3daw.h"

#include <rsys/logger.h>
#include<rsys/mem_allocator.h>
#include <star/s3d.h>

static void
test_cbox(struct s3daw* s3daw)
{
  const char* cbox_obj =
    "mtllib cbox.mtl cbox2.mtl\n"
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
    "f -4 -3 -2 -1\n";

  const char* cbox_mtl =
    "newmtl left\n"
    "Ns 10\n"
    "Ni 1.5\n"
    "illum 2\n"
    "Ka 0.63 0.065 0.05\n"
    "Kd 0.63 0.065 0.05\n"
    "Ks 0 0 0\n"
    "Ke 0 0 0\n"

    "newmtl right\n"
    "Ns 10\n"
    "Ni 1.5\n"
    "illum 2\n"
    "Ka 0.14 0.45 0.091\n"
    "Kd 0.14 0.45 0.091\n"
    "Ks 0 0 0\n"
    "Ke 0 0 0\n"

    "newmtl floor\n"
    "Ns 10\n"
    "Ni 1\n"
    "illum 2\n"
    "Ka 0.725 0.71 0.68\n"
    "Kd 0.725 0.71 0.68\n"
    "Ks 0 0 0\n"
    "Ke 0 0 0\n"

    "newmtl ceiling\n"
    "Ns 10\n"
    "Ni 1\n"
    "illum 2\n"
    "Ka 0.725 0.71 0.68\n"
    "Kd 0.725 0.71 0.68\n"
    "Ks 0 0 0\n"
    "Ke 0 0 0\n";

  const char* cbox2_mtl =
    "newmtl back\n"
    "Ns 10\n"
    "Ni 1\n"
    "illum 2\n"
    "Ka 0.725 0.71 0.68\n"
    "Kd 0.725 0.71 0.68\n"
    "Ks 0 0 0\n"
    "Ke 0 0 0\n";

  FILE* file;
  struct s3d_device* s3d;
  struct s3d_shape* shape;
  struct s3d_scene* scene = NULL;
  size_t ishape, nshapes;

  ASSERT(s3daw);

  file = fopen("cbox.obj", "w");

  CHK(file != NULL);
  fwrite(cbox_obj, sizeof(char), strlen(cbox_obj), file);
  fclose(file);

  file = fopen("cbox.mtl", "w");
  CHK(file != NULL);
  fwrite(cbox_mtl, sizeof(char), strlen(cbox_mtl), file);
  fclose(file);

  remove("cbox2.mtl");

  CHK(s3daw_load(NULL, NULL) == RES_BAD_ARG);
  CHK(s3daw_load(s3daw, NULL) == RES_BAD_ARG);
  CHK(s3daw_load(NULL, "cbox.obj") == RES_BAD_ARG);
  CHK(s3daw_load(s3daw, "cbox.obj__") == RES_IO_ERR);
  CHK(s3daw_load(s3daw, "cbox.obj") == RES_OK);

  file = fopen("cbox2.mtl", "w");
  CHK(file != NULL);
  fwrite(cbox2_mtl, sizeof(char), strlen(cbox2_mtl), file);
  fclose(file);

  CHK(s3daw_load(s3daw, "cbox.obj") == RES_OK);

  CHK(s3daw_get_shapes_count(NULL, NULL) == RES_BAD_ARG);
  CHK(s3daw_get_shapes_count(s3daw, NULL) == RES_BAD_ARG);
  CHK(s3daw_get_shapes_count(NULL, &nshapes) == RES_BAD_ARG);
  CHK(s3daw_get_shapes_count(s3daw, &nshapes) == RES_OK);
  CHK(nshapes == 5);

  CHK(s3daw_get_shape(NULL, 0, NULL) == RES_BAD_ARG);
  CHK(s3daw_get_shape(s3daw, 0, NULL) == RES_BAD_ARG);
  CHK(s3daw_get_shape(NULL, 0, &shape) == RES_BAD_ARG);
  FOR_EACH(ishape, 0, nshapes)
    CHK(s3daw_get_shape(s3daw, 0, &shape) == RES_OK);
  CHK(s3daw_get_shape(s3daw, nshapes, &shape) == RES_BAD_ARG);

  CHK(s3daw_clear(NULL) == RES_BAD_ARG);
  CHK(s3daw_clear(s3daw) == RES_OK);
  CHK(s3daw_get_shapes_count(s3daw, &nshapes) == RES_OK);
  CHK(nshapes == 0);
  CHK(s3daw_get_shape(s3daw, 0, &shape) == RES_BAD_ARG);

  file = fopen("cbox.obj", "r");
  CHK(file != NULL);

  CHK(s3daw_load_stream(s3daw, file) == RES_OK);
  CHK(s3daw_get_shapes_count(s3daw, &nshapes) == RES_OK);
  CHK(nshapes == 5);
  FOR_EACH(ishape, 0, nshapes)
    CHK(s3daw_get_shape(s3daw, ishape, &shape) == RES_OK);

  CHK(s3daw_get_s3d_device(s3daw, &s3d) == RES_OK);
  CHK(s3d_scene_create(s3d, &scene) == RES_OK);

  CHK(s3daw_attach_to_scene(NULL, NULL) == RES_BAD_ARG);
  CHK(s3daw_attach_to_scene(s3daw, NULL) == RES_BAD_ARG);
  CHK(s3daw_attach_to_scene(NULL, scene) == RES_BAD_ARG);
  CHK(s3daw_attach_to_scene(s3daw, scene) == RES_OK);

  CHK(s3daw_detach_from_scene(NULL, NULL) == RES_BAD_ARG);
  CHK(s3daw_detach_from_scene(s3daw, NULL) == RES_BAD_ARG);
  CHK(s3daw_detach_from_scene(NULL, scene) == RES_BAD_ARG);
  CHK(s3daw_detach_from_scene(s3daw, scene) == RES_OK);

  CHK(s3daw_detach_from_scene(s3daw, scene) == RES_BAD_ARG);
  CHK(s3d_scene_ref_put(scene) == RES_OK);

  fclose(file);
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator_proxy;
  struct aw_mtl* aw_mtl;
  struct aw_obj* aw_obj;
  struct s3daw* s3daw;
  struct s3d_device* s3d;
  struct s3d_device* s3d_tmp;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator_proxy, &mem_default_allocator);

  CHK(s3d_device_create(NULL, &allocator_proxy, 0, &s3d) == RES_OK);

  CHK(s3daw_create(NULL, NULL, NULL, NULL, NULL, 1, NULL) == RES_BAD_ARG);
  CHK(s3daw_create(NULL, NULL, NULL, NULL, NULL, 1, &s3daw) == RES_BAD_ARG);
  CHK(s3daw_create(NULL, NULL, NULL, NULL, s3d, 1,  NULL) == RES_BAD_ARG);
  CHK(s3daw_create(NULL, NULL, NULL, NULL,s3d, 1, &s3daw) == RES_OK);

  CHK(s3daw_ref_get(NULL) == RES_BAD_ARG);
  CHK(s3daw_ref_get(s3daw) == RES_OK);
  CHK(s3daw_ref_put(NULL) == RES_BAD_ARG);
  CHK(s3daw_ref_put(s3daw) == RES_OK);
  CHK(s3daw_ref_put(s3daw) == RES_OK);

  CHK(s3daw_create(NULL, &allocator_proxy, NULL, NULL, NULL, 1, NULL) == RES_BAD_ARG);
  CHK(s3daw_create(NULL, &allocator_proxy, NULL, NULL, NULL, 1, &s3daw) == RES_BAD_ARG);
  CHK(s3daw_create(NULL, &allocator_proxy, NULL, NULL, s3d, 1, NULL) == RES_BAD_ARG);
  CHK(s3daw_create(NULL, &allocator_proxy, NULL, NULL, s3d, 1, &s3daw) == RES_OK);
  CHK(s3daw_ref_put(s3daw) == RES_OK);

  CHK(s3daw_create
    (LOGGER_DEFAULT, &allocator_proxy, NULL, NULL, s3d, 1, &s3daw) == RES_OK);

  CHK(s3daw_get_loaders(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(s3daw_get_loaders(s3daw, NULL, NULL) == RES_OK); /* Useless */
  CHK(s3daw_get_loaders(NULL, &aw_obj, NULL) == RES_BAD_ARG);
  CHK(s3daw_get_loaders(s3daw, &aw_obj, NULL) == RES_OK);
  CHK(s3daw_get_loaders(NULL, NULL, &aw_mtl) == RES_BAD_ARG);
  CHK(s3daw_get_loaders(s3daw, NULL, &aw_mtl) == RES_OK);
  CHK(s3daw_get_loaders(NULL, &aw_obj, &aw_mtl) == RES_BAD_ARG);
  CHK(s3daw_get_loaders(s3daw, &aw_obj, &aw_mtl) == RES_OK);

  CHK(s3daw_get_s3d_device(NULL, NULL) == RES_BAD_ARG);
  CHK(s3daw_get_s3d_device(s3daw, NULL) == RES_BAD_ARG);
  CHK(s3daw_get_s3d_device(NULL, &s3d_tmp) == RES_BAD_ARG);
  CHK(s3daw_get_s3d_device(s3daw, &s3d_tmp) == RES_OK);
  CHK(s3d_tmp == s3d);

  test_cbox(s3daw);

  CHK(s3daw_ref_put(s3daw) == RES_OK);
  CHK(s3d_device_ref_put(s3d) == RES_OK);

  if(MEM_ALLOCATED_SIZE(&allocator_proxy)) {
    char dump[512];
    MEM_DUMP(&allocator_proxy, dump, sizeof(dump)/sizeof(char));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks\n");
  }
  mem_shutdown_proxy_allocator(&allocator_proxy);
  CHK(mem_allocated_size() == 0);
  return 0;
}

