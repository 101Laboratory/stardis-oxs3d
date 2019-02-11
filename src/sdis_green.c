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

#include "sdis_device_c.h"
#include "sdis_green.h"
#include "sdis_medium_c.h"
#include "sdis_interface_c.h"

#include <rsys/dynamic_array.h>
#include <rsys/hash_table.h>
#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#include <rsys/dynamic_array.h>
#include <rsys/ref_count.h>

#include <limits.h>

struct green_vertex {
  double pos[3]; /* Position */
  double delta_time; /* Time "spent" into the system */
  unsigned id; /* Id of the medium/interface onto wich the vertex lies */
};
#define GREEN_VERTEX_NULL__ {{INF,INF,INF}, INF, UINT_MAX}
static const struct green_vertex GREEN_VERTEX_NULL = GREEN_VERTEX_NULL__;

struct green_term {
  double term; /* Term computed during green estimation */
  unsigned id; /* Identifier of the medium/interface of the term */
};
#define GREEN_TERM_NULL__ {INF, UINT_MAX}
static const struct green_term GREEN_TERM_NULL = GREEN_TERM_NULL__;

static INLINE void
green_term_init(struct mem_allocator* allocator, struct green_term* term)
{
  ASSERT(term); (void)allocator;
  *term = GREEN_TERM_NULL;
}

/* Generate the dynamic array of green vertices */
#define DARRAY_NAME green_term
#define DARRAY_DATA struct green_term
#define DARRAY_FUNCTOR_INIT green_term_init
#include <rsys/dynamic_array.h>

struct green_path {
  struct darray_green_term flux_terms; /* List of flux terms */
  struct darray_green_term power_terms; /* List of volumic power terms */
  struct green_vertex limit_vertex; /* End vertex of the path */
  int end_on_interface; /* Define if the limit vertex lies on an interface */

  /* Indices of the last accessed medium/interface. Used to speed up the access
   * to the medium/interface. */
  uint16_t ilast_medium;
  uint16_t ilast_interf;
};

static INLINE void
green_path_init(struct mem_allocator* allocator, struct green_path* path)
{
  ASSERT(path);
  darray_green_term_init(allocator, &path->flux_terms);
  darray_green_term_init(allocator, &path->power_terms);
  path->limit_vertex = GREEN_VERTEX_NULL;
  path->end_on_interface = 0;
  path->ilast_medium = UINT16_MAX;
  path->ilast_interf = UINT16_MAX;
}

static INLINE void
green_path_release(struct green_path* path)
{
  ASSERT(path);
  darray_green_term_release(&path->flux_terms);
  darray_green_term_release(&path->power_terms);
}

static INLINE res_T
green_path_copy(struct green_path* dst, const struct green_path* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  dst->limit_vertex = src->limit_vertex;
  dst->end_on_interface = src->end_on_interface;
  dst->ilast_medium = src->ilast_medium;
  dst->ilast_interf = src->ilast_interf;
  res = darray_green_term_copy(&dst->flux_terms, &src->flux_terms);
  if(res != RES_OK) return res;
  res = darray_green_term_copy(&dst->power_terms, &src->power_terms);
  if(res != RES_OK) return res;
  return RES_OK;
}

static INLINE res_T
green_path_copy_and_clear(struct green_path* dst, struct green_path* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);

  dst->limit_vertex = src->limit_vertex;
  dst->end_on_interface = src->end_on_interface;
  dst->ilast_medium = src->ilast_medium;
  dst->ilast_interf = src->ilast_interf;
  res = darray_green_term_copy_and_clear(&dst->flux_terms, &src->flux_terms);
  if(res != RES_OK) return res;
  res = darray_green_term_copy_and_clear(&dst->power_terms, &src->power_terms);
  if(res != RES_OK) return res;
  return RES_OK;

}

static INLINE res_T
green_path_copy_and_release(struct green_path* dst, struct green_path* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  dst->limit_vertex = src->limit_vertex;
  dst->end_on_interface = src->end_on_interface;
  dst->ilast_medium = src->ilast_medium;
  dst->ilast_interf = src->ilast_interf;
  res = darray_green_term_copy_and_release(&dst->flux_terms, &src->flux_terms);
  if(res != RES_OK) return res;
  res = darray_green_term_copy_and_release(&dst->power_terms, &src->power_terms);
  if(res != RES_OK) return res;
  return RES_OK;
}

/* Generate the dynamic array of green paths */
#define DARRAY_NAME green_path
#define DARRAY_DATA struct green_path
#define DARRAY_FUNCTOR_INIT green_path_init
#define DARRAY_FUNCTOR_RELEASE green_path_release
#define DARRAY_FUNCTOR_COPY green_path_copy
#define DARRAY_FUNCTOR_COPY_AND_RELEASE green_path_copy_and_release
#include <rsys/dynamic_array.h>

/* Generate the hash table that maps and id to an interface */
#define HTABLE_NAME interf
#define HTABLE_KEY unsigned
#define HTABLE_DATA struct sdis_interface*
#include <rsys/hash_table.h>

/* Generate the hash table that maps and id to a medium */
#define HTABLE_NAME medium
#define HTABLE_KEY unsigned
#define HTABLE_DATA struct sdis_medium*
#include <rsys/hash_table.h>

struct sdis_green_function {
  struct htable_medium media;
  struct htable_interf interfaces;
  struct darray_green_path paths; /* List of paths used to estimate the green */

  ref_T ref;
  struct sdis_device* dev;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
ensure_medium_registration
  (struct sdis_green_function* green,
   struct sdis_medium* mdm)
{
  unsigned id;
  res_T res = RES_OK;
  ASSERT(green && mdm);

  id = medium_get_id(mdm);
  if(htable_medium_find(&green->media, &id)) goto exit;

  res = htable_medium_set(&green->media, &id, &mdm);
  if(res != RES_OK) goto error;

  SDIS(medium_ref_get(mdm));

exit:
  return res;
error:
  goto exit;
}

static res_T
ensure_interface_registration
  (struct sdis_green_function* green,
   struct sdis_interface* interf)
{
  unsigned id;
  res_T res = RES_OK;
  ASSERT(green && interf);

  id = interface_get_id(interf);
  if(htable_interf_find(&green->interfaces, &id)) goto exit;

  res = htable_interf_set(&green->interfaces, &id, &interf);
  if(res != RES_OK) goto error;

  SDIS(interface_ref_get(interf));

exit:
  return res;
error:
  goto exit;
}

static void
green_function_clear(struct sdis_green_function* green)
{
  struct htable_medium_iterator it_medium, end_medium;
  struct htable_interf_iterator it_interf, end_interf;
  ASSERT(green);

  /* Clean up medium hash table */
  htable_medium_begin(&green->media, &it_medium);
  htable_medium_end(&green->media, &end_medium);
  while(!htable_medium_iterator_eq(&it_medium, &end_medium)) {
    struct sdis_medium* medium;
    medium = *htable_medium_iterator_data_get(&it_medium);
    SDIS(medium_ref_put(medium));
  }
  htable_medium_clear(&green->media);

  /* Clean up the interface hash table */
  htable_interf_begin(&green->interfaces, &it_interf);
  htable_interf_end(&green->interfaces, &end_interf);
  while(!htable_interf_iterator_eq(&it_interf, &end_interf)) {
    struct sdis_interface* interf;
    interf = *htable_interf_iterator_data_get(&it_interf);
    SDIS(interface_ref_put(interf));
  }
  htable_interf_clear(&green->interfaces);

  /* Clean up the registered paths */
  darray_green_path_clear(&green->paths);
}

static void
green_function_release(ref_T* ref)
{
  struct sdis_device* dev;
  struct sdis_green_function* green;
  ASSERT(ref);
  green = CONTAINER_OF(ref, struct sdis_green_function, ref);
  dev = green->dev;
  green_function_clear(green);
  htable_medium_release(&green->media);
  htable_interf_release(&green->interfaces);
  darray_green_path_release(&green->paths);
  MEM_RM(dev->allocator, green);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_green_function_ref_get(struct sdis_green_function* green)
{
  if(!green) return RES_BAD_ARG;
  ref_get(&green->ref);
  return RES_OK;
}

res_T
sdis_green_function_ref_put(struct sdis_green_function* green)
{
  if(!green) return RES_BAD_ARG;
  ref_put(&green->ref, green_function_release);
  return RES_OK;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
green_function_create
  (struct sdis_device* dev, struct sdis_green_function** out_green)
{
  struct sdis_green_function* green = NULL;
  res_T res = RES_OK;
  ASSERT(dev && out_green);

  green = MEM_CALLOC(dev->allocator, 1, sizeof(*green));
  if(!green) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&green->ref);
  SDIS(device_ref_get(dev));
  green->dev = dev;
  htable_medium_init(dev->allocator, &green->media);
  htable_interf_init(dev->allocator, &green->interfaces);
  darray_green_path_init(dev->allocator, &green->paths);

exit:
  *out_green = green;
  return res;
error:
  if(green) {
    SDIS(green_function_ref_put(green));
    green = NULL;
  }
  goto exit;
}

res_T
green_function_merge_and_clear
  (struct sdis_green_function* dst, struct sdis_green_function* src)
{
  struct htable_medium_iterator it_medium, end_medium;
  struct htable_interf_iterator it_interf, end_interf;
  struct green_path* paths_src;
  struct green_path* paths_dst;
  size_t npaths_src;
  size_t npaths_dst;
  size_t npaths;
  size_t i;
  unsigned id;
  res_T res = RES_OK;
  ASSERT(dst && src);

  if(dst == src) goto exit;

  npaths_src = darray_green_path_size_get(&src->paths);
  npaths_dst = darray_green_path_size_get(&dst->paths);
  npaths = npaths_src + npaths_dst;

  res = darray_green_path_resize(&dst->paths, npaths);
  if(res != RES_OK) goto error;

  paths_src = darray_green_path_data_get(&src->paths);
  paths_dst = darray_green_path_data_get(&dst->paths) + npaths_dst;

  FOR_EACH(i, 0, darray_green_path_size_get(&src->paths)) {
    res = green_path_copy_and_clear(&paths_dst[i], &paths_src[i]);
    if(res != RES_OK) goto error;
  }

  htable_medium_begin(&src->media, &it_medium);
  htable_medium_end(&src->media, &end_medium);
  while(!htable_medium_iterator_eq(&it_medium, &end_medium)) {
    struct sdis_medium* medium;
    medium = *htable_medium_iterator_data_get(&it_medium);
    id = medium_get_id(medium);
    res = htable_medium_set(&dst->media, &id, &medium);
    if(res != RES_OK) goto error;
  }

  htable_interf_begin(&src->interfaces, &it_interf);
  htable_interf_end(&src->interfaces, &end_interf);
  while(!htable_interf_iterator_eq(&it_interf, &end_interf)) {
    struct sdis_interface* interf;
    interf = *htable_interf_iterator_data_get(&it_interf);
    id = interface_get_id(interf);
    res = htable_interf_set(&dst->interfaces, &id, &interf);
    if(res != RES_OK) goto error;
  }

  green_function_clear(src);
exit:
  return res;
error:
  goto exit;
}

res_T
green_function_create_path
  (struct sdis_green_function* green,
   struct green_path_handle* handle)
{
  size_t n;
  res_T res = RES_OK;
  ASSERT(green && handle);

  n = darray_green_path_size_get(&green->paths);
  res = darray_green_path_resize(&green->paths, n+1);
  if(res != RES_OK) return res;

  handle->green = green;
  handle->path = darray_green_path_data_get(&green->paths) + n;
  return RES_OK;
}

res_T
green_path_set_medium_limit_vertex
  (struct green_path_handle* handle,
   struct sdis_medium* mdm,
   const double pos[3],
   const double delta_time)
{
  res_T res = RES_OK;
  ASSERT(handle && mdm && pos);
  res = ensure_medium_registration(handle->green, mdm);
  if(res != RES_OK) return res;
  handle->path->limit_vertex.pos[0] = pos[0];
  handle->path->limit_vertex.pos[1] = pos[1];
  handle->path->limit_vertex.pos[2] = pos[2];
  handle->path->limit_vertex.delta_time = delta_time;
  handle->path->limit_vertex.id = medium_get_id(mdm);
  handle->path->end_on_interface = 0;
  return RES_OK;
}

res_T
green_path_set_interface_limit_vertex
  (struct green_path_handle* handle,
   struct sdis_interface* interf,
   const double pos[3],
   const double delta_time)
{
  res_T res = RES_OK;
  ASSERT(handle && interf && pos);
  res = ensure_interface_registration(handle->green, interf);
  if(res != RES_OK) return res;
  handle->path->limit_vertex.pos[0] = pos[0];
  handle->path->limit_vertex.pos[1] = pos[1];
  handle->path->limit_vertex.pos[2] = pos[2];
  handle->path->limit_vertex.delta_time = delta_time;
  handle->path->limit_vertex.id = interface_get_id(interf);
  handle->path->end_on_interface = 1;
  return RES_OK;
}

res_T
green_path_add_power_term
  (struct green_path_handle* handle,
   struct sdis_medium* mdm,
   const double term)
{
  struct green_path* path;
  struct green_term* terms;
  size_t nterms;
  size_t iterm;
  unsigned id;
  res_T res = RES_OK;
  ASSERT(handle && mdm && term >= 0);

  res = ensure_medium_registration(handle->green, mdm);
  if(res != RES_OK) goto error;

  path = handle->path;
  terms = darray_green_term_data_get(&path->power_terms);
  nterms = darray_green_term_size_get(&path->power_terms);
  id = medium_get_id(mdm);
  iterm = SIZE_MAX;

  /* Early find */
  if(path->ilast_medium < nterms && terms[path->ilast_medium].id == id) {
    iterm = path->ilast_medium;
  } else {
    /* Linear search of the submitted medium */
    FOR_EACH(iterm, 0, nterms) if(terms[iterm].id == id) break;
  }

  /* Add the power term to the path wrt the submitted medium */
  if(iterm < nterms) {
    terms[iterm].term += term;
  } else {
    struct green_term gterm = GREEN_TERM_NULL;
    gterm.term = term;
    gterm.id = id;
    res = darray_green_term_push_back(&handle->path->power_terms, &gterm);
    if(res != RES_OK) goto error;
  }

  /* Register the slot into which the last accessed medium lies */
  CHK(iterm < UINT16_MAX);
  path->ilast_medium = (uint16_t)iterm;

exit:
  return res;
error:
  goto exit;
}

res_T
green_path_add_flux_term
  (struct green_path_handle* handle,
   struct sdis_interface* interf,
   const double term)
{
  struct green_path* path;
  struct green_term* terms;
  size_t nterms;
  size_t iterm;
  unsigned id;
  res_T res = RES_OK;
  ASSERT(handle && interf && term >= 0);

  res = ensure_interface_registration(handle->green, interf);
  if(res != RES_OK) goto error;

  path = handle->path;
  terms = darray_green_term_data_get(&path->flux_terms);
  nterms = darray_green_term_size_get(&path->flux_terms);
  id = interface_get_id(interf);
  iterm = SIZE_MAX;

  /* Early find */
  if(path->ilast_interf < nterms && terms[path->ilast_interf].id == id) {
    iterm = path->ilast_interf;
  } else {
    /* Linear search of the submitted interface */
    FOR_EACH(iterm, 0, nterms) if(terms[iterm].id == id) break;
  }

  /* Add the flux term to the path wrt the submitted interface */
  if(iterm < nterms) {
    terms[iterm].term += term;
  } else {
    struct green_term gterm = GREEN_TERM_NULL;
    gterm.term = term;
    gterm.id = id;
    res = darray_green_term_push_back(&handle->path->flux_terms, &gterm);
    if(res != RES_OK) goto error;
  }

  /* Register the slot into which the last accessed interface lies */
  CHK(iterm < UINT16_MAX);
  path->ilast_interf = (uint16_t)iterm;

exit:
  return res;
error:
  goto exit;
}

