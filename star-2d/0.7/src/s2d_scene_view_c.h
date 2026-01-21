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

#ifndef S2D_SCENE_VIEW_C_H
#define S2D_SCENE_VIEW_C_H

#include "s2d_backend.h"
#include "s2d_scene_c.h"

#include <rsys/dynamic_array_uint.h>
#include <rsys/hash_table.h>
#include <rsys/list.h>
#include <rsys/ref_count.h>

/* Forward declarations */
struct s2d_scene_view;

/* Generate the htable_geom hash table */
#define HTABLE_NAME geom
#define HTABLE_DATA struct geometry*
#define HTABLE_KEY unsigned /* Id of the shape */
#include <rsys/hash_table.h>

/* Generate the darray_fltui dynamic array */
struct fltui { float flt; unsigned ui; };
#define DARRAY_NAME fltui
#define DARRAY_DATA struct fltui
#include <rsys/dynamic_array.h>

/* Generate the darray_geom_nprims array */
struct nprims_cdf { unsigned nprims, ishape; };
#define DARRAY_NAME nprims_cdf
#define DARRAY_DATA struct nprims_cdf
#include <rsys/dynamic_array.h>

struct s2d_scene_view {
  struct list_node node; /* Attachment point to the scene scene_views pool */

  struct htable_geom cached_geoms; /* Cached shape geometries */
  struct darray_fltui cdf; /* Unormalized CDF */
  struct darray_nprims_cdf nprims_cdf;

   /* Id of Shapes detached while the scnview is active */
  struct darray_uint detached_shapes;

  float lower[2], upper[2]; /* AABB of the scene */

  /* Callback attached to the sig_shape_detach signal of scn */
  scene_shape_cb_T on_shape_detach_cb;

  int mask; /* Combination of enum s3d_scene_view_flag */
  int rtc_scn_update; /* Define if Embree geometries were deleted/added */
  int rtc_commit; /* Define whether or not the Embree scene was committed */
  RTCScene rtc_scn; /* Embree scene */

  ref_T ref;
  struct s2d_scene* scn;
};

extern LOCAL_SYM void
rtc_hit_filter_wrapper
  (const struct RTCFilterFunctionNArguments* args);

extern LOCAL_SYM void
scene_view_destroy
  (struct s2d_scene_view* scnview);

static FINLINE struct geometry*
scene_view_geometry_from_embree_id
  (struct s2d_scene_view* scnview,
   const unsigned irtc)
{
  struct geometry* geom;
  RTCGeometry rtc_geom;
  ASSERT(scnview && irtc != RTC_INVALID_GEOMETRY_ID);
  rtc_geom = rtcGetGeometry(scnview->rtc_scn, irtc);
  ASSERT(rtc_geom);
  geom = rtcGetGeometryUserData(rtc_geom);
  ASSERT(geom);
  return geom;
}

#endif /* S2D_SCENE_VIEW_C_H */
