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
#include "ssf_microfacet_distribution_c.h"

#include <rsys/double3.h>
#include<rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#include <string.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static INLINE int
check_microfacet_distribution_type
  (const struct ssf_microfacet_distribution_type* type)
{
  return type
      && type->sample
      && type->eval
      && type->pdf
      && IS_POW2(type->alignof_distribution);
}

static void
microfacet_distribution_release(ref_T* ref)
{
  struct ssf_microfacet_distribution* distrib =
    CONTAINER_OF(ref, struct ssf_microfacet_distribution, ref);
  ASSERT(ref);
  if(distrib->data) {
    if(distrib->type.release) distrib->type.release(distrib->data);
    MEM_RM(distrib->allocator, distrib->data);
  }
  MEM_RM(distrib->allocator, distrib);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
ssf_microfacet_distribution_create
  (struct mem_allocator* allocator,
   const struct ssf_microfacet_distribution_type* type,
   struct ssf_microfacet_distribution** out_distrib)
{
  struct mem_allocator* mem_allocator = NULL;
  struct ssf_microfacet_distribution* distrib = NULL;
  res_T res = RES_OK;

  if(!out_distrib || !check_microfacet_distribution_type(type)) {
    res = RES_BAD_ARG;
    goto error;
  }
  mem_allocator = allocator ? allocator : &mem_default_allocator;
  distrib = MEM_CALLOC
    (mem_allocator, 1, sizeof(struct ssf_microfacet_distribution));
  if(!distrib) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&distrib->ref);
  distrib->allocator = mem_allocator;
  distrib->type = *type;

  if(distrib->type.sizeof_distribution) {
    distrib->data = MEM_ALLOC_ALIGNED
      (distrib->allocator,
       distrib->type.sizeof_distribution,
       distrib->type.alignof_distribution);
    if(!distrib->data) {
      res = RES_MEM_ERR;
      goto error;
    }
    memset(distrib->data, 0, distrib->type.sizeof_distribution);
    if(distrib->type.init) {
      res = distrib->type.init(mem_allocator, distrib->data);
      if(res != RES_OK) goto error;
    }
  }

exit:
  if(out_distrib) *out_distrib = distrib;
  return res;
error:
  if(distrib) {
    SSF(microfacet_distribution_ref_put(distrib));
    distrib = NULL;
  }
  goto exit;
}

res_T
ssf_microfacet_distribution_ref_get(struct ssf_microfacet_distribution* distrib)
{
  if(!distrib) return RES_BAD_ARG;
  ref_get(&distrib->ref);
  return RES_OK;
}

res_T
ssf_microfacet_distribution_ref_put(struct ssf_microfacet_distribution* distrib)
{
  if(!distrib) return RES_BAD_ARG;
  ref_put(&distrib->ref, microfacet_distribution_release);
  return RES_OK;
}

void
ssf_microfacet_distribution_sample
  (struct ssf_microfacet_distribution* distrib,
   struct ssp_rng* rng,
   const double N[3],
   double wh[3],
   double* out_pdf)
{
  ASSERT(distrib && rng && N && wh);
  ASSERT(d3_is_normalized(N));
  distrib->type.sample(distrib->data, rng, N, wh, out_pdf);
}

double
ssf_microfacet_distribution_eval
  (struct ssf_microfacet_distribution* distrib,
   const double N[3],
   const double wh[3])
{
  ASSERT(distrib && N && wh);
  ASSERT(d3_is_normalized(N) && d3_is_normalized(wh));
  return distrib->type.eval(distrib->data, N, wh);
}

double
ssf_microfacet_distribution_pdf
  (struct ssf_microfacet_distribution* distrib,
   const double N[3],
   const double wh[3])
{
  ASSERT(distrib && wh );
  ASSERT(d3_is_normalized(N) && d3_is_normalized(wh));
  return distrib->type.pdf(distrib->data, N, wh);
}

res_T
ssf_microfacet_distribution_get_data
  (struct ssf_microfacet_distribution* distrib, void** data)
{
  if(!distrib || !data) return RES_BAD_ARG;
  *data = distrib->data;
  return RES_OK;
}

