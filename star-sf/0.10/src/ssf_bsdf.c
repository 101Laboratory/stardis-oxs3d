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
#include "ssf_bsdf_c.h"

#include <rsys/double3.h>
#include<rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#include <string.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
check_bsdf_type(const struct ssf_bsdf_type* type)
{
  return type
      && type->sample
      && type->eval
      && type->pdf
      && IS_POW2(type->alignof_bsdf);
}

static void
bsdf_release(ref_T* ref)
{
  struct ssf_bsdf* bsdf = CONTAINER_OF(ref, struct ssf_bsdf, ref);
  ASSERT(ref);
  if(bsdf->data) {
    if(bsdf->type.release) bsdf->type.release(bsdf->data);
    MEM_RM(bsdf->allocator, bsdf->data);
  }
  MEM_RM(bsdf->allocator, bsdf);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
ssf_bsdf_create
  (struct mem_allocator* allocator,
   const struct ssf_bsdf_type* type,
   struct ssf_bsdf** out_bsdf)
{
  struct mem_allocator* mem_allocator = NULL;
  struct ssf_bsdf* bsdf = NULL;
  res_T res = RES_OK;

  if(!out_bsdf || !check_bsdf_type(type)) {
    res = RES_BAD_ARG;
    goto error;
  }
  mem_allocator = allocator ? allocator : &mem_default_allocator;
  bsdf = MEM_CALLOC(mem_allocator, 1, sizeof(struct ssf_bsdf));
  if(!bsdf) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&bsdf->ref);
  bsdf->allocator = mem_allocator;
  bsdf->type = *type;

  if(bsdf->type.sizeof_bsdf) {
    bsdf->data = MEM_ALLOC_ALIGNED
      (bsdf->allocator, bsdf->type.sizeof_bsdf, bsdf->type.alignof_bsdf);
    if(!bsdf->data) {
      res = RES_MEM_ERR;
      goto error;
    }
    memset(bsdf->data, 0, bsdf->type.sizeof_bsdf);
    if(bsdf->type.init) {
      res = bsdf->type.init(mem_allocator, bsdf->data);
      if(res != RES_OK) goto error;
    }
  }

exit:
  if(out_bsdf) *out_bsdf = bsdf;
  return res;
error:
  if(bsdf) {
    SSF(bsdf_ref_put(bsdf));
    bsdf = NULL;
  }
  goto exit;
}

res_T
ssf_bsdf_ref_get(struct ssf_bsdf* bsdf)
{
  if(!bsdf) return RES_BAD_ARG;
  ref_get(&bsdf->ref);
  return RES_OK;
}

res_T
ssf_bsdf_ref_put(struct ssf_bsdf* bsdf)
{
  if(!bsdf) return RES_BAD_ARG;
  ref_put(&bsdf->ref, bsdf_release);
  return RES_OK;
}

double
ssf_bsdf_sample
  (struct ssf_bsdf* bsdf,
   struct ssp_rng* rng,
   const double wo[3],
   const double N[3],
   double wi[3],
   int* type,
   double* out_pdf)
{
  double R;
  ASSERT(bsdf && rng && wo && N && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N));
  R = bsdf->type.sample(bsdf->data, rng, wo, N, wi, type, out_pdf);
  return R;
}

double
ssf_bsdf_eval
  (struct ssf_bsdf* bsdf,
   const double wo[3],
   const double N[3],
   const double wi[3])
{
  ASSERT(bsdf && wo && N && wi);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N) && d3_is_normalized(wi));
  return bsdf->type.eval(bsdf->data, wo, N, wi);
}

double
ssf_bsdf_pdf
  (struct ssf_bsdf* bsdf,
   const double wo[3],
   const double N[3],
   const double wi[3])
{
  ASSERT(bsdf && wi && wo);
  ASSERT(d3_is_normalized(wo) && d3_is_normalized(N) && d3_is_normalized(wi));
  return bsdf->type.pdf(bsdf->data, wo, N, wi);
}

res_T
ssf_bsdf_get_data(struct ssf_bsdf* bsdf, void** data)
{
  if(!bsdf || !data) return RES_BAD_ARG;
  *data = bsdf->data;
  return RES_OK;
}

