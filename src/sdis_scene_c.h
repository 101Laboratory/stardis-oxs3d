/* Copyright (C) |Meso|Star> 2016-2018 (contact@meso-star.com)
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

#ifndef SDIS_SCENE_C_H
#define SDIS_SCENE_C_H

#include <rsys/dynamic_array.h>
#include <rsys/ref_count.h>

/* Forward declaration of external types */
struct s3d_hit;

/* Context used to wrap the user geometry to Star-3D. */
struct geometry_context {
  void (*indices)(const size_t itri, size_t ids[3], void*);
  void (*position)(const size_t ivert, double pos[3], void*);
  void* data;
};

static INLINE void
interface_init
  (struct mem_allocator* allocator,
   struct sdis_interface** interface)
{
  (void)allocator;
  *interface = NULL;
}

/* Declare the array of interfaces */
#define DARRAY_NAME interface
#define DARRAY_DATA struct sdis_interface*
#define DARRAY_FUNCTOR_INIT interface_init
#include <rsys/dynamic_array.h>

struct sdis_scene {
  struct darray_interface interfaces; /* List of interfaces own by the scene */
  struct darray_interface prim_interfaces; /* Per primitive interface */
  struct s3d_scene_view* s3d_view;

  ref_T ref;
  struct sdis_device* dev;
};

extern LOCAL_SYM const struct sdis_interface*
scene_get_interface
  (const struct sdis_scene* scene,
   const unsigned iprim);

extern LOCAL_SYM res_T
scene_get_medium
  (const struct sdis_scene* scene,
   const double position[3],
   const struct sdis_medium** medium);

extern LOCAL_SYM int
hit_filter_function
  (const struct s3d_hit* hit,
   const float ray_org[3],
   const float ray_dir[3],
   void* ray_data, /* struct s3d_hit* */
   void* filter_data); /* NULL */

#endif /* SDIS_SCENE_C_H */

