/* Copyright (C) 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

#include "scmap.h"

#include <rsys/cstr.h>
#include <rsys/dynamic_array_double.h>
#include <rsys/logger.h>
#include<rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#define MSG_INFO_PREFIX "Star-CMap:\x1b[1m\x1b[32minfo\x1b[0m: "
#define MSG_ERROR_PREFIX "Star-CMap:\x1b[1m\x1b[31merror\x1b[0m: "
#define MSG_WARNING_PREFIX "Star-CMap:\x1b[1m\x1b[33mwarning\x1b[0m: "

struct scmap {
  struct darray_double palette; /* List of color in [0, 1]^3 */

  int verbose;
  struct logger* logger;
  struct logger logger__;
  struct mem_allocator* allocator;
  ref_T ref;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
print_info(const char* msg, void* ctx)
{
  (void)ctx;
  fprintf(stderr, MSG_INFO_PREFIX"%s", msg);
}

static void
print_err(const char* msg, void* ctx)
{
  (void)ctx;
  fprintf(stderr, MSG_ERROR_PREFIX"%s", msg);
}

static void
print_warn(const char* msg, void* ctx)
{
  (void)ctx;
  fprintf(stderr, MSG_WARNING_PREFIX"%s", msg);
}

static res_T
setup_default_logger(struct mem_allocator* allocator, struct logger* logger)
{
  res_T res = RES_OK;
  ASSERT(logger);
  res = logger_init(allocator, logger);
  if(res != RES_OK) return res;
  logger_set_stream(logger, LOG_OUTPUT, print_info, NULL);
  logger_set_stream(logger, LOG_ERROR, print_err, NULL);
  logger_set_stream(logger, LOG_WARNING, print_warn, NULL);
  return RES_OK;
}

static INLINE void
log_msg
  (const struct scmap* scmap,
   const enum log_type stream,
   const char* msg,
   va_list vargs)
{
  ASSERT(scmap && msg);
  if(scmap->verbose) {
    res_T res; (void)res;
    res = logger_vprint(scmap->logger, stream, msg, vargs);
    ASSERT(res == RES_OK);
  }
}

static INLINE void
log_err
  (const struct scmap* scmap,
   const char* msg, ...)
#ifdef COMPILER_GCC
  __attribute((format(printf, 2, 3)))
#endif
;

static INLINE void
log_warn
  (const struct scmap* scmap,
   const char* msg, ...)
#ifdef COMPILER_GCC
    __attribute((format(printf, 2, 3)))
#endif
;

void
log_err(const struct scmap* scmap, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(scmap && msg);

  va_start(vargs_list, msg);
  log_msg(scmap, LOG_ERROR, msg, vargs_list);
  va_end(vargs_list);
}

void
log_warn(const struct scmap* scmap, const char* msg, ...)
{
  va_list vargs_list;
  ASSERT(scmap && msg);

  va_start(vargs_list, msg);
  log_msg(scmap, LOG_WARNING, msg, vargs_list);

  va_end(vargs_list);
}

static FINLINE int
check_palette(const struct scmap_palette* palette)
{
  return palette && palette->get_color && palette->ncolors;
}

static INLINE res_T
setup_palette(struct scmap* scmap, const struct scmap_palette* palette)
{
  size_t i;
  res_T res = RES_OK;
  ASSERT(scmap && check_palette(palette));

  res = darray_double_resize(&scmap->palette, palette->ncolors*3);
  if(res != RES_OK) {
    log_err(scmap, "Could not allocate the palette -- %s.\n",
      res_to_cstr(res));
    goto error;
  }

  FOR_EACH(i, 0, palette->ncolors) {
    double color[3];

    palette->get_color(i, color, palette->context);

    if(color[0] < 0 || color[0] > 1
    || color[1] < 0 || color[1] > 1
    || color[2] < 0 || color[2] > 1) {
      log_err(scmap,
        "Invalid color {%g, %g, %g}. Each channel must be in [0, 1].\n",
        SPLIT3(color));
      res = RES_BAD_ARG;
      goto error;
    }

    darray_double_data_get(&scmap->palette)[i*3+0] = color[0];
    darray_double_data_get(&scmap->palette)[i*3+1] = color[1];
    darray_double_data_get(&scmap->palette)[i*3+2] = color[2];
  }

exit:
  return res;
error:
  darray_double_clear(&scmap->palette);
  goto exit;
}

static INLINE void
fetch_color_nearest
  (const struct scmap* scmap,
   const size_t icol,
   const double u,
   double color[3])
{
  const size_t i = u < 0.5 ? icol*3 : (icol + 1)*3;
  ASSERT(scmap && color && u >= 0 && u <1);
  ASSERT(i/3 < darray_double_size_get(&scmap->palette)/3);
  color[0] = darray_double_cdata_get(&scmap->palette)[i+0];
  color[1] = darray_double_cdata_get(&scmap->palette)[i+1];
  color[2] = darray_double_cdata_get(&scmap->palette)[i+2];
}

static INLINE void
fetch_color_linear
  (const struct scmap* scmap,
   const size_t icol,
   const double u,
   double color[3])
{
  const size_t i = icol*3;
  ASSERT(scmap && color && u >= 0 && u <1);
  ASSERT(i/3 < darray_double_size_get(&scmap->palette)/3);

  if(u == 0) {
    color[0] = darray_double_cdata_get(&scmap->palette)[i+0];
    color[1] = darray_double_cdata_get(&scmap->palette)[i+1];
    color[2] = darray_double_cdata_get(&scmap->palette)[i+2];
  } else {
    const size_t j = (icol+1)*3;
    const double* col0;
    const double* col1;
    ASSERT(j/3 < darray_double_size_get(&scmap->palette)/3);

    col0 = darray_double_cdata_get(&scmap->palette) + i;
    col1 = darray_double_cdata_get(&scmap->palette) + j;

    color[0] = u * (col1[0] - col0[0]) + col0[0];
    color[1] = u * (col1[1] - col0[1]) + col0[1];
    color[2] = u * (col1[2] - col0[2]) + col0[2];
  }
}

static void
release_scmap(ref_T* ref)
{
  struct scmap* scmap = CONTAINER_OF(ref, struct scmap, ref);
  ASSERT(ref);
  darray_double_release(&scmap->palette);
  if(scmap->logger == &scmap->logger__) logger_release(&scmap->logger__);
  MEM_RM(scmap->allocator, scmap);
}

/*******************************************************************************
 * Exported symbols
 ******************************************************************************/
res_T
scmap_create
  (struct logger* logger, /* NULL <=> use builtin logger */
   struct mem_allocator* mem_allocator, /* NULL <=> use default allocator */
   const int verbose, /* Verbosity level */
   const struct scmap_palette* palette,
   struct scmap** out_scmap)
{
  struct scmap* scmap = NULL;
  struct mem_allocator* allocator = NULL;
  res_T res = RES_OK;

  if(!out_scmap || !check_palette(palette)) {
    res = RES_BAD_ARG;
    goto error;
  }

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  scmap = MEM_CALLOC(allocator, 1, sizeof(*scmap));
  if(!scmap) {
    if(verbose) {
      #define ERR_STR "Could not allocate the Star-ColorMap.\n"
      if(logger) {
        logger_print(logger, LOG_ERROR, ERR_STR);
      } else {
        fprintf(stderr, MSG_ERROR_PREFIX ERR_STR);
      }
      #undef ERR_STR
    }
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&scmap->ref);
  scmap->allocator = allocator;
  scmap->verbose = verbose;
  darray_double_init(scmap->allocator, &scmap->palette);

  if(logger) {
    scmap->logger = logger;
  } else {
    res = setup_default_logger(scmap->allocator, &scmap->logger__);
    if(res != RES_OK) {
      if(verbose) {
        fprintf(stderr, MSG_ERROR_PREFIX
          "%s: could not setup the Star-ColorMap logger -- %s.\n",
          FUNC_NAME, res_to_cstr(res));
      }
      goto error;
    }
    scmap->logger = &scmap->logger__;
  }

  res = setup_palette(scmap, palette);
  if(res != RES_OK) goto error;

exit:
  if(out_scmap) *out_scmap = scmap;
  return res;
error:
  if(scmap) {
    SCMAP(ref_put(scmap));
    scmap = NULL;
  }
  goto exit;
}

res_T
scmap_ref_get(struct scmap* scmap)
{
  if(!scmap) return RES_BAD_ARG;
  ref_get(&scmap->ref);
  return RES_OK;
}

res_T
scmap_ref_put(struct scmap* scmap)
{
  if(!scmap) return RES_BAD_ARG;
  ref_put(&scmap->ref, release_scmap);
  return RES_OK;
}

res_T
scmap_fetch_color
  (const struct scmap* scmap,
   const double value,
   const enum scmap_filter filter,
   double color[3])
{
  size_t ncolors;
  size_t icol;
  double cell;
  double icell;
  double u;
  res_T res = RES_OK;

  if(!scmap || !color || (unsigned)filter >= SCMAP_FILTERS_COUNT__) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(value < 0 || value > 1) {
    log_err(scmap, "%s: the submitted value must be in [0, 1] -- value = %g.\n",
      FUNC_NAME, value);
    res = RES_BAD_ARG;
    goto error;
  }

  ncolors = darray_double_size_get(&scmap->palette)/3;
  cell = value * (double)(ncolors-1);

  u = modf(cell, &icell);
  icol = (size_t)icell;
  ASSERT(icol < ncolors);

  switch(filter) {
    case SCMAP_FILTER_NEAREST:
      fetch_color_nearest(scmap, icol, u, color);
      break;
    case SCMAP_FILTER_LINEAR:
      fetch_color_linear(scmap, icol, u, color);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

exit:
  return res;
error:
  goto exit;
}

