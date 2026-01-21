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

#include "ssf.h"
#include "ssf_phase_c.h"

#include <rsys/double3.h>
#include <rsys/math.h>
#include<rsys/mem_allocator.h>

#include <string.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
check_phase_type(const struct ssf_phase_type* type)
{
  return type
      && type->sample
      && type->eval
      && type->pdf
      && IS_POW2(type->alignof_phase);
}

static void
phase_release(ref_T* ref)
{
  struct ssf_phase* phase = CONTAINER_OF(ref, struct ssf_phase, ref);
  ASSERT(ref);
  if(phase->data) {
    if(phase->type.release) phase->type.release(phase->data);
    MEM_RM(phase->allocator, phase->data);
  }
  MEM_RM(phase->allocator, phase);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
ssf_phase_create
  (struct mem_allocator* allocator,
   const struct ssf_phase_type* type,
   struct ssf_phase** out_phase)
{
  struct mem_allocator* mem_allocator = NULL;
  struct ssf_phase* phase = NULL;
  res_T res = RES_OK;

  if(!out_phase || !check_phase_type(type)) {
    res = RES_BAD_ARG;
    goto error;
  }
  mem_allocator = allocator ? allocator : &mem_default_allocator;
  phase = MEM_CALLOC(mem_allocator, 1, sizeof(struct ssf_phase));
  if(!phase) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&phase->ref);
  phase->allocator = mem_allocator;
  phase->type = *type;

  if(phase->type.sizeof_phase) {
    phase->data = MEM_ALLOC_ALIGNED
      (phase->allocator, phase->type.sizeof_phase, phase->type.alignof_phase);
    if(!phase->data) {
      res = RES_MEM_ERR;
      goto error;
    }
    memset(phase->data, 0, phase->type.sizeof_phase);
    if(phase->type.init) {
      res = phase->type.init(mem_allocator, phase->data);
      if(res != RES_OK) goto error;
    }
  }

exit:
  if(out_phase) *out_phase = phase;
  return res;
error:
  if(phase) {
    SSF(phase_ref_put(phase));
    phase = NULL;
  }
  goto exit;
}

res_T
ssf_phase_ref_get(struct ssf_phase* phase)
{
  if(!phase) return RES_BAD_ARG;
  ref_get(&phase->ref);
  return RES_OK;
}

res_T
ssf_phase_ref_put(struct ssf_phase* phase)
{
  if(!phase) return RES_BAD_ARG;
  ref_put(&phase->ref, phase_release);
  return RES_OK;
}

void
ssf_phase_sample
  (struct ssf_phase* phase,
   struct ssp_rng* rng,
   const double wo[3],
   double wi[3],
   double* out_pdf)
{
  ASSERT(phase && rng && wo && wi);
  ASSERT(d3_is_normalized(wo));
  phase->type.sample(phase->data, rng, wo, wi, out_pdf);
}

double
ssf_phase_eval
  (struct ssf_phase* phase,
   const double wo[3],
   const double wi[3])
{
  ASSERT(phase && wo && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(wi));
  return phase->type.eval(phase->data, wo, wi);
}

double
ssf_phase_pdf
  (struct ssf_phase* phase,
   const double wo[3],
   const double wi[3])
{
  ASSERT(phase && wi && wo);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(wi));
  return phase->type.pdf(phase->data, wo, wi);
}

res_T
ssf_phase_get_data(struct ssf_phase* phase, void** data)
{
  if(!phase || !data) return RES_BAD_ARG;
  *data = phase->data;
  return RES_OK;
}

