/* Copyright (C) 2018, 2020-2025 |Méso|Star> (contact@meso-star.com)
 * Copyright (C) 2018 Université Paul Sabatier
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

#ifndef TEST_SVX_UTILS_H
#define TEST_SVX_UTILS_H

#include <rsys/double2.h>
#include <rsys/double3.h>
#include <rsys/image.h>
#include<rsys/mem_allocator.h>

#include <stdio.h>

enum data_type {
  TYPE_CHAR,
  TYPE_INT,
  TYPE_LONG,
  TYPE_FLOAT,
  TYPE_DOUBLE
};

struct camera {
  double pos[3];
  double x[3], y[3], z[3]; /* Frame */
};

struct ray {
  double org[3];
  double dir[3];
  double range[3];
};

static INLINE void
camera_init
  (struct camera* cam,
   const double pos[3],
   const double tgt[3],
   const double up[3],
   const double proj_ratio)
{
  const double fov_x = PI * 0.25;
  double d = 0.0;
  CHK(cam != NULL);

  d3_set(cam->pos, pos);
  d = d3_normalize(cam->z, d3_sub(cam->z, tgt, pos)); CHK(d != 0);
  d = d3_normalize(cam->x, d3_cross(cam->x, cam->z, up)); CHK(d != 0);
  d = d3_normalize(cam->y, d3_cross(cam->y, cam->z, cam->x)); CHK(d != 0);
  d3_divd(cam->z, cam->z, tan(fov_x*0.5));
  d3_divd(cam->y, cam->y, proj_ratio);
}

static INLINE void
camera_ray
  (const struct camera* cam,
   const double pixel[2],
   struct ray* ray)
{
  double x[3], y[3], f;
  CHK(cam && pixel && ray);

  d3_muld(x, cam->x, pixel[0]*2.0 - 1.0);
  d3_muld(y, cam->y, pixel[1]*2.0 - 1.0);
  d3_add(ray->dir, d3_add(ray->dir, x, y), cam->z);
  f = d3_normalize(ray->dir, ray->dir); CHK(f != 0);
  d3_set(ray->org, cam->pos);
  d2(ray->range, 0, INF);
}


static INLINE const char*
data_type_to_string(const enum data_type type)
{
  switch(type) {
    case TYPE_CHAR: return "char";
    case TYPE_INT: return "int";
    case TYPE_LONG: return "long";
    case TYPE_FLOAT: return "float";
    case TYPE_DOUBLE: return "double";
    default: FATAL("Unreachable code.\n");
  }
}

static INLINE void
write_leaf_vertices
  (const struct svx_voxel* leaf,
   const size_t ileaf,
   void* context)
{
  FILE* stream = context;
  (void)ileaf;
  CHK(stream != NULL);
  CHK(leaf != NULL);
  fprintf(stream,
    "%g %g %g\n%g %g %g\n%g %g %g\n%g %g %g\n"
    "%g %g %g\n%g %g %g\n%g %g %g\n%g %g %g\n",
    leaf->lower[0], leaf->lower[1], leaf->lower[2],
    leaf->upper[0], leaf->lower[1], leaf->lower[2],
    leaf->lower[0], leaf->upper[1], leaf->lower[2],
    leaf->upper[0], leaf->upper[1], leaf->lower[2],
    leaf->lower[0], leaf->lower[1], leaf->upper[2],
    leaf->upper[0], leaf->lower[1], leaf->upper[2],
    leaf->lower[0], leaf->upper[1], leaf->upper[2],
    leaf->upper[0], leaf->upper[1], leaf->upper[2]);
}

static INLINE void
write_leaf_cells
  (const struct svx_voxel* leaf,
   const size_t ileaf,
   void* context)
{
  FILE* stream = context;
  CHK(stream != NULL);
  CHK(leaf != NULL);
  fprintf(stream, "8 %lu %lu %lu %lu %lu %lu %lu %lu\n",
    (unsigned long)(ileaf*8 + 0),
    (unsigned long)(ileaf*8 + 1),
    (unsigned long)(ileaf*8 + 2),
    (unsigned long)(ileaf*8 + 3),
    (unsigned long)(ileaf*8 + 4),
    (unsigned long)(ileaf*8 + 5),
    (unsigned long)(ileaf*8 + 6),
    (unsigned long)(ileaf*8 + 7));
}

static INLINE void
dump_data
  (FILE* stream,
   struct svx_tree* tree,
   const enum data_type type,
   const size_t numcomps,
   void (*write_leaf_data)
    (const struct svx_voxel* leaf, const size_t ileaf, void* ctx))
{
  struct svx_tree_desc desc;
  size_t ileaf;

  CHK(stream != NULL);
  CHK(tree != NULL);
  CHK(write_leaf_data);

  fprintf(stream, "# vtk DataFile Version 2.0\n");
  fprintf(stream, "Volume\n");
  fprintf(stream, "ASCII\n");
  fprintf(stream, "DATASET UNSTRUCTURED_GRID\n");

  CHK(svx_tree_get_desc(tree, &desc) == RES_OK);
  CHK(desc.type == SVX_OCTREE); /* FIXME currently only octree are supported */
  fprintf(stream, "POINTS %lu float\n", (unsigned long)(desc.nleaves* 8));
  CHK(svx_tree_for_each_leaf(tree, write_leaf_vertices, stream) == RES_OK);

  fprintf(stream, "CELLS %lu %lu\n",
    (unsigned long)desc.nleaves,
    (unsigned long)(desc.nleaves*(8/*#verts per leaf*/ + 1/*1st field of a cell*/)));
  CHK(svx_tree_for_each_leaf(tree, write_leaf_cells, stream) == RES_OK);

  fprintf(stream, "CELL_TYPES %lu\n", (unsigned long)desc.nleaves);
  FOR_EACH(ileaf, 0, desc.nleaves) fprintf(stream, "11\n");

  fprintf(stream, "CELL_DATA %lu\n", (unsigned long)desc.nleaves);
  fprintf(stream, "SCALARS K %s %lu\n",
    data_type_to_string(type), (unsigned long)numcomps);
  fprintf(stream, "LOOKUP_TABLE default\n");
  CHK(svx_tree_for_each_leaf(tree, write_leaf_data, stream) == RES_OK);
}

static INLINE void
check_img_eq(const struct image* img0, const struct image* img1)
{
  size_t ix, iy;
  size_t pixsz;

  CHK(img0 && img1);
  CHK(img0->format == IMAGE_RGB8);
  CHK(img1->format == IMAGE_RGB8);
  CHK(img0->width == img1->width);
  CHK(img0->height == img1->height);
  CHK(img0->pitch == img1->pitch);

  pixsz = sizeof_image_format(img0->format);

  FOR_EACH(iy, 0, img0->height) {
    const char* row0 = img0->pixels + iy * img0->pitch;
    const char* row1 = img1->pixels + iy * img1->pitch;
    FOR_EACH(ix, 0, img0->width) {
      const uint8_t* pix0 = (const uint8_t*)(row0 + ix*pixsz);
      const uint8_t* pix1 = (const uint8_t*)(row1 + ix*pixsz);
      CHK(pix0[0] == pix1[0]);
      CHK(pix0[1] == pix1[1]);
      CHK(pix0[2] == pix1[2]);
    }
  }
}

static INLINE void
check_memory_allocator(struct mem_allocator* allocator)
{
  if(MEM_ALLOCATED_SIZE(allocator)) {
    char dump[512];
    MEM_DUMP(allocator, dump, sizeof(dump)/sizeof(char));
    fprintf(stderr, "%s\n", dump);
    FATAL("Memory leaks\n");
  }
}

#endif /* TEST_SVX_UTILS_H */

