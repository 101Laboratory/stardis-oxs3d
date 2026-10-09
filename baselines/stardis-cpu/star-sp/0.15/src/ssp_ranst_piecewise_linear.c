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

#include <rsys/mem_allocator.h>
#include <rsys/ref_count.h>

#include <algorithm>

using piecewise_dist = RAN_NAMESPACE::piecewise_linear_distribution<double>;

using piecewise_dist_float = RAN_NAMESPACE::piecewise_linear_distribution<float>;

struct ssp_ranst_piecewise_linear {
  ref_T ref;
  struct mem_allocator* allocator;
  piecewise_dist* distrib;
};

struct ssp_ranst_piecewise_linear_float {
  ref_T ref;
  struct mem_allocator* allocator;
  piecewise_dist_float* distrib;
};

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static void
piecewise_release(ref_T* ref)
{
  ssp_ranst_piecewise_linear* ran;
  ASSERT(ref);
  ran = CONTAINER_OF(ref, ssp_ranst_piecewise_linear, ref);
  if(ran->distrib) {
    ran->distrib->~piecewise_dist();
    MEM_RM(ran->allocator, ran->distrib);
  }
  MEM_RM(ran->allocator, ran);
}

static void
piecewise_float_release(ref_T* ref)
{
  ssp_ranst_piecewise_linear_float* ran;
  ASSERT(ref);
  ran = CONTAINER_OF(ref, ssp_ranst_piecewise_linear_float, ref);
  if(ran->distrib) {
    ran->distrib->~piecewise_dist_float();
    MEM_RM(ran->allocator, ran->distrib);
  }
  MEM_RM(ran->allocator, ran);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
ssp_ranst_piecewise_linear_create
  (struct mem_allocator* allocator,
   struct ssp_ranst_piecewise_linear** out_ran)
{
  struct ssp_ranst_piecewise_linear* ran = nullptr;
  void* mem = nullptr;
  res_T res = RES_OK;

  if(!out_ran) {
    res = RES_BAD_ARG;
    goto error;
  }

  allocator = allocator ? allocator : &mem_default_allocator;

  ran = static_cast<ssp_ranst_piecewise_linear*>
    (MEM_CALLOC(allocator, 1, sizeof(ssp_ranst_piecewise_linear)));
  if(!ran) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&ran->ref);

  mem = MEM_ALLOC(allocator, sizeof(piecewise_dist));
  if(!mem) {
    res = RES_MEM_ERR;
    goto error;
  }
  ran->allocator = allocator;
  ran->distrib = static_cast<piecewise_dist*>(new(mem) piecewise_dist);
  if(out_ran) *out_ran = ran;

exit:
  return res;
error:
  if(ran) {
    SSP(ranst_piecewise_linear_ref_put(ran));
    ran = nullptr;
  }
  goto exit;
}

res_T
ssp_ranst_piecewise_linear_float_create
  (struct mem_allocator* allocator,
   struct ssp_ranst_piecewise_linear_float** out_ran)
{
  struct ssp_ranst_piecewise_linear_float* ran = nullptr;
  void* mem = nullptr;
  res_T res = RES_OK;

  if(!out_ran) {
    res = RES_BAD_ARG;
    goto error;
  }

  allocator = allocator ? allocator : &mem_default_allocator;

  ran = static_cast<ssp_ranst_piecewise_linear_float*>
    (MEM_CALLOC(allocator, 1, sizeof(ssp_ranst_piecewise_linear_float)));
  if(!ran) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&ran->ref);

  mem = MEM_ALLOC(allocator, sizeof(piecewise_dist_float));
  if(!mem) {
    res = RES_MEM_ERR;
    goto error;
  }
  ran->allocator = allocator;
  ran->distrib
    = static_cast<piecewise_dist_float*>(new(mem) piecewise_dist_float);
  if (out_ran) *out_ran = ran;

exit:
  return res;
error:
  if(ran) {
    SSP(ranst_piecewise_linear_float_ref_put(ran));
    ran = nullptr;
  }
  goto exit;
}

res_T
ssp_ranst_piecewise_linear_setup
  (struct ssp_ranst_piecewise_linear* ran,
   const double* intervals,
   const double* weights,
   const size_t size)
{
  size_t i;
  if(!ran || !intervals || !weights || size < 2)
    return RES_BAD_ARG;

  /* Checking param validity to avoid an assert when using ran */
  for(i=0; i < size-1; i++) {
    if(weights[i] < 0) return RES_BAD_ARG;
    if(intervals[i+1] <= intervals[i]) return RES_BAD_ARG;
  }
  if (intervals[size-1] - intervals[size-2] <= 0) return RES_BAD_ARG;
  piecewise_dist::param_type p{intervals, intervals + size, weights};
  ran->distrib->param(p);
  return RES_OK;
}

res_T
ssp_ranst_piecewise_linear_float_setup
  (struct ssp_ranst_piecewise_linear_float* ran,
   const float* intervals,
   const float* weights,
   const size_t size)
{
  size_t i;
  if(!ran || !intervals || !weights || size < 2)
    return RES_BAD_ARG;

  /* Checking param validity to avoid an assert when using ran */
  for(i=0; i < size-1; i++) {
    if(weights[i] < 0) return RES_BAD_ARG;
    if(intervals[i+1] <= intervals[i]) return RES_BAD_ARG;
  }
  if (weights[size-1] < 0) return RES_BAD_ARG;
  piecewise_dist_float::param_type p{ intervals, intervals + size, weights };
  ran->distrib->param(p);
  return RES_OK;
}

res_T
ssp_ranst_piecewise_linear_ref_get
  (struct ssp_ranst_piecewise_linear* ran)
{
  if(!ran) return RES_BAD_ARG;
  ref_get(&ran->ref);
  return RES_OK;
}

res_T
ssp_ranst_piecewise_linear_float_ref_get
  (struct ssp_ranst_piecewise_linear_float* ran)
{
  if(!ran) return RES_BAD_ARG;
  ref_get(&ran->ref);
  return RES_OK;
}

res_T
ssp_ranst_piecewise_linear_ref_put
  (struct ssp_ranst_piecewise_linear* ran)
{
  if(!ran) return RES_BAD_ARG;
  ref_put(&ran->ref, piecewise_release);
  return RES_OK;
}

res_T
ssp_ranst_piecewise_linear_float_ref_put
  (struct ssp_ranst_piecewise_linear_float* ran)
{
  if(!ran) return RES_BAD_ARG;
  ref_put(&ran->ref, piecewise_float_release);
  return RES_OK;
}

double
ssp_ranst_piecewise_linear_get
  (const struct ssp_ranst_piecewise_linear* ran,
   struct ssp_rng* rng)
{
  return wrap_ran(*rng, *ran->distrib);
}

float
ssp_ranst_piecewise_linear_float_get
  (const struct ssp_ranst_piecewise_linear_float* ran,
  struct ssp_rng* rng)
{
  return wrap_ran(*rng, *ran->distrib);
}

double
ssp_ranst_piecewise_linear_pdf
  (const struct ssp_ranst_piecewise_linear *ran,
   double x)
{
  ASSERT(ran);
  if(x<ran->distrib->min() || x>ran->distrib->max())
    return 0;

  const auto& inter = ran->distrib->intervals();
  const auto& dens = ran->distrib->densities();
  auto b = std::lower_bound(inter.begin(), inter.end(), x);
  size_t idx = b - inter.begin();
  if (x == *b) return dens[idx];
  idx--;
  ASSERT(idx < inter.size() - 1);
  return (dens[idx+1] * (x - inter[idx]) + dens[idx] * (inter[idx+1] - x))
    / (inter[idx+1]- inter[idx]);
}

float
ssp_ranst_piecewise_linear_float_pdf
  (const struct ssp_ranst_piecewise_linear_float *ran,
   float x)
{
  ASSERT(ran);
  if (x<ran->distrib->min() || x>ran->distrib->max())
    return 0;

  const auto& inter = ran->distrib->intervals();
  /* std::piecewise_linear_distribution<float>::densities()
   * should be a std::vector<float>, but is a std::vector<double> with gcc
   * => use explicit casts */
  const auto& dens = ran->distrib->densities();
  auto b = std::lower_bound(inter.begin(), inter.end(), x);
  size_t idx = b - inter.begin();
  if (x == *b) return (float)dens[idx];
  idx--;
  ASSERT(idx < inter.size() - 1);
  return
    ((float)dens[idx+1] * (x-inter[idx]) + (float)dens[idx] * (inter[idx+1]-x))
      / (inter[idx+1] - inter[idx]);
}
