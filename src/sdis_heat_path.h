/* Copyright (C) 2016-2020 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_HEAT_PATH_H
#define SDIS_HEAT_PATH_H

#include "sdis.h"

#include <rsys/dynamic_array.h>
#include <rsys/rsys.h>

/* Forward declarations */
struct rwalk_2d;
struct rwalk_3d;
struct rwalk_context;
struct sdis_scene;
struct ssp_rng;
struct temperature_2d;
struct temperature_3d;

/* Generate the dynamic array of heat vertices */
#define DARRAY_NAME heat_vertex
#define DARRAY_DATA struct sdis_heat_vertex
#include <rsys/dynamic_array.h>

/*******************************************************************************
 * Heat path data structure
 ******************************************************************************/
struct sdis_heat_path {
  struct darray_heat_vertex vertices;
  enum sdis_heat_path_flag status;
};

static INLINE void
heat_path_init(struct mem_allocator* allocator, struct sdis_heat_path* path)
{
  ASSERT(path);
  path->status = SDIS_HEAT_PATH_NONE;
  darray_heat_vertex_init(allocator, &path->vertices);
}

static INLINE void
heat_path_release(struct sdis_heat_path* path)
{
  ASSERT(path);
  darray_heat_vertex_release(&path->vertices);
}

static INLINE res_T
heat_path_copy(struct sdis_heat_path* dst, const struct sdis_heat_path* src)
{
  ASSERT(dst && src);
  dst->status = src->status;
  return darray_heat_vertex_copy(&dst->vertices, &src->vertices);
}

static INLINE res_T
heat_path_copy_and_release(struct sdis_heat_path* dst, struct sdis_heat_path* src)
{
  ASSERT(dst && src);
  dst->status = src->status;
  return darray_heat_vertex_copy_and_release(&dst->vertices, &src->vertices);
}

static INLINE res_T
heat_path_copy_and_clear(struct sdis_heat_path* dst, struct sdis_heat_path* src)
{
  ASSERT(dst && src);
  dst->status = src->status;
  return darray_heat_vertex_copy_and_clear(&dst->vertices, &src->vertices);
}

static INLINE res_T
heat_path_add_vertex(struct sdis_heat_path* path, const struct sdis_heat_vertex* vtx)
{
  ASSERT(path && vtx);
  return darray_heat_vertex_push_back(&path->vertices, vtx);
}

static INLINE struct sdis_heat_vertex*
heat_path_get_last_vertex(struct sdis_heat_path* path)
{
  size_t sz;
  ASSERT(path);
  sz = darray_heat_vertex_size_get(&path->vertices);
  ASSERT(sz);
  return darray_heat_vertex_data_get(&path->vertices) + (sz-1);
}

/* Generate the dynamic array of heat paths */
#define DARRAY_NAME heat_path
#define DARRAY_DATA struct sdis_heat_path
#define DARRAY_FUNCTOR_INIT heat_path_init
#define DARRAY_FUNCTOR_RELEASE heat_path_release
#define DARRAY_FUNCTOR_COPY heat_path_copy
#define DARRAY_FUNCTOR_COPY_AND_RELEASE heat_path_copy_and_release
#include <rsys/dynamic_array.h>

/*******************************************************************************
 * Trace or pursue a radiative path
 ******************************************************************************/
extern LOCAL_SYM res_T
trace_radiative_path_2d
  (struct sdis_scene* scn,
   const float ray_dir[3],
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* temperature);

extern LOCAL_SYM res_T
trace_radiative_path_3d
  (struct sdis_scene* scn,
   const float ray_dir[3],
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* temperature);

extern LOCAL_SYM res_T
radiative_path_2d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* temperature);

extern LOCAL_SYM res_T
radiative_path_3d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* temperature);

/*******************************************************************************
 * Convective path
 ******************************************************************************/
extern LOCAL_SYM res_T
convective_path_2d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* temperature);

extern LOCAL_SYM res_T
convective_path_3d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* temperature);

/*******************************************************************************
 * Conductive path
 ******************************************************************************/
extern LOCAL_SYM res_T
conductive_path_2d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* temperature);

extern LOCAL_SYM res_T
conductive_path_3d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* temperature);

/*******************************************************************************
 * Boundary sub-path
 ******************************************************************************/
extern LOCAL_SYM res_T
boundary_path_2d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_2d* rwalk,
   struct ssp_rng* rng,
   struct temperature_2d* temperature);

extern LOCAL_SYM res_T
boundary_path_3d
  (struct sdis_scene* scn,
   const double fp_to_meter,
   const struct rwalk_context* ctx,
   struct rwalk_3d* rwalk,
   struct ssp_rng* rng,
   struct temperature_3d* temperature);

#endif /* SDIS_HEAT_PATH_H */

