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

using normal_dist = RAN_NAMESPACE::normal_distribution<double>;

using normal_dist_float = RAN_NAMESPACE::normal_distribution<float>;

struct ssp_ranst_gaussian {
  ref_T ref;
  struct mem_allocator* allocator;
  normal_dist* distrib;
  double mu;
  double K1; /* 1.0 / sigma */
  double K2; /* 1.0 / (sigma * SQRT_2_PI) */
};

struct ssp_ranst_gaussian_float {
  ref_T ref;
  struct mem_allocator* allocator;
  normal_dist_float* distrib;
  float mu;
  float K1; /* 1.0 / sigma */
  float K2; /* 1.0 / (sigma * SQRT_2_PI) */
};

/*******************************************************************************
 * Helper function
 ******************************************************************************/
static void
gaussian_release(ref_T* ref)
{
  ssp_ranst_gaussian* ran;
  ASSERT(ref);
  ran = CONTAINER_OF(ref, ssp_ranst_gaussian, ref);
  if(ran->distrib) {
    ran->distrib->~normal_dist();
    MEM_RM(ran->allocator, ran->distrib);
  }
  MEM_RM(ran->allocator, ran);
}

static void
gaussian_float_release(ref_T* ref)
{
  ssp_ranst_gaussian_float* ran;
  ASSERT(ref);
  ran = CONTAINER_OF(ref, ssp_ranst_gaussian_float, ref);
  if(ran->distrib) {
    ran->distrib->~normal_dist_float();
    MEM_RM(ran->allocator, ran->distrib);
  }
  MEM_RM(ran->allocator, ran);
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
ssp_ranst_gaussian_create
  (struct mem_allocator* allocator,
   struct ssp_ranst_gaussian** out_ran)
{
  ssp_ranst_gaussian* ran = nullptr;
  void* mem = nullptr;
  res_T res = RES_OK;

  if(!out_ran)
    return RES_BAD_ARG;

  allocator = allocator ? allocator : &mem_default_allocator;

  ran = static_cast<ssp_ranst_gaussian*>
    (MEM_CALLOC(allocator, 1, sizeof(struct ssp_ranst_gaussian)));
  if(!ran) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&ran->ref);

  mem = MEM_ALLOC(allocator, sizeof(normal_dist));
  if(!mem) {
    res = RES_MEM_ERR;
    goto error;
  }
  ran->allocator = allocator;
  ran->distrib = static_cast<normal_dist*>(new(mem) normal_dist);
  ran->K1 = -1; /* invalid */
  if(out_ran) *out_ran = ran;

exit:
  return res;
error:
  if(ran) {
    SSP(ranst_gaussian_ref_put(ran));
    ran = nullptr;
  }
  goto exit;
}

res_T
ssp_ranst_gaussian_float_create
  (struct mem_allocator* allocator,
   struct ssp_ranst_gaussian_float** out_ran)
{
  ssp_ranst_gaussian_float* ran = nullptr;
  void* mem = nullptr;
  res_T res = RES_OK;

  if(!out_ran)
    return RES_BAD_ARG;

  allocator = allocator ? allocator : &mem_default_allocator;

  ran = static_cast<ssp_ranst_gaussian_float*>
    (MEM_CALLOC(allocator, 1, sizeof(struct ssp_ranst_gaussian_float)));
  if(!ran) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&ran->ref);

  mem = MEM_ALLOC(allocator, sizeof(normal_dist_float));
  if(!mem) {
    res = RES_MEM_ERR;
    goto error;
  }
  ran->allocator = allocator;
  ran->distrib = static_cast<normal_dist_float*>(new(mem) normal_dist_float);
  ran->K1 = -1; /* invalid */
  if (out_ran) *out_ran = ran;

exit:
  return res;
error:
  if(ran) {
    SSP(ranst_gaussian_float_ref_put(ran));
    ran = nullptr;
  }
  goto exit;
}

res_T
ssp_ranst_gaussian_setup
  (struct ssp_ranst_gaussian* ran,
   const double mu,
   const double sigma)
{
  if(!ran || sigma < 0)
    return RES_BAD_ARG;

  normal_dist::param_type p{mu, sigma};
  ran->distrib->param(p);
  ran->mu = mu;
  ran->K1 = 1 / sigma;
  ran->K2 = 1 / (sigma * SQRT_2_PI);

  return RES_OK;
}

res_T
ssp_ranst_gaussian_float_setup
  (struct ssp_ranst_gaussian_float* ran,
   const float mu,
   const float sigma)
{
  if(!ran || sigma < 0)
    return RES_BAD_ARG;

  normal_dist_float::param_type p{ mu, sigma };
  ran->distrib->param(p);
  ran->mu = mu;
  ran->K1 = 1 / sigma;
  ran->K2 = 1 / (sigma * (float)SQRT_2_PI);

  return RES_OK;
}

res_T
ssp_ranst_gaussian_ref_get
  (struct ssp_ranst_gaussian* ran)
{
  if(!ran) return RES_BAD_ARG;
  ref_get(&ran->ref);
  return RES_OK;
}

res_T
ssp_ranst_gaussian_float_ref_get
  (struct ssp_ranst_gaussian_float* ran)
{
  if(!ran) return RES_BAD_ARG;
  ref_get(&ran->ref);
  return RES_OK;
}

res_T
ssp_ranst_gaussian_ref_put
  (struct ssp_ranst_gaussian* ran)
{
  if(!ran) return RES_BAD_ARG;
  ref_put(&ran->ref, gaussian_release);
  return RES_OK;
}

res_T
ssp_ranst_gaussian_float_ref_put
  (struct ssp_ranst_gaussian_float* ran)
{
  if(!ran) return RES_BAD_ARG;
  ref_put(&ran->ref, gaussian_float_release);
  return RES_OK;
}

double
ssp_ranst_gaussian_get
  (const struct ssp_ranst_gaussian* ran, struct ssp_rng* rng)
{
  ASSERT(ran->K1 > 0);
  return wrap_ran(*rng, *ran->distrib);
}

float
ssp_ranst_gaussian_float_get
  (const struct ssp_ranst_gaussian_float* ran, struct ssp_rng* rng)
{
  ASSERT(ran->K1 > 0);
  return wrap_ran(*rng, *ran->distrib);
}

double
ssp_ranst_gaussian_pdf
  (const struct ssp_ranst_gaussian* ran, const double x)
{
  const double tmp = (x - ran->mu) * ran->K1;
  ASSERT(ran->K1 > 0);
  return ran->K2 * exp(-0.5 * tmp * tmp);
}

float
ssp_ranst_gaussian_float_pdf
  (const struct ssp_ranst_gaussian_float* ran, const float x)
{
  const float tmp = (x - ran->mu) * ran->K1;
  ASSERT(ran->K1 > 0);
  return ran->K2 * expf(-0.5f * tmp * tmp);
}

