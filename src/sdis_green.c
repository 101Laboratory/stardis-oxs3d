/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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
#include "sdis_estimator_c.h"
#include "sdis_green.h"
#include "sdis_interface_c.h"
#include "sdis_log.h"
#include "sdis_medium_c.h"
#include "sdis_misc.h"
#include "sdis_radiative_env_c.h"
#include "sdis_scene_c.h"
#include "sdis_source_c.h"

#include <star/ssp.h>

#include <rsys/cstr.h>
#include <rsys/dynamic_array.h>
#include <rsys/hash_table.h>
#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>
#include <rsys/dynamic_array.h>

#include <limits.h>

/* Path used to estimate the green function */
struct sdis_green_path {
  /* Internal data. Should not be accessed */
  void* green__;
  size_t id__;
};

struct power_term {
  double term; /* Power term computed during green estimation */
  unsigned id; /* Identifier of the medium of the term */
};

#define POWER_TERM_NULL__ {DBL_MAX, UINT_MAX}
static const struct power_term POWER_TERM_NULL = POWER_TERM_NULL__;

static INLINE void
power_term_init(struct mem_allocator* allocator, struct power_term* term)
{
  ASSERT(term); (void)allocator;
  *term = POWER_TERM_NULL;
}

/* Generate the dynamic array of power terms */
#define DARRAY_NAME power_term
#define DARRAY_DATA struct power_term
#define DARRAY_FUNCTOR_INIT power_term_init
#include <rsys/dynamic_array.h>

struct flux_term {
  double term;
  unsigned id; /* Id of the interface of the flux term */
  enum sdis_side side;
};
#define FLUX_TERM_NULL__ {DBL_MAX, UINT_MAX, SDIS_SIDE_NULL__}
static const struct flux_term FLUX_TERM_NULL = FLUX_TERM_NULL__;

static INLINE void
flux_term_init(struct mem_allocator* allocator, struct flux_term* term)
{
  ASSERT(term); (void)allocator;
  *term = FLUX_TERM_NULL;
}

/* Generate the dynamic array of flux terms */
#define DARRAY_NAME flux_term
#define DARRAY_DATA struct flux_term
#define DARRAY_FUNCTOR_INIT flux_term_init
#include <rsys/dynamic_array.h>

static INLINE void
extflux_terms_init
  (struct mem_allocator* allocator,
   struct sdis_green_external_flux_terms* terms)
{
  ASSERT(terms); (void)allocator;
  *terms = SDIS_GREEN_EXTERNAL_FLUX_TERMS_NULL;
}

/* Generate the dynamic array of the external flux terms */
#define DARRAY_NAME extflux_terms
#define DARRAY_DATA struct sdis_green_external_flux_terms
#define DARRAY_FUNCTOR_INIT extflux_terms_init
#include <rsys/dynamic_array.h>

struct green_path {
  double elapsed_time;
  struct darray_extflux_terms extflux_terms; /* List of external flux terms */
  struct darray_flux_term flux_terms; /* List of flux terms */
  struct darray_power_term power_terms; /* List of volumic power terms */
  union {
    struct sdis_rwalk_vertex vertex;
    struct sdis_interface_fragment fragment;
    struct sdis_radiative_ray ray;
  } limit;
  unsigned limit_id; /* Identifier of the limit medium/interface */
  enum sdis_green_path_end_type end_type;

  /* Indices of the last accessed medium/interface. Used to speed up the access
   * to the medium/interface. */
  uint16_t ilast_medium;
  uint16_t ilast_interf;
};

static INLINE void
green_path_init(struct mem_allocator* allocator, struct green_path* path)
{
  ASSERT(path);
  darray_extflux_terms_init(allocator, &path->extflux_terms);
  darray_flux_term_init(allocator, &path->flux_terms);
  darray_power_term_init(allocator, &path->power_terms);
  path->elapsed_time = -INF;
  path->limit.vertex = SDIS_RWALK_VERTEX_NULL;
  path->limit.fragment = SDIS_INTERFACE_FRAGMENT_NULL;
  path->limit.ray = SDIS_RADIATIVE_RAY_NULL;
  path->limit_id = UINT_MAX;
  path->end_type = SDIS_GREEN_PATH_END_TYPES_COUNT__;
  path->ilast_medium = UINT16_MAX;
  path->ilast_interf = UINT16_MAX;
}

static INLINE void
green_path_release(struct green_path* path)
{
  ASSERT(path);
  darray_flux_term_release(&path->flux_terms);
  darray_power_term_release(&path->power_terms);
  darray_extflux_terms_release(&path->extflux_terms);
}

static INLINE res_T
green_path_copy(struct green_path* dst, const struct green_path* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  dst->elapsed_time = src->elapsed_time;
  dst->limit = src->limit;
  dst->limit_id = src->limit_id;
  dst->end_type = src->end_type;
  dst->ilast_medium = src->ilast_medium;
  dst->ilast_interf = src->ilast_interf;
  res = darray_extflux_terms_copy(&dst->extflux_terms, &src->extflux_terms);
  if(res != RES_OK) return res;
  res = darray_flux_term_copy(&dst->flux_terms, &src->flux_terms);
  if(res != RES_OK) return res;
  res = darray_power_term_copy(&dst->power_terms, &src->power_terms);
  if(res != RES_OK) return res;
  return RES_OK;
}

static INLINE res_T
green_path_copy_and_clear(struct green_path* dst, struct green_path* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  dst->elapsed_time = src->elapsed_time;
  dst->limit = src->limit;
  dst->limit_id = src->limit_id;
  dst->end_type = src->end_type;
  dst->ilast_medium = src->ilast_medium;
  dst->ilast_interf = src->ilast_interf;
  res = darray_extflux_terms_copy_and_clear
    (&dst->extflux_terms, &src->extflux_terms);
  if(res != RES_OK) return res;
  res = darray_flux_term_copy_and_clear(&dst->flux_terms, &src->flux_terms);
  if(res != RES_OK) return res;
  res = darray_power_term_copy_and_clear(&dst->power_terms, &src->power_terms);
  if(res != RES_OK) return res;
  return RES_OK;

}

static INLINE res_T
green_path_copy_and_release(struct green_path* dst, struct green_path* src)
{
  res_T res = RES_OK;
  ASSERT(dst && src);
  dst->elapsed_time = src->elapsed_time;
  dst->limit = src->limit;
  dst->limit_id = src->limit_id;
  dst->end_type = src->end_type;
  dst->ilast_medium = src->ilast_medium;
  dst->ilast_interf = src->ilast_interf;
  res = darray_extflux_terms_copy_and_release
    (&dst->extflux_terms, &src->extflux_terms);
  if(res != RES_OK) return res;
  res = darray_flux_term_copy_and_release(&dst->flux_terms, &src->flux_terms);
  if(res != RES_OK) return res;
  res = darray_power_term_copy_and_release(&dst->power_terms, &src->power_terms);
  if(res != RES_OK) return res;
  return RES_OK;
}

static res_T
green_path_write(const struct green_path* path, FILE* stream)
{
  size_t sz = 0;
  res_T res = RES_OK;
  ASSERT(path && stream);

  #define WRITE(Var, N) {                                                      \
    if(fwrite((Var), sizeof(*(Var)), (N), stream) != (N)) {                    \
      res = RES_IO_ERR;                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  /* Write elapsed time */
  WRITE(&path->elapsed_time, 1);

  /* Write the list of external flux terms */
  sz = darray_extflux_terms_size_get(&path->extflux_terms);
  WRITE(&sz, 1);
  WRITE(darray_extflux_terms_cdata_get(&path->extflux_terms), sz);

  /* Write the list of flux terms */
  sz = darray_flux_term_size_get(&path->flux_terms);
  WRITE(&sz, 1);
  WRITE(darray_flux_term_cdata_get(&path->flux_terms), sz);

  /* Write the list of power terms */
  sz = darray_power_term_size_get(&path->power_terms);
  WRITE(&sz, 1);
  WRITE(darray_power_term_cdata_get(&path->power_terms), sz);

  /* Write the limit point */
  WRITE(&path->limit, 1);
  WRITE(&path->limit_id, 1);
  WRITE(&path->end_type, 1);

  /* Write miscellaneous data */
  WRITE(&path->ilast_medium, 1);
  WRITE(&path->ilast_interf, 1);

  #undef WRITE

exit:
  return res;
error:
  goto exit;
}

static res_T
green_path_read(struct green_path* path, FILE* stream)
{
  size_t sz = 0;
  res_T res = RES_OK;
  ASSERT(path && stream);

  #define READ(Var, N) {                                                       \
    if(fread((Var), sizeof(*(Var)), (N), stream) != (N)) {                     \
      if(feof(stream)) {                                                       \
        res = RES_BAD_ARG;                                                     \
      } else if(ferror(stream)) {                                              \
        res = RES_IO_ERR;                                                      \
      } else {                                                                 \
        res = RES_UNKNOWN_ERR;                                                 \
      }                                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  /* Read elapsed time */
  READ(&path->elapsed_time, 1);

  /* Read the list of external flux terms */
  READ(&sz, 1);
  res = darray_extflux_terms_resize(&path->extflux_terms, sz);
  if(res != RES_OK) goto error;
  READ(darray_extflux_terms_data_get(&path->extflux_terms), sz);

  /* Read the list of flux terms */
  READ(&sz, 1);
  res = darray_flux_term_resize(&path->flux_terms, sz);
  if(res != RES_OK) goto error;
  READ(darray_flux_term_data_get(&path->flux_terms), sz);

  /* Read the list of power tems */
  READ(&sz, 1);
  res = darray_power_term_resize(&path->power_terms, sz);
  if(res != RES_OK) goto error;
  READ(darray_power_term_data_get(&path->power_terms), sz);

  /* Read the limit point */
  READ(&path->limit, 1);
  READ(&path->limit_id, 1);
  READ(&path->end_type, 1);

  /* Read the miscellaneous data */
  READ(&path->ilast_medium, 1);
  READ(&path->ilast_interf, 1);

  #undef READ

exit:
  return res;
error:
  goto exit;
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

/* Generate the hash table that maps an id to a medium */
#define HTABLE_NAME medium
#define HTABLE_KEY unsigned
#define HTABLE_DATA struct sdis_medium*
#include <rsys/hash_table.h>

struct sdis_green_function {
  struct htable_medium media;
  struct htable_interf interfaces;
  struct darray_green_path paths; /* List of paths used to estimate the green */

  size_t npaths_valid;
  size_t npaths_invalid;
  hash256_T signature;

  struct accum realisation_time; /* Time per realisation */

  enum ssp_rng_type rng_type;
  FILE* rng_state;

  ref_T ref;
  struct sdis_scene* scn;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE res_T
check_sdis_green_function_create_from_stream_args
  (const struct sdis_green_function_create_from_stream_args* args)
{
  if(!args || !args->scene || !args->stream)
    return RES_BAD_ARG;
  return RES_OK;
}

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

static FINLINE struct sdis_medium*
green_function_fetch_medium
  (struct sdis_green_function* green, const unsigned medium_id)
{
  struct sdis_medium* const* pmdm = NULL;
  ASSERT(green);
  pmdm = htable_medium_find(&green->media, &medium_id);
  ASSERT(pmdm);
  return *pmdm;
}

static FINLINE struct sdis_interface*
green_function_fetch_interf
  (struct sdis_green_function* green, const unsigned interf_id)
{
  struct sdis_interface* const* pinterf = NULL;
  ASSERT(green);
  pinterf = htable_interf_find(&green->interfaces, &interf_id);
  ASSERT(pinterf);
  return *pinterf;
}

static double /* [K] */
green_path_power_contribution
  (struct sdis_green_function* green,
   const struct green_path* path)
{
  double temperature = 0; /* [K] */
  size_t i=0, n=0;

  ASSERT(green && path);

  n = darray_power_term_size_get(&path->power_terms);
  FOR_EACH(i, 0, n) {
    struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;
    const struct power_term* power_term = NULL;
    const struct sdis_medium* medium = NULL;
    double power = 0; /* [W] */

    power_term = darray_power_term_cdata_get(&path->power_terms) + i;
    medium = green_function_fetch_medium(green, power_term->id);

    /* Dummy argument used only to satisfy the function profile used to recover
     * power. Its position is unused, since power is assumed to be constant in
     * space, and its time is set to infinity, since the green function is
     * assumed to be evaluated at steady state */
    vtx.time = INF;
    power = solid_get_volumic_power(medium, &vtx);

    temperature += power_term->term * power; /* [K] */
  }
  return temperature; /* [K] */
}

static double /* [K] */
green_path_flux_contribution
  (struct sdis_green_function* green,
   const struct green_path* path)
{
  double temperature = 0;
  size_t i=0, n=0;

  ASSERT(green && path);

  n = darray_flux_term_size_get(&path->flux_terms);
  FOR_EACH(i, 0, n) {
    struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
    const struct flux_term* flux_term = NULL;
    const struct sdis_interface* interf = NULL;
    double flux = 0; /* [W/m^2] */

    flux_term = darray_flux_term_cdata_get(&path->flux_terms) + i;
    interf = green_function_fetch_interf(green, flux_term->id);

    /* Interface fragment. Its position is unused, since flux is assumed to be
     * constant in space, and its time is set to infinity, since the green
     * function is assumed to be evaluated at steady state */
    frag.time = INF;
    frag.side = flux_term->side;
    flux = interface_side_get_flux(interf, &frag);

    temperature += flux_term->term * flux; /* [K] */
  }
  return temperature; /* [K] */
}

static double /* [K] */
green_path_external_flux_contribution
  (struct sdis_green_function* green,
   const struct green_path* path)
{
  const struct sdis_source* extsrc = NULL;
  double value = 0;
  size_t i=0, n=0;

  ASSERT(green && path);

  if((extsrc = green->scn->source) == NULL) return 0;

  n = darray_extflux_terms_size_get(&path->extflux_terms);
  FOR_EACH(i, 0, n) {
    const struct sdis_green_external_flux_terms* extflux = NULL;
    double power = 0; /* [W] */
    double diffrad = 0; /* [W/m^2/sr] */

    extflux = darray_extflux_terms_cdata_get(&path->extflux_terms)+i;
    power = source_get_power(extsrc, extflux->time);
    diffrad = source_get_diffuse_radiance(extsrc, extflux->time, extflux->dir);

    value += extflux->term_wrt_power * power; /* [K] */
    value += extflux->term_wrt_diffuse_radiance * diffrad; /* [K] */
  }
  return value; /* [K] */
}

static res_T
green_function_solve_path
  (struct sdis_green_function* green,
   const size_t ipath,
   double* weight)
{
  const struct green_path* path = NULL;
  const struct sdis_medium* medium = NULL;
  const struct sdis_interface* interf = NULL;
  struct sdis_scene* scn = NULL;
  struct sdis_rwalk_vertex vtx = SDIS_RWALK_VERTEX_NULL;
  struct sdis_interface_fragment frag = SDIS_INTERFACE_FRAGMENT_NULL;
  struct sdis_radiative_ray ray = SDIS_RADIATIVE_RAY_NULL;
  double power;
  double flux;
  double external_flux;
  double end_temperature;
  res_T res = RES_OK;
  ASSERT(green && ipath < darray_green_path_size_get(&green->paths) && weight);

  path = darray_green_path_cdata_get(&green->paths) + ipath;
  if(path->end_type == SDIS_GREEN_PATH_END_ERROR) { /* Rejected path */
    res = RES_BAD_OP;
    goto error;
  }

  power = green_path_power_contribution(green, path);
  flux = green_path_flux_contribution(green, path);
  external_flux = green_path_external_flux_contribution(green, path);

  /* Compute path's end temperature */
  switch(path->end_type) {
    case SDIS_GREEN_PATH_END_AT_INTERFACE:
      interf = green_function_fetch_interf(green, path->limit_id);
      frag = path->limit.fragment;
      end_temperature = interface_side_get_temperature(interf, &frag);
      break;
    case SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV:
      SDIS(green_function_get_scene(green, &scn));
      ray = path->limit.ray;
      end_temperature = radiative_env_get_temperature(green->scn->radenv, &ray);
      break;
    case SDIS_GREEN_PATH_END_IN_VOLUME:
      medium = green_function_fetch_medium(green, path->limit_id);
      vtx = path->limit.vertex;
      end_temperature = medium_get_temperature(medium, &vtx);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

  if(SDIS_TEMPERATURE_IS_UNKNOWN(end_temperature)) {
    log_err(green->scn->dev,
      "%s: unknown boundary/initial condition.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  /* Compute the path weight */
  *weight = power + flux + external_flux + end_temperature;

exit:
  return res;
error:
  goto exit;
}

static res_T
write_media(struct sdis_green_function* green, FILE* stream)
{
  struct htable_medium_iterator it, it_end;
  size_t nmedia = 0;
  res_T res = RES_OK;
  ASSERT(green && stream);

  #define WRITE(Var) {                                                         \
    if(fwrite((Var), sizeof(*(Var)), 1, stream) != 1) {                        \
      res = RES_IO_ERR;                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  nmedia = htable_medium_size_get(&green->media);
  WRITE(&nmedia);

  htable_medium_begin(&green->media, &it);
  htable_medium_end(&green->media, &it_end);
  while(!htable_medium_iterator_eq(&it, &it_end)) {
    const struct sdis_medium* mdm = *htable_medium_iterator_data_get(&it);
    htable_medium_iterator_next(&it);
    WRITE(&mdm->id);
    WRITE(&mdm->type);
  }

  #undef WRITE

exit:
  return res;
error:
  goto exit;
}

static res_T
read_media(struct sdis_green_function* green, FILE* stream)
{
  size_t nmedia = 0;
  size_t imedium = 0;
  res_T res = RES_OK;
  ASSERT(green && stream);

  #define READ(Var) {                                                          \
    if(fread((Var), sizeof(*(Var)), 1, stream) != 1) {                         \
      if(feof(stream)) {                                                       \
        res = RES_BAD_ARG;                                                     \
      } else if(ferror(stream)) {                                              \
        res = RES_IO_ERR;                                                      \
      } else {                                                                 \
        res = RES_UNKNOWN_ERR;                                                 \
      }                                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  READ(&nmedia);
  FOR_EACH(imedium, 0, nmedia) {
    struct name* name = NULL;
    struct sdis_medium* mdm = NULL;
    struct fid id;
    enum sdis_medium_type mdm_type;

    READ(&id);
    READ(&mdm_type);

    name = flist_name_get(&green->scn->dev->media_names, id);
    if(!name) {
      log_err(green->scn->dev, "%s: a Stardis medium is missing.\n",
        FUNC_NAME);
      res = RES_BAD_ARG;
      goto error;
    }

    mdm = name->mem;

    if(mdm_type != mdm->type) {
      log_err(green->scn->dev, "%s: inconsistency between the a Stardis medium "
        "and its serialised data.\n", FUNC_NAME);
      res = RES_BAD_ARG;
      goto error;
    }

    res = ensure_medium_registration(green, mdm);
    if(res != RES_OK) goto error;
  }

  #undef READ

exit:
  return res;
error:
  goto exit;
}

static res_T
write_interfaces(struct sdis_green_function* green, FILE* stream)
{
  struct htable_interf_iterator it, it_end;
  size_t ninterfaces = 0;
  res_T res = RES_OK;
  ASSERT(green && stream);

  #define WRITE(Var) {                                                         \
    if(fwrite((Var), sizeof(*(Var)), 1, stream) != 1) {                        \
      res = RES_IO_ERR;                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0
  ninterfaces = htable_interf_size_get(&green->interfaces);
  WRITE(&ninterfaces);

  htable_interf_begin(&green->interfaces, &it);
  htable_interf_end(&green->interfaces, &it_end);
  while(!htable_interf_iterator_eq(&it, &it_end)) {
    const struct sdis_interface* interf = *htable_interf_iterator_data_get(&it);
    htable_interf_iterator_next(&it);
    WRITE(&interf->id);
    WRITE(&interf->medium_front->id);
    WRITE(&interf->medium_front->type);
    WRITE(&interf->medium_back->id);
    WRITE(&interf->medium_back->type);
  }
  #undef WRITE

exit:
  return res;
error:
  goto exit;
}

static res_T
read_interfaces(struct sdis_green_function* green, FILE* stream)
{
  size_t ninterfs = 0;
  size_t iinterf = 0;
  res_T res = RES_OK;
  ASSERT(green && stream);

  #define READ(Var) {                                                          \
    if(fread((Var), sizeof(*(Var)), 1, stream) != 1) {                         \
      if(feof(stream)) {                                                       \
        res = RES_BAD_ARG;                                                     \
      } else if(ferror(stream)) {                                              \
        res = RES_IO_ERR;                                                      \
      } else {                                                                 \
        res = RES_UNKNOWN_ERR;                                                 \
      }                                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  READ(&ninterfs);
  FOR_EACH(iinterf, 0, ninterfs) {
    struct name* name = NULL;
    struct sdis_interface* interf = NULL;
    struct sdis_medium* mdm_front = NULL;
    struct sdis_medium* mdm_back = NULL;
    struct fid id;
    struct fid mdm_front_id;
    struct fid mdm_back_id;
    enum sdis_medium_type mdm_front_type;
    enum sdis_medium_type mdm_back_type;

    READ(&id);
    READ(&mdm_front_id);
    READ(&mdm_front_type);
    READ(&mdm_back_id);
    READ(&mdm_back_type);

    name = flist_name_get(&green->scn->dev->interfaces_names, id);
    if(!name) {
      log_err(green->scn->dev, "%s: a Stardis interface is missing.\n",
        FUNC_NAME);
      res = RES_BAD_ARG;
      goto error;
    }

    interf = name->mem;
    mdm_front = flist_name_get(&green->scn->dev->media_names, mdm_front_id)->mem;
    mdm_back = flist_name_get(&green->scn->dev->media_names, mdm_back_id)->mem;

    if(mdm_front != interf->medium_front
    || mdm_back != interf->medium_back
    || mdm_front_type != interf->medium_front->type
    || mdm_back_type != interf->medium_back->type) {
      log_err(green->scn->dev, "%s: inconsistency between the a Stardis interface "
        "and its serialised data.\n", FUNC_NAME);
      res = RES_BAD_ARG;
      goto error;
    }

    res = ensure_interface_registration(green, interf);
    if(res != RES_OK) goto error;
  }

  #undef READ

exit:
  return res;
error:
  goto exit;
}

static res_T
write_paths_list(struct sdis_green_function* green, FILE* stream)
{
  size_t npaths = 0;
  size_t ipath = 0;
  res_T res = RES_OK;
  ASSERT(green && stream);

  #define WRITE(Var) {                                                         \
    if(fwrite((Var), sizeof(*(Var)), 1, stream) != 1) {                        \
      res = RES_IO_ERR;                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0
  npaths = darray_green_path_size_get(&green->paths);
  WRITE(&npaths);
  FOR_EACH(ipath, 0, npaths) {
    const struct green_path* path = NULL;
    path = darray_green_path_cdata_get(&green->paths) + ipath;

    res = green_path_write(path, stream);
    if(res != RES_OK) goto error;
  }
  #undef WRITE

exit:
  return res;
error:
  goto exit;
}

static res_T
read_paths_list(struct sdis_green_function* green, FILE* stream)
{
  size_t npaths = 0;
  size_t ipath = 0;
  res_T res = RES_OK;

  #define READ(Var) {                                                          \
    if(fread((Var), sizeof(*(Var)), 1, stream) != 1) {                         \
      if(feof(stream)) {                                                       \
        res = RES_BAD_ARG;                                                     \
      } else if(ferror(stream)) {                                              \
        res = RES_IO_ERR;                                                      \
      } else {                                                                 \
        res = RES_UNKNOWN_ERR;                                                 \
      }                                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  READ(&npaths);
  res = darray_green_path_resize(&green->paths, npaths);
  if(res != RES_OK) goto error;

  FOR_EACH(ipath, 0, npaths) {
    struct green_path* path = NULL;
    path = darray_green_path_data_get(&green->paths) + ipath;

    res = green_path_read(path, stream);
    if(res != RES_OK) goto error;
  }
  #undef READ

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
    htable_medium_iterator_next(&it_medium);
  }
  htable_medium_clear(&green->media);

  /* Clean up the interface hash table */
  htable_interf_begin(&green->interfaces, &it_interf);
  htable_interf_end(&green->interfaces, &end_interf);
  while(!htable_interf_iterator_eq(&it_interf, &end_interf)) {
    struct sdis_interface* interf;
    interf = *htable_interf_iterator_data_get(&it_interf);
    SDIS(interface_ref_put(interf));
    htable_interf_iterator_next(&it_interf);
  }
  htable_interf_clear(&green->interfaces);

  /* Clean up the registered paths */
  darray_green_path_clear(&green->paths);
}

static void
green_function_release(ref_T* ref)
{
  struct sdis_scene* scn;
  struct sdis_green_function* green;
  ASSERT(ref);
  green = CONTAINER_OF(ref, struct sdis_green_function, ref);
  scn = green->scn;
  green_function_clear(green);
  htable_medium_release(&green->media);
  htable_interf_release(&green->interfaces);
  darray_green_path_release(&green->paths);
  if(green->rng_state) fclose(green->rng_state);
  MEM_RM(scn->dev->allocator, green);
  SDIS(scene_ref_put(scn));
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

res_T
sdis_green_function_solve
  (struct sdis_green_function* green,
   struct sdis_estimator** out_estimator)
{
  struct sdis_estimator* estimator = NULL;
  size_t npaths;
  size_t ipath;
  size_t N = 0; /* #realisations */
  double accum = 0;
  double accum2 = 0;
  res_T res = RES_OK;

  if(!green || !out_estimator) {
    res = RES_BAD_ARG;
    goto error;
  }

  npaths = darray_green_path_size_get(&green->paths);

  /* Create the estimator */
  res = estimator_create(green->scn->dev, SDIS_ESTIMATOR_TEMPERATURE, &estimator);
  if(res != RES_OK) goto error;

  /* Solve the green function */
  FOR_EACH(ipath, 0, npaths) {
    double w;

    res = green_function_solve_path(green, ipath, &w);
    if(res == RES_BAD_OP) continue;
    if(res != RES_OK) goto error;

    accum += w;
    accum2 += w*w;
    ++N;
  }

  /* Setup the estimated temperature */
  estimator_setup_realisations_count(estimator, npaths, N);
  estimator_setup_temperature(estimator, accum, accum2);
  estimator_setup_realisation_time
    (estimator, green->realisation_time.sum, green->realisation_time.sum2);

exit:
  if(out_estimator) *out_estimator = estimator;
  return res;
error:
  if(estimator) {
    SDIS(estimator_ref_put(estimator));
    estimator = NULL;
  }
  goto exit;
}

res_T
sdis_green_function_write(struct sdis_green_function* green, FILE* stream)
{
  struct ssp_rng* rng = NULL;
  hash256_T hash;
  res_T res = RES_OK;

  if(!green || !stream) {
    res = RES_BAD_ARG;
    goto error;
  }

  #define WRITE(Var, Nb) {                                                     \
    if(fwrite((Var), sizeof(*(Var)), (Nb), stream) != (Nb)) {                  \
      res = RES_IO_ERR;                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  if(green->rng_type == SSP_RNG_TYPE_NULL) {
    log_err(green->scn->dev,
      "%s: could not write a green function with an unknown RNG type.\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  WRITE(&SDIS_GREEN_FUNCTION_VERSION, 1);

  res = scene_compute_hash(green->scn, hash);
  if(res != RES_OK) goto error;

  WRITE(hash, sizeof(hash256_T));
  WRITE(green->signature, sizeof(hash256_T));

  res = write_media(green, stream);
  if(res != RES_OK) goto error;
  res = write_interfaces(green, stream);
  if(res != RES_OK) goto error;
  res = write_paths_list(green, stream);
  if(res != RES_OK) goto error;

  WRITE(&green->npaths_valid, 1);
  WRITE(&green->npaths_invalid, 1);
  WRITE(&green->realisation_time, 1);
  WRITE(&green->rng_type, 1);
  #undef WRITE

  /* Create a temporary RNG used to serialise the RNG state */
  res = ssp_rng_create(green->scn->dev->allocator, green->rng_type, &rng);
  if(res != RES_OK) goto error;
  rewind(green->rng_state);
  res = ssp_rng_read(rng, green->rng_state);
  if(res != RES_OK) goto error;
  res = ssp_rng_write(rng, stream);
  if(res != RES_OK) goto error;

exit:
  if(rng) SSP(rng_ref_put(rng));
  return res;
error:
  goto exit;
}

res_T
sdis_green_function_create_from_stream
  (struct sdis_green_function_create_from_stream_args* args,
   struct sdis_green_function** out_green)
{
  hash256_T hash0, hash1;
  struct sdis_green_function* green = NULL;
  struct ssp_rng* rng = NULL;
  int version = 0;
  res_T res = RES_OK;

  if(!out_green) { res = RES_BAD_ARG; goto error; }
  res = check_sdis_green_function_create_from_stream_args(args);
  if(res != RES_OK) goto error;

  res = green_function_create(args->scene, args->signature, &green);
  if(res != RES_OK) goto error;

  #define READ(Var, Nb) {                                                      \
    if(fread((Var), sizeof(*(Var)), (Nb), args->stream) != (Nb)) {             \
      if(feof(args->stream)) {                                                 \
        res = RES_BAD_ARG;                                                     \
      } else if(ferror(args->stream)) {                                        \
        res = RES_IO_ERR;                                                      \
      } else {                                                                 \
        res = RES_UNKNOWN_ERR;                                                 \
      }                                                                        \
      goto error;                                                              \
    }                                                                          \
  } (void)0

  READ(&version, 1);
  if(version != SDIS_GREEN_FUNCTION_VERSION) {
    log_err(green->scn->dev,
      "%s: unexpected green function version %d. Expecting a green function "
      "in version %d.\n",
      FUNC_NAME, version, SDIS_GREEN_FUNCTION_VERSION);
    res = RES_BAD_ARG;
    goto error;
  }

  res = scene_compute_hash(green->scn, hash0);
  if(res != RES_OK) goto error;

  READ(hash1, sizeof(hash256_T));
  if(!hash256_eq(hash0, hash1)) {
    log_err(green->scn->dev,
      "%s: the submitted scene does not match the scene used to estimate the "
      "green function.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  READ(hash1, sizeof(hash256_T));
  if(!hash256_eq(hash1, green->signature)) {
    log_err(green->scn->dev,
      "%s: the input signature does not match the saved signature\n",
      FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  res = read_media(green, args->stream);
  if(res != RES_OK) goto error;
  res = read_interfaces(green, args->stream);
  if(res != RES_OK) goto error;
  res = read_paths_list(green, args->stream);
  if(res != RES_OK) goto error;

  READ(&green->npaths_valid, 1);
  READ(&green->npaths_invalid, 1);
  READ(&green->realisation_time, 1);
  READ(&green->rng_type, 1);
  #undef READ

  /* Create a temporary RNG used to deserialise the RNG state */
  res = ssp_rng_create(green->scn->dev->allocator, green->rng_type, &rng);
  if(res != RES_OK) goto error;
  res = ssp_rng_read(rng, args->stream);
  if(res != RES_OK) goto error;
  res = ssp_rng_write(rng, green->rng_state);
  if(res != RES_OK) goto error;

exit:
  if(rng) SSP(rng_ref_put(rng));
  if(out_green) *out_green = green;
  return res;
error:
  if(green) {
    SDIS(green_function_ref_put(green));
    green = NULL;
  }
  goto exit;
}

res_T
sdis_green_function_get_scene
  (const struct sdis_green_function* green,
   struct sdis_scene** scn)
{
  if(!green || !scn) return RES_BAD_ARG;
  ASSERT(green->npaths_valid != SIZE_MAX);
  *scn = green->scn;
  return RES_OK;
}

res_T
sdis_green_function_get_paths_count
  (const struct sdis_green_function* green, size_t* npaths)
{
  if(!green || !npaths) return RES_BAD_ARG;
  ASSERT(green->npaths_valid != SIZE_MAX);
  *npaths = green->npaths_valid;
  return RES_OK;
}

res_T
sdis_green_function_get_invalid_paths_count
  (const struct sdis_green_function* green, size_t* nfails)
{
  if(!green || !nfails) return RES_BAD_ARG;
  ASSERT(green->npaths_invalid != SIZE_MAX);
  *nfails = green->npaths_invalid;
  return RES_OK;
}

res_T
sdis_green_function_get_signature
  (const struct sdis_green_function* green, hash256_T signature)
{
  if(!green || !signature) return RES_BAD_ARG;
  memcpy(signature, green->signature, sizeof(hash256_T));
  return RES_OK;
}

res_T
sdis_green_function_for_each_path
  (struct sdis_green_function* green,
   sdis_process_green_path_T func,
   void* context)
{
  size_t npaths;
  size_t ipath;
  res_T res = RES_OK;

  if(!green || !func) {
    res = RES_BAD_ARG;
    goto error;
  }

  npaths = darray_green_path_size_get(&green->paths);
  FOR_EACH(ipath, 0, npaths) {
    struct sdis_green_path path_handle;
    const struct green_path* path = darray_green_path_cdata_get(&green->paths)+ipath;

    if(path->end_type == SDIS_GREEN_PATH_END_ERROR) continue;

    path_handle.green__ = green;
    path_handle.id__ = ipath;

    res = func(&path_handle, context);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_green_path_get_elapsed_time
  (struct sdis_green_path* path_handle, double* elapsed)
{
  const struct green_path* path = NULL;
  struct sdis_green_function* green = NULL;
  res_T res = RES_OK;

  if(!path_handle || !elapsed) {
    res = RES_BAD_ARG;
    goto error;
  }

  green = path_handle->green__;
  ASSERT(path_handle->id__ < darray_green_path_size_get(&green->paths));

  path = darray_green_path_cdata_get(&green->paths) + path_handle->id__;
  *elapsed = path->elapsed_time;

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_green_path_get_end
  (struct sdis_green_path* path_handle,
   struct sdis_green_path_end* end)
{
  const struct green_path* path = NULL;
  struct sdis_green_function* green = NULL;
  res_T res = RES_OK;

  if(!path_handle || !end) {
    res = RES_BAD_ARG;
    goto error;
  }

  green = path_handle->green__;
  ASSERT(path_handle->id__ < darray_green_path_size_get(&green->paths));

  path = darray_green_path_cdata_get(&green->paths) + path_handle->id__;
  end->type = path->end_type;

  switch(path->end_type) {
    case SDIS_GREEN_PATH_END_AT_INTERFACE:
      end->data.itfrag.intface = green_function_fetch_interf(green, path->limit_id);
      end->data.itfrag.fragment = path->limit.fragment;
      break;
    case SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV:
      end->data.radenvray.radenv = green->scn->radenv;
      end->data.radenvray.ray = path->limit.ray;
      break;
    case SDIS_GREEN_PATH_END_IN_VOLUME:
      end->data.mdmvert.medium = green_function_fetch_medium(green, path->limit_id);
      end->data.mdmvert.vertex = path->limit.vertex;
      break;
    case SDIS_GREEN_PATH_END_ERROR:
      res = RES_BAD_OP;
      goto error;
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_green_path_get_green_function
  (struct sdis_green_path* path_handle,
   struct sdis_green_function** out_green)

{
  struct sdis_green_function* green = NULL;
  res_T res = RES_OK;

  if(!path_handle || !out_green) {
    res = RES_BAD_ARG;
    goto error;
  }

  green = path_handle->green__;
  ASSERT(path_handle->id__ < darray_green_path_size_get(&green->paths));

  *out_green = green;

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_green_function_get_power_terms_count
  (const struct sdis_green_path* path_handle,
   size_t* nterms)
{
  const struct green_path* path = NULL;
  struct sdis_green_function* green = NULL;
  res_T res = RES_OK;

  if(!path_handle || !nterms) {
    res = RES_BAD_ARG;
    goto error;
  }

  green = path_handle->green__; (void)green;
  ASSERT(path_handle->id__ < darray_green_path_size_get(&green->paths));

  path = darray_green_path_cdata_get(&green->paths) + path_handle->id__;

  *nterms = darray_power_term_size_get(&path->power_terms);

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_green_function_get_flux_terms_count
  (const struct sdis_green_path* path_handle,
   size_t* nterms)
{
  const struct green_path* path = NULL;
  struct sdis_green_function* green = NULL;
  res_T res = RES_OK;

  if(!path_handle || !nterms) {
    res = RES_BAD_ARG;
    goto error;
  }

  green = path_handle->green__; (void)green;
  ASSERT(path_handle->id__ < darray_green_path_size_get(&green->paths));

  path = darray_green_path_cdata_get(&green->paths) + path_handle->id__;

  *nterms = darray_flux_term_size_get(&path->flux_terms);

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_green_function_get_external_flux_terms_count
  (const struct sdis_green_path* path_handle,
   size_t* nterms)
{
  const struct green_path* path = NULL;
  struct sdis_green_function* green = NULL;
  res_T res = RES_OK;

  if(!path_handle || !nterms) {
    res = RES_BAD_ARG;
    goto error;
  }

  green = path_handle->green__; (void)green;
  ASSERT(path_handle->id__ < darray_green_path_size_get(&green->paths));

  path = darray_green_path_cdata_get(&green->paths) + path_handle->id__;

  *nterms = darray_extflux_terms_size_get(&path->extflux_terms);

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_green_path_for_each_power_term
  (struct sdis_green_path* path_handle,
   sdis_process_medium_power_term_T func,
   void* context)
{
  const struct green_path* path = NULL;
  struct sdis_green_function* green = NULL;
  const struct power_term* terms = NULL;
  size_t i, n;
  res_T res = RES_OK;

  if(!path_handle || !func) {
    res = RES_BAD_ARG;
    goto error;
  }

  green = path_handle->green__;
  ASSERT(path_handle->id__ < darray_green_path_size_get(&green->paths));

  path = darray_green_path_cdata_get(&green->paths) + path_handle->id__;

  n = darray_power_term_size_get(&path->power_terms);
  terms = darray_power_term_cdata_get(&path->power_terms);
  FOR_EACH(i, 0, n) {
    struct sdis_medium* mdm;
    mdm = green_function_fetch_medium(green, terms[i].id);
    res = func(mdm, terms[i].term, context);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_green_path_for_each_flux_term
  (struct sdis_green_path* path_handle,
   sdis_process_interface_flux_term_T func,
   void* context)
{
  const struct green_path* path = NULL;
  struct sdis_green_function* green = NULL;
  const struct flux_term* terms = NULL;
  size_t i, n;
  res_T res = RES_OK;

  if(!path_handle || !func) {
    res = RES_BAD_ARG;
    goto error;
  }

  green = path_handle->green__;
  ASSERT(path_handle->id__ < darray_green_path_size_get(&green->paths));

  path = darray_green_path_cdata_get(&green->paths) + path_handle->id__;

  n = darray_flux_term_size_get(&path->flux_terms);
  terms = darray_flux_term_cdata_get(&path->flux_terms);
  FOR_EACH(i, 0, n) {
    struct sdis_interface* interf;
    interf = green_function_fetch_interf(green, terms[i].id);

    res = func(interf, terms[i].side, terms[i].term, context);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
sdis_green_path_for_each_external_flux_terms
  (struct sdis_green_path* path_handle,
   sdis_process_external_flux_terms_T func,
   void* context)
{
  const struct green_path* path = NULL;
  struct sdis_green_function* green = NULL;
  size_t i, n;
  res_T res = RES_OK;

  if(!path_handle || !func) {
    res = RES_BAD_ARG;
    goto error;
  }

  green = path_handle->green__;
  ASSERT(path_handle->id__ < darray_green_path_size_get(&green->paths));

  path = darray_green_path_cdata_get(&green->paths) + path_handle->id__;

  n = darray_extflux_terms_size_get(&path->extflux_terms);
  if(n && !green->scn->source) {
    /* In can't have external flux terms without an external source */
    log_err(green->scn->dev, "%s: the external source is missing\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  FOR_EACH(i, 0, n) {
    const struct sdis_green_external_flux_terms* terms = NULL;
    terms = darray_extflux_terms_cdata_get(&path->extflux_terms) + i;

    res = func(green->scn->source, terms, context);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}


/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
green_function_create
  (struct sdis_scene* scn,
   const hash256_T signature,
   struct sdis_green_function** out_green)
{
  struct sdis_green_function* green = NULL;
  res_T res = RES_OK;
  ASSERT(scn && out_green);

  green = MEM_CALLOC(scn->dev->allocator, 1, sizeof(*green));
  if(!green) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&green->ref);
  SDIS(scene_ref_get(scn));
  green->scn = scn;
  htable_medium_init(scn->dev->allocator, &green->media);
  htable_interf_init(scn->dev->allocator, &green->interfaces);
  darray_green_path_init(scn->dev->allocator, &green->paths);
  green->npaths_valid = SIZE_MAX;
  green->npaths_invalid = SIZE_MAX;
  memcpy(green->signature, signature, sizeof(hash256_T));

  /* TODO replace the tmpfile. tmpfile can only be called a limited number of
   * times while one could create a huge amount of green functions at the same
   * time (e.g. for image rendering) */
  green->rng_state = tmpfile();
  if(!green->rng_state) {
    res = RES_IO_ERR;
    goto error;
  }

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
    res = ensure_medium_registration(dst, medium);
    if(res != RES_OK) goto error;
    htable_medium_iterator_next(&it_medium);
  }

  htable_interf_begin(&src->interfaces, &it_interf);
  htable_interf_end(&src->interfaces, &end_interf);
  while(!htable_interf_iterator_eq(&it_interf, &end_interf)) {
    struct sdis_interface* interf;
    interf = *htable_interf_iterator_data_get(&it_interf);
    res = ensure_interface_registration(dst, interf);
    if(res != RES_OK) goto error;
    htable_interf_iterator_next(&it_interf);
  }

  green_function_clear(src);
exit:
  return res;
error:
  goto exit;
}

res_T
green_function_redux_and_clear
  (struct sdis_green_function* dst,
   struct sdis_green_function* greens[],
   const size_t ngreens)
{
  size_t i;
  res_T res = RES_OK;
  ASSERT(dst && greens);

  FOR_EACH(i, 0, ngreens) {
    res = green_function_merge_and_clear(dst, greens[i]);
    if(res != RES_OK) goto error;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
green_function_finalize
  (struct sdis_green_function* green,
   struct ssp_rng_proxy* proxy,
   const struct accum* time)
{
  size_t i, n;
  res_T res = RES_OK;

  if(!green || !proxy || !time) {
    res = RES_BAD_ARG;
    goto error;
  }

  /* Save the RNG state */
  SSP(rng_proxy_get_type(proxy, &green->rng_type));
  res = ssp_rng_proxy_write(proxy, green->rng_state);
  if(res != RES_OK) goto error;

  /* Compute the number of valid/invalid green paths */
  green->npaths_valid = 0;
  n = darray_green_path_size_get(&green->paths);
  FOR_EACH(i, 0, n) {
    const struct green_path* path = darray_green_path_cdata_get(&green->paths)+i;
    green->npaths_valid += (path->end_type != SDIS_GREEN_PATH_END_ERROR);
  }
  green->npaths_invalid = n - green->npaths_valid;

  ASSERT(green->npaths_valid == time->count);
  green->realisation_time = *time;

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
green_path_set_limit_interface_fragment
  (struct green_path_handle* handle,
   struct sdis_interface* interf,
   const struct sdis_interface_fragment* frag,
   const double elapsed_time)
{
  res_T res = RES_OK;
  ASSERT(handle && interf && frag);
  ASSERT(handle->path->end_type == SDIS_GREEN_PATH_END_TYPES_COUNT__);
  res = ensure_interface_registration(handle->green, interf);
  if(res != RES_OK) return res;
  handle->path->elapsed_time = elapsed_time;
  handle->path->limit.fragment = *frag;
  handle->path->limit_id = interface_get_id(interf);
  handle->path->end_type = SDIS_GREEN_PATH_END_AT_INTERFACE;
  return RES_OK;
}

res_T
green_path_set_limit_vertex
  (struct green_path_handle* handle,
   struct sdis_medium* mdm,
   const struct sdis_rwalk_vertex* vert,
   const double elapsed_time)
{
  res_T res = RES_OK;
  ASSERT(handle && mdm && vert);
  ASSERT(handle->path->end_type == SDIS_GREEN_PATH_END_TYPES_COUNT__);
  res = ensure_medium_registration(handle->green, mdm);
  if(res != RES_OK) return res;
  handle->path->elapsed_time = elapsed_time;
  handle->path->limit.vertex = *vert;
  handle->path->limit_id = medium_get_id(mdm);
  handle->path->end_type = SDIS_GREEN_PATH_END_IN_VOLUME;
  return RES_OK;
}

res_T
green_path_set_limit_radiative_ray
  (struct green_path_handle* handle,
   const struct sdis_radiative_ray* ray,
   const double elapsed_time)
{
  ASSERT(handle);
  ASSERT(handle->path->end_type == SDIS_GREEN_PATH_END_TYPES_COUNT__);
  handle->path->elapsed_time = elapsed_time;
  handle->path->limit.ray = *ray;
  handle->path->end_type = SDIS_GREEN_PATH_END_AT_RADIATIVE_ENV;
  return RES_OK;
}

res_T
green_path_reset_limit(struct green_path_handle* handle)
{
  ASSERT(handle);
  handle->path->elapsed_time = -INF;
  handle->path->end_type = SDIS_GREEN_PATH_END_TYPES_COUNT__;
  return RES_OK;
}

res_T
green_path_add_power_term
  (struct green_path_handle* handle,
   struct sdis_medium* mdm,
   const struct sdis_rwalk_vertex* vtx,
   const double val)
{
  struct green_path* path;
  struct power_term* terms;
  size_t nterms;
  size_t iterm;
  unsigned id;
  res_T res = RES_OK;
  ASSERT(handle && mdm && vtx);

  /* Unused position and time: the current implementation of the green function
   * assumes that the power is constant in space and time per medium. */
  (void)vtx;

  res = ensure_medium_registration(handle->green, mdm);
  if(res != RES_OK) goto error;

  path = handle->path;
  terms = darray_power_term_data_get(&path->power_terms);
  nterms = darray_power_term_size_get(&path->power_terms);
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
    terms[iterm].term += val;
  } else {
    struct power_term term = POWER_TERM_NULL__;
    term.term = val;
    term.id = id;
    res = darray_power_term_push_back(&handle->path->power_terms, &term);
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
   const struct sdis_interface_fragment* frag,
   const double val)
{
  struct green_path* path;
  struct flux_term* terms;
  size_t nterms;
  size_t iterm;
  unsigned id;
  res_T res = RES_OK;
  ASSERT(handle && interf && frag && val >= 0);

  res = ensure_interface_registration(handle->green, interf);
  if(res != RES_OK) goto error;

  path = handle->path;
  terms = darray_flux_term_data_get(&path->flux_terms);
  nterms = darray_flux_term_size_get(&path->flux_terms);
  id = interface_get_id(interf);
  iterm = SIZE_MAX;

  /* Early find */
  if(path->ilast_interf < nterms
  && terms[path->ilast_interf].id == id
  && terms[path->ilast_interf].side == frag->side) {
    iterm = path->ilast_interf;
  } else {
    /* Linear search of the submitted interface */
    FOR_EACH(iterm, 0, nterms) {
      if(terms[iterm].id == id && terms[iterm].side == frag->side) {
        break;
      }
    }
  }

  /* Add the flux term to the path wrt the submitted interface */
  if(iterm < nterms) {
    terms[iterm].term += val;
  } else {
    struct flux_term term = FLUX_TERM_NULL__;
    term.term = val;
    term.id = id;
    term.side = frag->side;
    res = darray_flux_term_push_back(&handle->path->flux_terms, &term);
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

res_T
green_path_add_external_flux_terms
  (struct green_path_handle* handle,
   const struct sdis_green_external_flux_terms* terms)
{
  res_T res = RES_OK;
  ASSERT(handle && terms);

  res = darray_extflux_terms_push_back(&handle->path->extflux_terms, terms);
  if(res != RES_OK) {
    log_err(handle->green->scn->dev,
      "%s: cannot store external flux terms -- %s\n",
      FUNC_NAME, res_to_cstr(res));
    goto error;
  }

exit:
  return res;
error:
  goto exit;
}
