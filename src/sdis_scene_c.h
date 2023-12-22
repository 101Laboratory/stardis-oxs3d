/* Copyright (C) 2016-2023 |Méso|Star> (contact@meso-star.com)
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

#include <star/s2d.h>
#include <star/s3d.h>

#include <rsys/dynamic_array_uint.h>
#include <rsys/hash.h>
#include <rsys/hash_table.h>
#include <rsys/ref_count.h>

#include <limits.h>

struct prim_prop {
  struct sdis_interface* interf;
  unsigned front_enclosure; /* Id of the front facing enclosure  */
  unsigned back_enclosure; /* Id of the back facing enclosure */
};

struct hit_filter_data {
  struct s2d_hit hit_2d;
  struct s3d_hit hit_3d;
  double epsilon; /* Threshold defining roughly equal intersections */
};
#define HIT_FILTER_DATA_NULL__ {S2D_HIT_NULL__, S3D_HIT_NULL__, 0}
static const struct hit_filter_data HIT_FILTER_DATA_NULL =
  HIT_FILTER_DATA_NULL__;

struct get_medium_info {
  /* Targeted position */
  float pos_tgt[3];
  /* Ray trace to the targeted position in order to define the current medium */
  float ray_org[3];
  float ray_dir[3];
  /* Hit encouters along the ray and used to define the current medium */
  struct s2d_hit hit_2d;
  struct s3d_hit hit_3d;
};
#define GET_MEDIUM_INFO_NULL__ \
  {{0,0,0}, {0,0,0}, {0,0,0}, S2D_HIT_NULL__, S3D_HIT_NULL__}
static const struct get_medium_info GET_MEDIUM_INFO_NULL =
  GET_MEDIUM_INFO_NULL__;

static INLINE void
prim_prop_init(struct mem_allocator* allocator, struct prim_prop* prim)
{
  (void)allocator;
  prim->interf = NULL;
  prim->front_enclosure = UINT_MAX;
  prim->back_enclosure = UINT_MAX;
}

static INLINE void
interface_init(struct mem_allocator* allocator, struct sdis_interface** interf)
{
  (void)allocator;
  *interf = NULL;
}

static INLINE void
medium_init(struct mem_allocator* allocator, struct sdis_medium** medium)
{
  (void)allocator;
  *medium = NULL;
}

#define ENCLOSURE_MULTI_MEDIA UINT_MAX

struct enclosure {
  struct s2d_scene_view* s2d_view;
  struct s3d_scene_view* s3d_view;
  /* Map the id of the enclosure primitives to their primitive id into the
   * whole scene */
  struct darray_uint local2global;

  double hc_upper_bound;
  double S_over_V; /* in 3D = surface/volume; in 2D = perimeter/area */
  double V; /* 3D = volume; 2D = area; */

  unsigned medium_id;
};

static INLINE void
enclosure_init(struct mem_allocator* allocator, struct enclosure* enc)
{
  ASSERT(allocator && enc);
  enc->s2d_view = NULL;
  enc->s3d_view = NULL;
  darray_uint_init(allocator, &enc->local2global);
  enc->S_over_V = 0;
  enc->V = 0;
  enc->hc_upper_bound = 0;
  enc->medium_id = ENCLOSURE_MULTI_MEDIA;
}

static INLINE void
enclosure_release(struct enclosure* enc)
{
  if(enc->s2d_view) S2D(scene_view_ref_put(enc->s2d_view));
  if(enc->s3d_view) S3D(scene_view_ref_put(enc->s3d_view));
  darray_uint_release(&enc->local2global);
}

static INLINE res_T
enclosure_copy(struct enclosure* dst, const struct enclosure* src)
{
  if(src->s3d_view) {
    S3D(scene_view_ref_get(src->s3d_view));
    dst->s3d_view = src->s3d_view;
  }
  if(src->s2d_view) {
    S2D(scene_view_ref_get(src->s2d_view));
    dst->s2d_view = src->s2d_view;
  }
  dst->S_over_V = src->S_over_V;
  dst->V = src->V;
  dst->hc_upper_bound = src->hc_upper_bound;
  dst->medium_id = src->medium_id;
  return darray_uint_copy(&dst->local2global, &src->local2global);
}

static INLINE res_T
enclosure_copy_and_release(struct enclosure* dst, struct enclosure* src)
{
  res_T res = RES_OK;
  res = darray_uint_copy_and_release(&dst->local2global, &src->local2global);
  if(res != RES_OK) return res;
  if(src->s3d_view) {
    /* Only transfer ownership */
    dst->s3d_view = src->s3d_view;
    src->s3d_view = NULL;
  }
  if(src->s2d_view) {
    /* Only transfer ownership */
    dst->s2d_view = src->s2d_view;
    src->s2d_view = NULL;
  }
  dst->S_over_V = src->S_over_V;
  dst->V = src->V;
  dst->hc_upper_bound = src->hc_upper_bound;
  dst->medium_id = src->medium_id;
  return RES_OK;
}

static INLINE unsigned
enclosure_local2global_prim_id
  (const struct enclosure* enc,
   const size_t local_prim_id)
{
  ASSERT(enc && local_prim_id < darray_uint_size_get(&enc->local2global));
  return darray_uint_cdata_get(&enc->local2global)[local_prim_id];
}

/* Declare the array of interfaces */
#define DARRAY_NAME interf
#define DARRAY_DATA struct sdis_interface*
#define DARRAY_FUNCTOR_INIT interface_init
#include <rsys/dynamic_array.h>

/* Declare the array of media */
#define DARRAY_NAME medium
#define DARRAY_DATA struct sdis_medium*
#define DARRAY_FUNCTOR_INIT medium_init
#include <rsys/dynamic_array.h>

/* Declare the array of primitives */
#define DARRAY_NAME prim_prop
#define DARRAY_DATA struct prim_prop
#define DARRAY_FUNCTOR_INIT prim_prop_init
#include <rsys/dynamic_array.h>

/* Declare the hash table that maps an enclosure id to its data */
#define HTABLE_NAME enclosure
#define HTABLE_KEY unsigned
#define HTABLE_DATA struct enclosure
#define HTABLE_DATA_FUNCTOR_INIT enclosure_init
#define HTABLE_DATA_FUNCTOR_RELEASE enclosure_release
#define HTABLE_DATA_FUNCTOR_COPY enclosure_copy
#define HTABLE_DATA_FUNCTOR_COPY_AND_RELEASE enclosure_copy_and_release
#include <rsys/hash_table.h>

/* Declare the hash table that maps an enclosure id to hc upper bound */
#define HTABLE_NAME d
#define HTABLE_KEY unsigned
#define HTABLE_DATA double
#include <rsys/hash_table.h>

struct sdis_scene {
  struct darray_interf interfaces; /* List of interfaces own by the scene */
  struct darray_medium media; /* List of media own by the scene */
  struct darray_prim_prop prim_props; /* Per primitive properties */
  struct s2d_scene_view* s2d_view;
  struct s3d_scene_view* s3d_view;
  struct senc2d_scene* senc2d_scn;
  struct senc3d_scene* senc3d_scn;

  struct htable_d tmp_hc_ub; /* Map an enclosure id to its hc upper bound */
  struct htable_enclosure enclosures; /* Map an enclosure id to its data */
  unsigned outer_enclosure_id;

  double fp_to_meter;
  struct sdis_ambient_radiative_temperature trad;
  double tmin; /* Minimum temperature of the system (In Kelvin) */
  double tmax; /* Maximum temperature of the system (In Kelvin) */

  struct sdis_source* source; /* External source. May be NULL */

  ref_T ref;
  struct sdis_device* dev;
};

static FINLINE size_t
scene_get_primitives_count(const struct sdis_scene* scn)
{
  ASSERT(scn);
  return darray_prim_prop_size_get(&scn->prim_props);
}

extern LOCAL_SYM struct sdis_interface*
scene_get_interface
  (const struct sdis_scene* scene,
   const unsigned iprim);

extern LOCAL_SYM res_T
scene_get_medium
  (struct sdis_scene* scene,
   const double position[],
   struct get_medium_info* info, /* May be NULL */
   struct sdis_medium** medium);

/* This function assumes that the tested position lies into finite enclosure.
 * The medium into which it lies is thus retrieved by tracing a random ray
 * around the current position. For possible infinite enclosure, one has to use
 * the `scene_get_medium' function instead that, in counterpart, can be more
 * time consuming.
 *
 * Note that actually, the function internally calls scene_get_medium if no
 * valid medium is found with the regular procedure. This may be due to
 * numerical issues or wrong assumptions on the current medium (its boundaries
 * are opened to infinity). */
extern LOCAL_SYM res_T
scene_get_medium_in_closed_boundaries
  (struct sdis_scene* scn,
   const double position[],
   struct sdis_medium** medium);

extern LOCAL_SYM res_T
scene_compute_hash
  (const struct sdis_scene* scn,
   hash256_T hash);

/* Check that the primitive identifier is valid wrt the scene. If not, the
 * function prints an error message and returns RES_BAD_ARG. */
extern LOCAL_SYM res_T
scene_check_primitive_index
  (const struct sdis_scene* scn,
   const size_t iprim);

/* Check that the scene is 2D. If not, the function prints an error message and
 * returns RES_BAD_ARG */
extern LOCAL_SYM res_T
scene_check_dimensionality_2d
  (const struct sdis_scene* scn);

/* Check that the scene is 3D. If not, the function prints an error message and
 * returns RES_BAD_ARG */
extern LOCAL_SYM res_T
scene_check_dimensionality_3d
  (const struct sdis_scene* scn);

static INLINE void
scene_get_enclosure_ids
  (const struct sdis_scene* scn,
   const unsigned iprim,
   unsigned encs[2]) /* Front and Back enclosure identifiers */
{
  ASSERT(scn && iprim < darray_prim_prop_size_get(&scn->prim_props));
  ASSERT(encs);
  encs[0] = darray_prim_prop_cdata_get(&scn->prim_props)[iprim].front_enclosure;
  encs[1] = darray_prim_prop_cdata_get(&scn->prim_props)[iprim].back_enclosure;
}

static INLINE int
scene_is_outside
  (const struct sdis_scene* scn,
   const enum sdis_side side,
   const unsigned iprim)
{
  unsigned encs[2];
  ASSERT(scn && scn->outer_enclosure_id != UINT_MAX);
  scene_get_enclosure_ids(scn, iprim, encs);
  return (encs[side] == scn->outer_enclosure_id);
}

static INLINE const struct enclosure*
scene_get_enclosure(struct sdis_scene* scn, const unsigned ienc)
{
  const struct enclosure* enc = NULL;
  ASSERT(scn);
  enc = htable_enclosure_find(&scn->enclosures, &ienc);
  return enc;
}

static INLINE int
scene_is_2d(const struct sdis_scene* scn)
{
  ASSERT(scn && (scn->s2d_view || scn->s3d_view));
  return scn->s2d_view != NULL;
}

#endif /* SDIS_SCENE_C_H */

