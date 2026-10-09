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

static int bsdf_is_init = 0;

struct ALIGN(64) bsdf {
  uint32_t id;
  struct ssp_rng* rng;
  double wi[3];
  double wo[3];
  double N[3];
  double reflectivity;
  double value;
  double pdf;
  double rho;
};

static res_T
bsdf_init(struct mem_allocator* allocator, void* bsdf)
{
  CHK(allocator != NULL);
  CHK(bsdf != NULL);
  CHK(IS_ALIGNED(bsdf, 64) == 1);
  ((struct bsdf*)bsdf)->id = 0xDECAFBAD;
  bsdf_is_init = 1;
  return RES_OK;
}

static void
bsdf_release(void* bsdf)
{
  CHK(bsdf != NULL);
  CHK(((struct bsdf*)bsdf)->id == 0xDECAFBAD);
  bsdf_is_init = 0;
}

static double
bsdf_sample
  (void* bsdf,
   struct ssp_rng* rng,
   const double wo[3],
   const double N[3],
   double wi[3],
   int* type,
   double* pdf)
{
  struct bsdf* BxDF = bsdf;
  CHK(BxDF != NULL);
  CHK(BxDF->rng == rng);
  CHK(d3_eq(BxDF->wo, wo) == 1);
  CHK(d3_eq(BxDF->N, N) == 1);
  d3(wi, 1.0, 2.0, 3.0);
  if(type) *type = 314;
  if(pdf) *pdf = 4;
  return BxDF->reflectivity;
}

static double
bsdf_eval
  (void* bsdf,
   const double wo[3],
   const double N[3],
   const double wi[3])
{
  struct bsdf* BxDF = bsdf;
  CHK(BxDF != NULL);
  CHK(wi != NULL);
  CHK(wo != NULL);
  CHK(d3_eq(BxDF->wo, wo) == 1);
  CHK(d3_eq(BxDF->N, N) == 1);
  CHK(d3_eq(BxDF->wi, wi) == 1);
  return BxDF->value;
}

static double
bsdf_pdf
  (void* bsdf,
   const double wo[3],
   const double N[3],
   const double wi[3])
{
  struct bsdf* BxDF = bsdf;
  CHK(BxDF != NULL);
  CHK(wi != NULL);
  CHK(wo != NULL);
  CHK(d3_eq(BxDF->wo, wo) == 1);
  CHK(d3_eq(BxDF->N, N) == 1);
  CHK(d3_eq(BxDF->wi, wi) == 1);
  return BxDF->pdf;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct ssp_rng* rng;
  struct bsdf* data;
  struct ssf_bsdf* bsdf;
  struct ssf_bsdf_type type = SSF_BXDF_TYPE_NULL;
  struct ssf_bsdf_type type2 = bsdf_dummy;
  double wo[3];
  double N[3];
  double wi[4];
  double pdf;
  int i;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng) == RES_OK);

  type.init = bsdf_init;
  type.release = bsdf_release;
  type.sample = bsdf_sample;
  type.eval = bsdf_eval;
  type.pdf = bsdf_pdf;
  type.sizeof_bsdf = sizeof(struct bsdf);
  type.alignof_bsdf = ALIGNOF(struct bsdf);

  CHK(ssf_bsdf_create(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_bsdf_create(&allocator, NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_bsdf_create(NULL, &type, NULL) == RES_BAD_ARG);
  CHK(ssf_bsdf_create(&allocator, &type, NULL) == RES_BAD_ARG);
  CHK(ssf_bsdf_create(NULL, NULL, &bsdf) == RES_BAD_ARG);
  CHK(ssf_bsdf_create(&allocator, NULL, &bsdf) == RES_BAD_ARG);

  CHK(bsdf_is_init == 0);
  CHK(ssf_bsdf_create(NULL, &type, &bsdf) == RES_OK);
  CHK(bsdf_is_init == 1);

  CHK(ssf_bsdf_ref_get(NULL) == RES_BAD_ARG);
  CHK(ssf_bsdf_ref_get(bsdf) == RES_OK);
  CHK(ssf_bsdf_ref_put(NULL) == RES_BAD_ARG);
  CHK(ssf_bsdf_ref_put(bsdf) == RES_OK);
  CHK(bsdf_is_init == 1);
  CHK(ssf_bsdf_ref_put(bsdf) == RES_OK);
  CHK(bsdf_is_init == 0);

  CHK(ssf_bsdf_create(&allocator, &type, &bsdf) == RES_OK);
  CHK(bsdf_is_init == 1);
  CHK(ssf_bsdf_ref_put(bsdf) == RES_OK);
  CHK(bsdf_is_init == 0);

  type2.init = NULL;
  CHK(ssf_bsdf_create(&allocator, &type2, &bsdf) == RES_OK);
  CHK(ssf_bsdf_ref_put(bsdf) == RES_OK);
  type2.init = bsdf_dummy.init;
  type2.release = NULL;
  CHK(ssf_bsdf_create(&allocator, &type2, &bsdf) == RES_OK);
  CHK(ssf_bsdf_ref_put(bsdf) == RES_OK);

  type.sample = NULL;
  CHK(ssf_bsdf_create(&allocator, &type, &bsdf) == RES_BAD_ARG);
  CHK(bsdf_is_init == 0);
  type.sample = bsdf_sample;
  type.alignof_bsdf = 3;
  CHK(ssf_bsdf_create(&allocator, &type, &bsdf) == RES_BAD_ARG);
  CHK(bsdf_is_init == 0);
  type.alignof_bsdf = ALIGNOF(struct bsdf);
  CHK(ssf_bsdf_create(&allocator, &type, &bsdf) == RES_OK);
  CHK(bsdf_is_init == 1);

  CHK(ssf_bsdf_get_data(NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_bsdf_get_data(bsdf, NULL) == RES_BAD_ARG);
  CHK(ssf_bsdf_get_data(NULL, (void**)&data) == RES_BAD_ARG);
  CHK(ssf_bsdf_get_data(bsdf, (void**)&data) == RES_OK);

  CHK(data->id == 0xDECAFBAD);

  d3_normalize(wo, d3(wo, -1, -1, 0));
  d3(N, 0.0, 1.0, 0.0);
  d3_set(data->wo, wo);
  d3_set(data->N, N);
  data->rng = rng;
  data->reflectivity = 0.1234;

  i = 0, pdf = 0;
  CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, &i, NULL) == 0.1234);
  CHK(i == 314);
  CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, NULL, &pdf) == 0.1234);
  CHK(pdf == 4);
  CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, NULL, NULL) == 0.1234);

  data->reflectivity = 0.314;
  CHK(ssf_bsdf_sample(bsdf, rng, wo, N, wi, &i, &pdf) == 0.314);

  d3_normalize(wi, wi);
  d3_set(data->wi, wi);
  data->value = 0.4567;
  CHK(ssf_bsdf_eval(bsdf, wo, N, wi) == data->value);
  data->pdf = 0.890;
  CHK(ssf_bsdf_pdf(bsdf, wo, N, wi) == data->pdf);

  CHK(bsdf_is_init == 1);
  CHK(ssf_bsdf_ref_put(bsdf) == RES_OK);
  CHK(bsdf_is_init == 0);

  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

