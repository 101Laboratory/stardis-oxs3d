/* Copyright (C) 2016-2018, 2021-2025 |Méso|Star> (contact@meso-star.com)
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

#include "ssf_fresnel_c.h"

#include <rsys/math.h>
#include<rsys/mem_allocator.h>

#include <string.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
check_fresnel_type(const struct ssf_fresnel_type* type)
{
  return type
      && type->eval
      && IS_POW2(type->alignof_fresnel);
}

static void
fresnel_release(ref_T* ref)
{
  struct ssf_fresnel* fresnel = CONTAINER_OF(ref, struct ssf_fresnel, ref);
  ASSERT(ref);
  if(fresnel->data) {
    if(fresnel->type.release) fresnel->type.release(fresnel->data);
    MEM_RM(fresnel->allocator, fresnel->data);
  }
  MEM_RM(fresnel->allocator, fresnel);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
ssf_fresnel_create
  (struct mem_allocator* allocator,
   const struct ssf_fresnel_type* type,
   struct ssf_fresnel** out_fresnel)
{
  struct mem_allocator* mem_allocator;
  struct ssf_fresnel* fresnel = NULL;
  res_T res = RES_OK;

  if(!out_fresnel || !check_fresnel_type(type)) {
    res = RES_BAD_ARG;
    goto error;
  }

  mem_allocator = allocator ? allocator : &mem_default_allocator;
  fresnel = MEM_CALLOC(mem_allocator, 1, sizeof(struct ssf_fresnel));
  if(!fresnel) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&fresnel->ref);
  fresnel->allocator = mem_allocator;
  fresnel->type = *type;

  if(fresnel->type.sizeof_fresnel) {
    fresnel->data = MEM_ALLOC_ALIGNED(fresnel->allocator,
      fresnel->type.sizeof_fresnel, fresnel->type.alignof_fresnel);
    if(!fresnel->data) {
      res = RES_MEM_ERR;
      goto error;
    }
    memset(fresnel->data, 0, fresnel->type.sizeof_fresnel);
    if(fresnel->type.init) {
      res = fresnel->type.init(fresnel->allocator, fresnel->data);
      if(res != RES_OK) goto error;
    }
  }

exit:
  if(out_fresnel) *out_fresnel = fresnel;
  return res;
error:
  if(fresnel) {
    SSF(fresnel_ref_put(fresnel));
    fresnel = NULL;
  }
  goto exit;
}

res_T
ssf_fresnel_ref_get(struct ssf_fresnel* fresnel)
{
  if(!fresnel) return RES_BAD_ARG;
  ref_get(&fresnel->ref);
  return RES_OK;
}

res_T
ssf_fresnel_ref_put(struct ssf_fresnel* fresnel)
{
  if(!fresnel) return RES_BAD_ARG;
  ref_put(&fresnel->ref, fresnel_release);
  return RES_OK;
}

double
ssf_fresnel_eval(struct ssf_fresnel* fresnel, const double cos_theta)
{
  ASSERT(fresnel && cos_theta >= 0);
  return fresnel->type.eval(fresnel->data, cos_theta);
}

res_T
ssf_fresnel_get_data(struct ssf_fresnel* fresnel, void** data)
{
  if(!fresnel || !data) return RES_BAD_ARG;
  *data = fresnel->data;
  return RES_OK;
}

