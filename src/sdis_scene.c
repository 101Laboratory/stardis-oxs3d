/* Copyright (C) 2016-2018 |Meso|Star> (contact@meso-star.com)
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

#include "sdis_scene_Xd.h"

/* Generate the 2D functions of the scene */
#define SDIS_SCENE_DIMENSION 2
#include "sdis_scene_Xd.h"

/* Generate the 3D functions of the scene */
#define SDIS_SCENE_DIMENSION 3
#include "sdis_scene_Xd.h"

#include "sdis.h"
#include "sdis_scene_c.h"

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static void
scene_release(ref_T * ref)
{
  struct sdis_device* dev = NULL;
  struct sdis_scene* scn = NULL;
  ASSERT(ref);
  scn = CONTAINER_OF(ref, struct sdis_scene, ref);
  dev = scn->dev;
  clear_properties(scn);
  darray_interf_release(&scn->interfaces);
  darray_medium_release(&scn->media);
  darray_prim_prop_release(&scn->prim_props);
  htable_enclosure_release(&scn->enclosures);
  if(scn->s2d_view) S2D(scene_view_ref_put(scn->s2d_view));
  if(scn->s3d_view) S3D(scene_view_ref_put(scn->s3d_view));
  MEM_RM(dev->allocator, scn);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_scene_create
  (struct sdis_device* dev,
   const size_t ntris, /* #triangles */
   void (*indices)(const size_t itri, size_t ids[3], void*),
   void (*interf)(const size_t itri, struct sdis_interface** bound, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[3], void* ctx),
   void* ctx,
   struct sdis_scene** out_scn)
{
  return scene_create_3d
    (dev, ntris, indices, interf, nverts, position, ctx, out_scn);
}

res_T
sdis_scene_2d_create
  (struct sdis_device* dev,
   const size_t nsegs, /* #segments */
   void (*indices)(const size_t itri, size_t ids[2], void*),
   void (*interf)(const size_t itri, struct sdis_interface** bound, void*),
   const size_t nverts, /* #vertices */
   void (*position)(const size_t ivert, double pos[2], void* ctx),
   void* ctx,
   struct sdis_scene** out_scn)
{
  return scene_create_2d
    (dev, nsegs, indices, interf, nverts, position, ctx, out_scn);
}

res_T
sdis_scene_ref_get(struct sdis_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_get(&scn->ref);
  return RES_OK;
}

res_T
sdis_scene_ref_put(struct sdis_scene* scn)
{
  if(!scn) return RES_BAD_ARG;
  ref_put(&scn->ref, scene_release);
  return RES_OK;
}

res_T
sdis_scene_get_aabb
  (const struct sdis_scene* scn, double lower[], double upper[])
{
  float low[3], upp[3];
  res_T res = RES_OK;
  if(!scn || !lower || !upper) return RES_BAD_ARG;

  if(scene_is_2d(scn)) {
    res = s2d_scene_view_get_aabb(scn->s2d_view, low, upp);
    if(res != RES_OK) return res;
    d2_set_f2(lower, low);
    d2_set_f2(upper, upp);
  } else {
    res = s3d_scene_view_get_aabb(scn->s3d_view, low, upp);
    if(res != RES_OK) return res;
    d3_set_f3(lower, low);
    d3_set_f3(upper, upp);
  }
  return RES_OK;
}

res_T
sdis_scene_get_boundary_position
  (const struct sdis_scene* scn,
   const size_t iprim,
   const double uv[],
   double pos[])
{
  if(!scn || !uv || !pos) return RES_BAD_ARG;
  if(iprim >= scene_get_primitives_count(scn)) return RES_BAD_ARG;

  if(scene_is_2d(scn)) {
    struct s2d_primitive prim;
    struct s2d_attrib attr;
    float s = (float)uv[0];

    S2D(scene_view_get_primitive(scn->s2d_view, (unsigned int)iprim, &prim));
    S2D(primitive_get_attrib(&prim, S2D_POSITION, s, &attr));
    d2_set_f2(pos, attr.value);
  } else {
    struct s3d_primitive prim;
    struct s3d_attrib attr;
    float st[2];

    f2_set_d2(st, uv);
    S3D(scene_view_get_primitive(scn->s3d_view, (unsigned int)iprim, &prim));
    S3D(primitive_get_attrib(&prim, S3D_POSITION, st, &attr));
    d3_set_f3(pos, attr.value);
  }
  return RES_OK;
}

/*******************************************************************************
 * Local miscellaneous function
 ******************************************************************************/
const struct sdis_interface*
scene_get_interface(const struct sdis_scene* scn, const unsigned iprim)
{
  ASSERT(scn && iprim < darray_prim_prop_size_get(&scn->prim_props));
  return darray_prim_prop_cdata_get(&scn->prim_props)[iprim].interf;
}

res_T
scene_get_medium
  (const struct sdis_scene* scn,
   const double pos[],
   const struct sdis_medium** out_medium)
{
  return scene_is_2d(scn)
    ? scene_get_medium_2d(scn, pos, out_medium)
    : scene_get_medium_3d(scn, pos, out_medium);
}

