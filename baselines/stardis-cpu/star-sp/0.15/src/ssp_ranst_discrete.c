/* Copyright (C) 2015-2025 |Méso|Star> (contact@meso-star.com)
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

#include "ssp_rng_c.h"

#include <rsys/dynamic_array_double.h>
#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>

using discrete_dist=RAN_NAMESPACE::discrete_distribution<size_t>;

struct ssp_ranst_discrete {
  struct darray_double pdf;
  ref_T ref;
  struct mem_allocator* allocator;
  discrete_dist* distrib;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
discrete_release(ref_T* ref)
{
  struct ssp_ranst_discrete* ran;
  ASSERT(ref);
  ran = CONTAINER_OF(ref, struct ssp_ranst_discrete, ref);
  if(ran->distrib) {
    ran->distrib->~discrete_dist();
    MEM_RM(ran->allocator, ran->distrib);
  }
  darray_double_release(&ran->pdf);
  MEM_RM(ran->allocator, ran);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
ssp_ranst_discrete_create
  (struct mem_allocator* allocator,
   struct ssp_ranst_discrete** out_ran)
{
  struct ssp_ranst_discrete* ran = nullptr;
  void* mem = nullptr;
  res_T res = RES_OK;

  if(!out_ran) {
    res = RES_BAD_ARG;
    goto error;
  }
  allocator = allocator ? allocator : &mem_default_allocator;

  ran = static_cast<ssp_ranst_discrete*>
    (MEM_CALLOC(allocator, 1, sizeof(ssp_ranst_discrete)));
  if(!ran) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&ran->ref);

  mem = MEM_ALLOC(allocator, sizeof(discrete_dist));
  if(!mem) {
    res = RES_MEM_ERR;
    goto error;
  }
  ran->allocator = allocator;
  ran->distrib = static_cast<discrete_dist*>(new(mem) discrete_dist);
  darray_double_init(allocator, &ran->pdf);

exit:
  if(out_ran) *out_ran = ran;
  return res;
error:
  if(ran) {
    SSP(ranst_discrete_ref_put(ran));
    ran = NULL;
  }
  goto exit;
}

res_T
ssp_ranst_discrete_setup
  (struct ssp_ranst_discrete* ran,
   const double* weights,
   const size_t nweights)
{
  double* pdf;
  double sum = 0;
  size_t i;
  res_T res = RES_OK;

  if(!ran || !weights || !nweights) {
    res = RES_BAD_ARG;
    goto error;
  }

  res = darray_double_resize(&ran->pdf, nweights);
  if(res != RES_OK) goto error;

  pdf = darray_double_data_get(&ran->pdf);

  FOR_EACH(i, 0, nweights) {
    if(weights[i] < 0.f) {
      res = RES_BAD_ARG;
      goto error;
    }
    sum += weights[i];
    pdf[i] = weights[i];
  }
  if(sum == 0) {
    res = RES_BAD_ARG;
    goto error;
  }

  sum = 1 / sum;
  FOR_EACH(i, 0, nweights) {
    pdf[i] *= sum;
  }

  {
    discrete_dist::param_type p{weights, weights + nweights};
    ran->distrib->param(p);
  }

exit:
  return res;
error:
  if(ran) {
    darray_double_clear(&ran->pdf);
  }
  goto exit;
}

res_T
ssp_ranst_discrete_ref_get(struct ssp_ranst_discrete* ran)
{
  if(!ran) return RES_BAD_ARG;
  ref_get(&ran->ref);
  return RES_OK;
}

res_T
ssp_ranst_discrete_ref_put(struct ssp_ranst_discrete* ran)
{
  if(!ran) return RES_BAD_ARG;
  ref_put(&ran->ref, discrete_release);
  return RES_OK;
}

size_t
ssp_ranst_discrete_get(struct ssp_rng* rng, const struct ssp_ranst_discrete* ran)
{
  return wrap_ran(*rng, *ran->distrib);
}

double
ssp_ranst_discrete_pdf(const size_t i, const struct ssp_ranst_discrete* ran)
{
  ASSERT(ran && i < darray_double_size_get(&ran->pdf));
  return darray_double_cdata_get(&ran->pdf)[i];
}

