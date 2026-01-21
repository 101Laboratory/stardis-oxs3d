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
#include <rsys/logger.h>
#include <rsys/mem_allocator.h>

#include <string.h>

static void
test_common(struct aw_mtl* mtl)
{
  static const char* mtl_common =
    "#!\n"
    "newmtl my_mtl\n"
    "\n"
    "Ka 0.0435 0.0436 0.0437\n"
    "Kd 0.1086 0.1087 0.1088\n"
    "Ks 0 0 0\n"
    "Tf xyz 0.987 0.988 0.989\n"
    "illum 6\n"
    "d -halo 0.66\n"
    "Ns 10.0\n"
    "sharpness 60\n"
    "Ni 1.19713\n"
    "\n"
    "map_Ka -s 1 1 1 -o 0 0 0 -mm 0 1 chrome.mpc\n"
    "map_Kd -s 1 1 1 -o 0 0 0 -mm 0 1 chrome.mpc\n"
    "map_Ks -s 1 1 1 -o 0 0 0 -mm 0 1 chrome.mpc\n"
    "map_Ns -s 1 1 1 -o 0 0 0 -mm 0 1 wisp.mps\n"
    "map_d -s 1 1 1 -o 0 0 0 -mm 0 1 wisp.mps\n"
    "disp -s 1 1 .5 wisp.mps\n"
    "decal -s 1 1 1 -o 0 0 0 -mm 0 1 sand.mps\n"
    "refl -type sphere -mm 0 1 clouds.mpc\n"
    "\n"
    "bump -s 1 1 1 -o 0 0 0 -bm 1 sand.mpb";
  FILE* file;
  size_t nmtls;
  double tmp[3];
  struct aw_material mat;

  CHK(mtl != NULL);

  file = fopen("test_mtl_common.mtl", "w");
  CHK(file != NULL);
  fwrite(mtl_common, sizeof(char), strlen(mtl_common), file);
  CHK(fclose(file) == 0);

  CHK(aw_mtl_load(NULL, NULL) == RES_BAD_ARG);
  CHK(aw_mtl_load(mtl, NULL) == RES_BAD_ARG);
  CHK(aw_mtl_load(NULL, "test_mtl_common.mtl") == RES_BAD_ARG);
  CHK(aw_mtl_load(mtl, "none.mtl") == RES_IO_ERR);
  CHK(aw_mtl_load(mtl, "test_mtl_common.mtl") == RES_OK);

  CHK(aw_mtl_get_materials_count(NULL, NULL) == RES_BAD_ARG);
  CHK(aw_mtl_get_materials_count(mtl, NULL) == RES_BAD_ARG);
  CHK(aw_mtl_get_materials_count(NULL, &nmtls) == RES_BAD_ARG);
  CHK(aw_mtl_get_materials_count(mtl, &nmtls) == RES_OK);

  CHK(nmtls == 1);

  CHK(aw_mtl_clear(NULL) == RES_BAD_ARG);
  CHK(aw_mtl_clear(mtl) == RES_OK);
  CHK(aw_mtl_get_materials_count(mtl, &nmtls) == RES_OK);
  CHK(nmtls == 0);

  CHK(aw_mtl_load(mtl, "test_mtl_common.mtl") == RES_OK);

  CHK(aw_mtl_get_material(NULL, 0, &mat) == RES_BAD_ARG);
  CHK(aw_mtl_get_material(mtl, SIZE_MAX, &mat) == RES_BAD_ARG);
  CHK(aw_mtl_get_material(mtl, 0, NULL) == RES_BAD_ARG);
  CHK(aw_mtl_get_material(mtl, 0, &mat) == RES_OK);

  CHK(!strcmp(mat.name, "my_mtl"));
  CHK(mat.ambient.color_space == AW_COLOR_RGB);
  d3(tmp, 0.0435, 0.0436, 0.0437);
  CHK(d3_eq(mat.ambient.value, tmp));

  CHK(mat.diffuse.color_space == AW_COLOR_RGB);
  d3(tmp, 0.1086, 0.1087, 0.1088);
  CHK(d3_eq(mat.diffuse.value, tmp));

  CHK(mat.specular.color_space == AW_COLOR_RGB);
  d3_splat(tmp, 0);
  CHK(d3_eq(mat.specular.value, tmp));

  CHK(mat.transmission.color_space == AW_COLOR_XYZ);
  d3(tmp, 0.987, 0.988, 0.989);
  CHK(d3_eq(mat.transmission.value, tmp));

  CHK(mat.specular_exponent == 10.0);
  CHK(mat.refraction_index == 1.19713);
  CHK(mat.illumination_model == 6);

  CHK(!strcmp(mat.ambient_map.filename, "chrome.mpc"));
  CHK(mat.ambient_map.options_mask == 0);
  CHK(mat.ambient_map.image_bias == 0.f);
  CHK(mat.ambient_map.image_scale == 1.f);
  CHK(d3_eq(mat.ambient_map.texcoord_bias, d3_splat(tmp, 0.0)));
  CHK(d3_eq(mat.ambient_map.texcoord_scale, d3_splat(tmp, 1.0)));
  CHK(d3_eq(mat.ambient_map.texcoord_turbulence, d3_splat(tmp, 0.0)));

  CHK(!strcmp(mat.diffuse_map.filename, "chrome.mpc"));
  CHK(mat.diffuse_map.options_mask == 0);
  CHK(mat.diffuse_map.image_bias == 0.f);
  CHK(mat.diffuse_map.image_scale == 1.f);
  CHK(d3_eq(mat.diffuse_map.texcoord_bias, d3_splat(tmp, 0.0)));
  CHK(d3_eq(mat.diffuse_map.texcoord_scale, d3_splat(tmp, 1.0)));
  CHK(d3_eq(mat.diffuse_map.texcoord_turbulence, d3_splat(tmp, 0.0)));

  CHK(!strcmp(mat.specular_map.filename, "chrome.mpc"));
  CHK(mat.specular_map.options_mask == 0);
  CHK(mat.specular_map.image_bias == 0.0);
  CHK(mat.specular_map.image_scale == 1.0);
  CHK(d3_eq(mat.specular_map.texcoord_bias, d3_splat(tmp, 0.0)));
  CHK(d3_eq(mat.specular_map.texcoord_scale, d3_splat(tmp, 1.0)));
  CHK(d3_eq(mat.specular_map.texcoord_turbulence, d3_splat(tmp, 0.0)));

  CHK(!strcmp(mat.specular_exponent_map.filename, "wisp.mps"));
  CHK(mat.specular_exponent_map.options_mask == 0);
  CHK(mat.specular_exponent_map.image_bias == 0.0);
  CHK(mat.specular_exponent_map.image_scale == 1.0);
  CHK(d3_eq(mat.specular_exponent_map.texcoord_scale, d3_splat(tmp, 1.0)));
  CHK(d3_eq(mat.specular_exponent_map.texcoord_bias, d3_splat(tmp, 0.0)));
  CHK(d3_eq(mat.specular_exponent_map.texcoord_turbulence, tmp) == 1);
  CHK(mat.specular_exponent_map.scalar == AW_MAP_CHANNEL_LUMINANCE);

  CHK(!strcmp(mat.bump_map.filename, "sand.mpb"));
  CHK(mat.bump_map.options_mask == 0);
  CHK(mat.bump_map.image_bias == 0.0);
  CHK(mat.bump_map.image_scale == 1.0);
  CHK(d3_eq(mat.bump_map.texcoord_scale, d3_splat(tmp, 1.0)));
  CHK(d3_eq(mat.bump_map.texcoord_bias, d3_splat(tmp, 0.0)));
  CHK(d3_eq(mat.bump_map.texcoord_turbulence, tmp) == 1);
  CHK(mat.bump_map.scalar == AW_MAP_CHANNEL_LUMINANCE);
  CHK(mat.bump_map.bump_multiplier == 1.0);

  CHK(aw_mtl_purge(NULL) == RES_BAD_ARG);
  CHK(aw_mtl_purge(mtl) == RES_OK);
  CHK(aw_mtl_get_materials_count(mtl, &nmtls) == RES_OK);
  CHK(nmtls == 0);
}

static void
test_multiple_materials(struct aw_mtl* mtl)
{
  const char* mtl_multi =
    "newmtl material_0\n"
    "\tNs 8.0\n"
    "\tNi 1.5\n"
    "\td 1.0\n"
    "\tTf 1.0 1 1\n"
    "\tillum 2\n"
    "\tKa 0 0 0\n"
    "\tKd 0.734118\t0.730588 0.674118\n"
    "\tKs 0 0 0\n"
    "\tmap_Ka my_long_and_verbose_filename_of_a_RED_GREEN_BLUE_1024x64_image.png\n"
    "\tmap_Kd tp.png\n"
    "\n"
    "newmtl textured_material\n"
    "Ns 6\n"
    "Ni 1.70\n"
    "d 1\n"
    "Tf 1 1.2 1.3 \n"
    "illum 0\n"
    "Ka\t\t 0 0 0\n"
    "Kd \t 0.734118 0.709412 0.674118\n"
    "Ks 0 0 0\n"
    "map_Ka\ttex6x6.png\n"
    "map_Kd tex6x6.png\n"
    "bump -imfchan r -bm 0.2 tex6x6-bump.png\n"
    "\n"
    "newmtl hello_world\n"
    "Ns 8\n"
    "Ni 1.5\n"
    "d 1\n"
    "Tr 0\n"
    "Tf 1 1 1\n"
    "illum 2\n"
    "Ka\t0 \t0\t0000\n"
    "Kd 0.546274 0.219608   0.183922\n"
    "Ks 2\n";
  FILE* file;
  size_t nmtls;
  double tmp[3];
  struct aw_material mat;

  CHK(mtl != NULL);

  file = fopen("test_mtl_multi.mtl", "w+");
  CHK(file != NULL);
  fwrite(mtl_multi, sizeof(char), strlen(mtl_multi), file);
  CHK(fseek(file, 0, SEEK_SET) == 0);

  CHK(aw_mtl_load_stream(NULL, file, NULL) == RES_BAD_ARG);
  CHK(aw_mtl_load_stream(mtl, NULL, NULL) == RES_BAD_ARG);
  CHK(aw_mtl_load_stream(mtl, file, NULL) == RES_OK);
  CHK(aw_mtl_get_materials_count(mtl, &nmtls) == RES_OK);
  CHK(nmtls == 3);

  CHK(aw_mtl_get_material(mtl, 0, &mat) == RES_OK);
  CHK(!strcmp(mat.name, "material_0"));
  CHK(mat.specular_exponent == 8.f);
  CHK(mat.refraction_index == 1.5f);
  CHK(mat.transmission.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.transmission.value, d3_splat(tmp, 1.0)));
  CHK(mat.illumination_model == 2);
  CHK(mat.ambient.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.ambient.value, d3_splat(tmp, 0.0)));
  CHK(mat.diffuse.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.diffuse.value, d3(tmp, 0.734118, 0.730588, 0.674118)));
  CHK(mat.specular.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.specular.value, d3_splat(tmp, 0.0)));
  CHK(!strcmp
    (mat.ambient_map.filename,
     "my_long_and_verbose_filename_of_a_RED_GREEN_BLUE_1024x64_image.png"));
  CHK(mat.ambient_map.options_mask == 0);
  CHK(mat.ambient_map.image_bias == 0.0);
  CHK(mat.ambient_map.image_scale == 1.0);
  CHK(d3_eq(mat.ambient_map.texcoord_bias, d3_splat(tmp, 0.0)));
  CHK(d3_eq(mat.ambient_map.texcoord_scale, d3_splat(tmp, 1.0)));
  CHK(d3_eq(mat.ambient_map.texcoord_turbulence, d3_splat(tmp, 0.0)));
  CHK(mat.ambient_map.resolution == 0);
  CHK(!strcmp(mat.diffuse_map.filename, "tp.png"));
  CHK(!mat.specular_map.filename);
  CHK(!mat.specular_exponent_map.filename);
  CHK(!mat.bump_map.filename);

  CHK(aw_mtl_get_material(mtl, 1, &mat) == RES_OK);
  CHK(!strcmp(mat.name, "textured_material"));
  CHK(mat.specular_exponent == 6.0);
  CHK(mat.refraction_index == 1.7);
  CHK(mat.transmission.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.transmission.value, d3(tmp, 1.0, 1.2, 1.3)));
  CHK(mat.illumination_model == 0);
  CHK(mat.ambient.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.ambient.value, d3_splat(tmp, 0.0)));
  CHK(mat.diffuse.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.diffuse.value, d3(tmp, 0.734118, 0.709412, 0.674118)));
  CHK(mat.specular.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.specular.value, d3_splat(tmp, 0.0)));
  CHK(!strcmp(mat.ambient_map.filename, "tex6x6.png"));
  CHK(!strcmp(mat.diffuse_map.filename, "tex6x6.png"));
  CHK(!strcmp(mat.bump_map.filename, "tex6x6-bump.png"));
  CHK(mat.bump_map.scalar == AW_MAP_CHANNEL_RED);
  CHK(mat.bump_map.bump_multiplier == 0.2);
  CHK(!mat.specular_exponent_map.filename);

  CHK(aw_mtl_get_material(mtl, 2, &mat) == RES_OK);
  CHK(!strcmp(mat.name, "hello_world"));
  CHK(mat.specular_exponent == 8.0);
  CHK(mat.refraction_index == 1.5);
  CHK(mat.transmission.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.transmission.value, d3_splat(tmp, 1.0)));
  CHK(mat.illumination_model == 2);
  CHK(mat.ambient.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.ambient.value, d3_splat(tmp, 0.0)));
  CHK(mat.diffuse.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.diffuse.value, d3(tmp, 0.546274, 0.219608, 0.183922)));
  CHK(mat.specular.color_space == AW_COLOR_RGB);
  CHK(d3_eq(mat.specular.value, d3_splat(tmp, 2.0)));
  CHK(!mat.ambient_map.filename);
  CHK(!mat.diffuse_map.filename);
  CHK(!mat.bump_map.filename);
  CHK(!mat.specular_exponent_map.filename);

  CHK(fclose(file) == 0);
}

static void
test_unloadable(struct aw_mtl* mtl)
{
  const char* mtl0 =
    "newmtl material_0\n"
    "\tNs 8.0\n"
    "\tNii 1.5\n"
    "\td 1.0\n"
    "\tTf 1.0 1 1\n"
    "\tillum 2\n"
    "\tKa 0 0 0\n"
    "\tmap_Kambient hop.png\n"
    "\tKd\n"
    "\tKs 0 0 0\n";
  FILE* file;
  size_t nmtls;

  CHK(mtl != NULL);

  file = fopen("mtl0.mtl", "w");
  CHK(file != NULL);
  fwrite(mtl0, sizeof(char), strlen(mtl0), file);
  CHK(fclose(file) == 0);

  CHK(aw_mtl_load(mtl, "mtl0.mtl") == RES_BAD_ARG);
  CHK(aw_mtl_get_materials_count(mtl, &nmtls) == RES_OK);
  CHK(nmtls == 0);
}
int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct aw_mtl* mtl;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  CHK(aw_mtl_create(LOGGER_DEFAULT, NULL, 1, NULL) == RES_BAD_ARG);
  CHK(aw_mtl_create(LOGGER_DEFAULT, &allocator, 1, NULL) == RES_BAD_ARG);
  CHK(aw_mtl_create(LOGGER_DEFAULT, NULL, 1, &mtl) == RES_OK);

  CHK(aw_mtl_ref_get(NULL) == RES_BAD_ARG);
  CHK(aw_mtl_ref_get(mtl) == RES_OK);
  CHK(aw_mtl_ref_put(NULL) == RES_BAD_ARG);
  CHK(aw_mtl_ref_put(mtl) == RES_OK);
  CHK(aw_mtl_ref_put(mtl) == RES_OK);

  CHK(aw_mtl_create(NULL, &allocator, 1, &mtl) == RES_OK);

  test_common(mtl);
  test_multiple_materials(mtl);
  test_unloadable(mtl);

  CHK(aw_mtl_ref_put(mtl) == RES_OK);

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

