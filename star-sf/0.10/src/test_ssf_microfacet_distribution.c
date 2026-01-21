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
#include "test_ssf_utils.h"

#include <rsys/double3.h>

static int ufacet_is_init = 0;

struct ALIGN(64) ufacet {
  uint32_t id;
  struct ssp_rng* rng;
  double wh[3];
  double N[3];
  double pdf;
  double value;
};

static res_T
ufacet_init(struct mem_allocator* allocator, void* distrib)
{
  CHK(allocator != NULL);
  CHK(distrib != NULL);
  CHK(IS_ALIGNED(distrib, 64) == 1);
  ((struct ufacet*)distrib)->id = 0xDECAFBAD;
  ufacet_is_init = 1;
  return RES_OK;
}

static void
ufacet_release(void* distrib)
{
  CHK(distrib != NULL);
  CHK(((struct ufacet*)distrib)->id == 0xDECAFBAD);
  ufacet_is_init = 0;
}

static void
ufacet_sample
  (void* distrib,
   struct ssp_rng* rng,
   const double N[3],
   double wh[3],
   double* pdf)
{
  struct ufacet* ufacet = distrib;
  CHK(ufacet != NULL);
  CHK(N != NULL);
  CHK(wh != NULL);
  CHK(ufacet->rng == rng);
  CHK(d3_eq(ufacet->N, N) == 1);
  d3_normalize(wh, d3(wh, 1.0, 2.0, 3.0));
  if(pdf) *pdf = ufacet->pdf;
}

static double
ufacet_eval
  (void* distrib,
   const double N[3],
   const double wh[3])
{
  struct ufacet* ufacet = distrib;
  CHK(distrib != NULL);
  CHK(N != NULL);
  CHK(wh != NULL);
  CHK(d3_eq(ufacet->wh, wh) == 1);
  CHK(d3_eq(ufacet->N, N) == 1);
  return ufacet->value;
}

static double
ufacet_pdf
  (void* distrib,
   const double N[3],
   const double wh[3])
{
  struct ufacet* ufacet = distrib;
  CHK(N != NULL);
  CHK(wh != NULL);
  CHK(d3_eq(ufacet->wh, wh) == 1);
  CHK(d3_eq(ufacet->N, N) == 1);
  return ufacet->pdf;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct ufacet* data;
  struct ssf_microfacet_distribution_type type =
    SSF_MICROFACET_DISTRIBUTION_TYPE_NULL;
  struct ssf_microfacet_distribution_type type2 = microfacet_dummy;
  struct ssf_microfacet_distribution* distrib;
  struct ssp_rng* rng;
  double N[3], wh[3], pdf;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng) == RES_OK);

  type.init = ufacet_init;
  type.release = ufacet_release;
  type.sample = ufacet_sample;
  type.eval = ufacet_eval;
  type.pdf = ufacet_pdf;
  type.sizeof_distribution = sizeof(struct ufacet);
  type.alignof_distribution = ALIGNOF(struct ufacet);

  #define CREATE ssf_microfacet_distribution_create
  CHK(CREATE(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(&allocator, NULL, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, &type, NULL) == RES_BAD_ARG);
  CHK(CREATE(&allocator, &type, NULL) == RES_BAD_ARG);
  CHK(CREATE(NULL, NULL, &distrib) == RES_BAD_ARG);
  CHK(CREATE(&allocator, NULL, &distrib) == RES_BAD_ARG);

  CHK(ufacet_is_init == 0);
  CHK(CREATE(NULL, &type, &distrib) == RES_OK);
  CHK(ufacet_is_init == 1);

  CHK(ssf_microfacet_distribution_ref_get(NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_distribution_ref_get(distrib) == RES_OK);
  CHK(ssf_microfacet_distribution_ref_put(NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_distribution_ref_put(distrib) == RES_OK);
  CHK(ufacet_is_init == 1);
  CHK(ssf_microfacet_distribution_ref_put(distrib) == RES_OK);
  CHK(ufacet_is_init == 0);

  CHK(CREATE(&allocator, &type, &distrib) == RES_OK);
  CHK(ufacet_is_init == 1);
  CHK(ssf_microfacet_distribution_ref_put(distrib) == RES_OK);
  CHK(ufacet_is_init == 0);

  type2.init = NULL;
  CHK(CREATE(&allocator, &type2, &distrib) == RES_OK);
  CHK(ssf_microfacet_distribution_ref_put(distrib) == RES_OK);
  type2.init = microfacet_dummy.init;
  type2.release = NULL;
  CHK(CREATE(&allocator, &type2, &distrib) == RES_OK);
  CHK(ssf_microfacet_distribution_ref_put(distrib) == RES_OK);

  type.sample = NULL;
  CHK(CREATE(&allocator, &type, &distrib) == RES_BAD_ARG);
  type.sample = ufacet_sample;
  type.eval = NULL;
  CHK(CREATE(&allocator, &type, &distrib) == RES_BAD_ARG);
  type.eval = ufacet_eval;
  type.pdf = NULL;
  CHK(CREATE(&allocator, &type, &distrib) == RES_BAD_ARG);
  type.pdf = ufacet_pdf;
  type.alignof_distribution = 0;
  CHK(CREATE(&allocator, &type, &distrib) == RES_BAD_ARG);
  type.alignof_distribution = ALIGNOF(struct ufacet);
  CHK(CREATE(&allocator, &type, &distrib) == RES_OK);
  CHK(ufacet_is_init == 1);
  #undef CREATE

  CHK(ssf_microfacet_distribution_get_data(NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_distribution_get_data(distrib, NULL) == RES_BAD_ARG);
  CHK(ssf_microfacet_distribution_get_data(NULL, (void**)&data) == RES_BAD_ARG);
  CHK(ssf_microfacet_distribution_get_data(distrib, (void**)&data) == RES_OK);
  CHK(data->id == 0xDECAFBAD);

  d3(N, 0.0, 1.0, 0.0);
  d3_set(data->N, N);
  d3_normalize(data->wh, d3(data->wh, 1, 2, 3));
  data->rng = rng;
  data->pdf = 0.1234;
  data->value = 0.43;

  ssf_microfacet_distribution_sample(distrib, rng, N, wh, NULL);
  ssf_microfacet_distribution_sample(distrib, rng, N, wh, &pdf);
  CHK(d3_eq(wh, data->wh) == 1);
  CHK(pdf == data->pdf);
  ssf_microfacet_distribution_sample(distrib, rng, N, wh, &pdf);
  CHK(d3_eq(wh, data->wh) == 1);
  CHK(pdf == data->pdf);

  CHK(ssf_microfacet_distribution_eval(distrib, N, wh) == data->value);
  CHK(ssf_microfacet_distribution_pdf(distrib, N, wh) == data->pdf);

  CHK(ssf_microfacet_distribution_ref_put(distrib) == RES_OK);
  CHK(ufacet_is_init == 0);

  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}
