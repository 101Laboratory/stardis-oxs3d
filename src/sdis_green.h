/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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

#ifndef SDIS_GREEN_H
#define SDIS_GREEN_H

#include <rsys/dynamic_array.h>
#include <rsys/ref_count.h>

/*******************************************************************************
 * Spatio temporal vertices registered against the green function
 ******************************************************************************/
struct green_vertex {
  double pos[3];
  double delta_time; /* Time spent into the system */
};

/* Generate the dynamic array of green vertices */
#define DARRAY_NAME green_vertex
#define DARRAY_DATA struct green_vertex
#include <rsys/dynamic_array.h>

/*******************************************************************************
 * Medium registered against the green function
 ******************************************************************************/
struct green_medium {
  struct sdis_medium* mdm;
  double power_term;
  struct darray_green_vertex limit_vertices;
};

static INLINE void
green_medium_init
  (struct mem_allocator* allocator, struct green_medium* gmdm)
{
  ASSERT(gmdm);
  gmdm->mdm = NULL;
  gmdm->power_term = 0;
  darray_green_vertex_init(allocator, &gmdm->limit_vertices);
}

static INLINE void
green_medium_release(struct green_medium* gmdm)
{
  ASSERT(gmdm);
  darray_green_vertex_release(&gmdm->limit_vertices);
}

static INLINE res_T
green_medium_copy
  (struct green_medium* dst, const struct green_medium* src)
{
  ASSERT(dst && src);
  dst->mdm = src->mdm;
  dst->power_term = src->power_term;
  return darray_green_vertex_copy(&dst->limit_vertices, &src->limit_vertices);
}

static INLINE res_T
green_medium_copy_and_release
  (struct green_medium* dst, struct green_medium* src)
{
  ASSERT(dst && src);
  dst->mdm = src->mdm;
  dst->power_term = src->power_term;
  return darray_green_vertex_copy_and_release
    (&dst->limit_vertices, &src->limit_vertices);
}

/* Generate the dynamic array of media registered in the green function */
#define DARRAY_NAME green_medium
#define DARRAY_DATA struct green_medium
#define DARRAY_FUNCTOR_INIT green_medium_init
#define DARRAY_FUNCTOR_RELEASE green_medium_release
#define DARRAY_FUNCTOR_COPY green_medium_copy
#define DARRAY_FUNCTOR_COPY_AND_RELEASE green_medium_copy_and_release
#include <rsys/dynamic_array.h>

/*******************************************************************************
 * Interface registered against the green function
 ******************************************************************************/
struct green_interface {
  struct sdis_interface* interf;
  double flux_term;
  struct darray_green_vertex limit_vertices;
};

static INLINE void
green_interface_init
  (struct mem_allocator* allocator, struct green_interface* ginter)
{
  ASSERT(ginter);
  ginter->interf = NULL;
  ginter->flux_term = 0;
  darray_green_vertex_init(allocator, &ginter->limit_vertices);
}

static INLINE void
green_interface_release(struct green_interface* ginter)
{
  ASSERT(ginter);
  darray_green_vertex_release(&ginter->limit_vertices);
}

static INLINE res_T
green_interface_copy
  (struct green_interface* dst, const struct green_interface* src)
{
  ASSERT(dst && src);
  dst->interf = src->interf;
  dst->flux_term = src->flux_term;
  return darray_green_vertex_copy(&dst->limit_vertices, &src->limit_vertices);
}

static INLINE res_T
green_interface_copy_and_release
  (struct green_interface* dst, struct green_interface* src)
{
  ASSERT(dst && src);
  dst->interf = src->interf;
  dst->flux_term = src->flux_term;
  return darray_green_vertex_copy_and_release
    (&dst->limit_vertices, &src->limit_vertices);
}

/* Generate the dynamic array of interfaces registered in the green function */
#define DARRAY_NAME green_interface
#define DARRAY_DATA struct green_interface
#define DARRAY_FUNCTOR_INIT green_interface_init
#define DARRAY_FUNCTOR_RELEASE green_interface_release
#define DARRAY_FUNCTOR_COPY green_interface_copy
#define DARRAY_FUNCTOR_COPY_AND_RELEASE green_interface_copy_and_release
#include <rsys/dynamic_array.h>

/*******************************************************************************
 * Green function private API
 ******************************************************************************/
struct sdis_green_function {
  struct darray_green_medium media;
  struct darray_green_interface interfaces;

  ref_T ref;
  struct sdis_device* dev;
};

extern LOCAL_SYM res_T
green_function_create
  (struct sdis_device* dev,
   struct sdis_green_function** green);

extern LOCAL_SYM void
green_function_ref_get
  (struct sdis_green_function* greeN);

extern LOCAL_SYM void
green_function_ref_put
  (struct sdis_green_function* green);

extern LOCAL_SYM res_T
green_function_add_medium_limit_vertex
  (struct sdis_green_function* green,
   struct sdis_medium* mdm,
   const struct green_vertex* vertex);

extern LOCAL_SYM res_T
green_function_add_interface_limit_vertex
  (struct sdis_green_function* green,
   struct sdis_interface* interf,
   const struct green_vertex* vertex);

extern LOCAL_SYM res_T
green_function_add_power_term
  (struct sdis_green_function* green,
   struct sdis_medium* mdm,
   const double term);

extern LOCAL_SYM res_T
green_function_add_flux_term
  (struct sdis_green_function* green,
   struct sdis_interface* interf,
   const double term);

#endif /* SDIS_GREEN_H */

