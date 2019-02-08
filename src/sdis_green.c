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
#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#include "sdis_scene_c.h" /* struct darray_<medium|interface> */

#include <rsys/dynamic_array.h>
#include <rsys/ref_count.h>

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

struct sdis_green_function {
  struct darray_medium media; /* List of registered media */
  struct darray_interf interfaces; /* List of registered interfaces */
  struct darray_green_path paths; /* List of paths used to estimate the green */

  /* Indices of the last accessed medium/interface used to speed up the access
   * to the medium/interface. */
  size_t ilast_medium;
  size_t ilast_interf;

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
  size_t i;
  res_T res = RES_OK;
  ASSERT(green && mdm);

  /* Early find */
  if(green->ilast_medium < darray_medium_size_get(&green->media)
  && darray_medium_cdata_get(&green->media)[green->ilast_medium] == mdm)
    goto exit;

  /* Linear search of the submitted medium */
  FOR_EACH(i, 0, darray_medium_size_get(&green->media)) {
    if(darray_medium_cdata_get(&green->media)[i] == mdm) break;
  }

  /* Register the medium if it is not already registered */
  if(i >= darray_medium_size_get(&green->media)) {
    SDIS(medium_ref_get(mdm));
    res = darray_medium_push_back(&green->media, &mdm);
    if(res != RES_OK) goto error;
  }

  green->ilast_medium = i;

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
  size_t i;
  res_T res = RES_OK;
  ASSERT(green && interf);

  /* Early find */
  if(green->ilast_interf < darray_interf_size_get(&green->interfaces)
  && darray_interf_cdata_get(&green->interfaces)[green->ilast_interf] == interf)
    goto exit;

  /* Linear search of the submitted interface */
  FOR_EACH(i, 0, darray_interf_size_get(&green->interfaces)) {
    if(darray_interf_cdata_get(&green->interfaces)[i] == interf) break;
  }

  /* Register the interface if it is not already registered */
  if(i >= darray_interf_size_get(&green->interfaces)) {
    SDIS(interface_ref_get(interf));
    res = darray_interf_push_back(&green->interfaces, &interf);
    if(res != RES_OK) goto error;
  }

  green->ilast_interf = i;

exit:
  return res;
error:
  goto exit;
}

static void
green_function_clear(struct sdis_green_function* green)
{
  size_t i;
  ASSERT(green);

  FOR_EACH(i, 0, darray_medium_size_get(&green->media)) {
    SDIS(medium_ref_put(darray_medium_data_get(&green->media)[i]));
  }
  darray_medium_clear(&green->media);

  FOR_EACH(i, 0, darray_interf_size_get(&green->interfaces)) {
    SDIS(interface_ref_put(darray_interf_data_get(&green->interfaces)[i]));
  }
  darray_interf_clear(&green->interfaces);

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
  darray_medium_release(&green->media);
  darray_interf_release(&green->interfaces);
  darray_green_path_release(&green->paths);
  MEM_RM(dev->allocator, green);
  SDIS(device_ref_put(dev));
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
  darray_medium_init(dev->allocator, &green->media);
  darray_interf_init(dev->allocator, &green->interfaces);
  darray_green_path_init(dev->allocator, &green->paths);
  green->ilast_medium = SIZE_MAX;
  green->ilast_interf = SIZE_MAX;

exit:
  *out_green = green;
  return res;
error:
  if(green) {
    green_function_ref_put(green);
    green = NULL;
  }
  goto exit;
}

void
green_function_ref_get(struct sdis_green_function* green)
{
  ASSERT(green);
  ref_get(&green->ref);
}

void
green_function_ref_put(struct sdis_green_function* green)
{
  ASSERT(green);
  ref_put(&green->ref, green_function_release);
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
green_path_add_medium_limit_vertex
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
green_path_add_interface_limit_vertex
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

