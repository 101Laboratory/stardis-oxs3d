/* Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#include "scam_c.h"
#include "scam_log.h"

#include <rsys/logger.h>
#include<rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
check_sample
  (const struct scam* cam,
   const struct scam_sample* sample)
{
  ASSERT(cam);
  if(!sample) return 0;

  if(sample->film[0] < 0 || sample->film[0] >= 1
  || sample->film[1] < 0 || sample->film[1] >= 1) {
    log_err(cam, 
      "Invalid camera film sample (%g, %g). It must be in [0, 1[^2.\n",
      SPLIT2(sample->film));
    return 0;
  }

  if(sample->lens[0] < 0 || sample->lens[0] >= 1
  || sample->lens[1] < 0 || sample->lens[1] >= 1) {
    log_err(cam, 
      "Invalid camera lens sample (%g, %g). It must be in [0, 1[^2.\n",
      SPLIT2(sample->lens));
    return 0;
  }
  return 1;
}

static void
release_scam(ref_T* ref)
{
  struct scam* cam = CONTAINER_OF(ref, struct scam, ref);
  ASSERT(ref);
  if(cam->logger == &cam->logger__) logger_release(&cam->logger__);
  MEM_RM(cam->allocator, cam);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
scam_generate_ray
  (const struct scam* cam,
   const struct scam_sample* sample,
   struct scam_ray* ray)
{
  res_T res = RES_OK;

  if(!cam || !ray || !check_sample(cam, sample)) {
    res = RES_BAD_ARG;
    goto error;
  }

  switch(cam->type) {
    case SCAM_ORTHOGRAPHIC:
      orthographic_generate_ray(cam, sample, ray);
      break;
    case SCAM_PERSPECTIVE:
      perspective_generate_ray(cam, sample, ray);
      break;
    default: FATAL("Unreachable code.\n"); break;
  }

exit:
  return res;
error:
  goto exit;
}

res_T
scam_get_type(const struct scam* cam, enum scam_type* type)
{
  if(!cam || !type) return RES_BAD_ARG;
  *type = cam->type;
  return RES_OK;
}

res_T
scam_ref_get(struct scam* cam)
{
  if(!cam) return RES_BAD_ARG;
  ref_get(&cam->ref);
  return RES_OK;
}

res_T
scam_ref_put(struct scam* cam)
{
  if(!cam) return RES_BAD_ARG;
  ref_put(&cam->ref, release_scam);
  return RES_OK;
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
camera_create
  (struct logger* logger,
   struct mem_allocator* mem_allocator,
   const int verbose, /* Verbosity level */
   const enum scam_type type,
   struct scam** out_cam)
{
  struct scam* cam = NULL;
  struct mem_allocator* allocator = NULL;
  res_T res = RES_OK;
  ASSERT(out_cam && (unsigned)type < SCAM_TYPES_COUNT__);

  allocator = mem_allocator ? mem_allocator : &mem_default_allocator;
  cam = MEM_CALLOC(allocator, 1, sizeof(*cam));
  if(!cam) {
    if(verbose) {
      #define ERR_STR "Could not allocate the Star-Camera.\n"
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
  ref_init(&cam->ref);
  cam->allocator = allocator;
  cam->type = type;
  cam->verbose = verbose;

  if(logger) {
    cam->logger = logger;
  } else {
    setup_log_default(cam);
  }

exit:
  if(out_cam) *out_cam = cam;
  return res;
error:
  if(cam) {
    SCAM(ref_put(cam));
    cam = NULL;
  }
  goto exit;
}

