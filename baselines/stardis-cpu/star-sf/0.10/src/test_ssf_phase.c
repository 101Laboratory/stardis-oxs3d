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

static int phase_is_init = 0;

struct ALIGN(64) phase {
  uint32_t id;
  struct ssp_rng* rng;
  double wi[3];
  double wo[3];
  double pdf;
  double value;
};

static res_T
phase_init(struct mem_allocator* allocator, void* phase)
{
  CHK(allocator != NULL);
  CHK(phase != NULL);
  CHK(IS_ALIGNED(phase, 64) == 1);
  ((struct phase*)phase)->id = 0xDECAFBAD;
  phase_is_init = 1;
  return RES_OK;
}

static void
phase_release(void* phase)
{
  CHK(phase != NULL);
  CHK(((struct phase*)phase)->id == 0xDECAFBAD);
  phase_is_init = 0;
}

static void
phase_sample
  (void* data,
   struct ssp_rng* rng,
   const double wo[3],
   double wi[3],
   double* pdf)
{
  struct phase* phase = data;
  CHK(phase != NULL);
  CHK(phase->rng == rng);
  CHK(d3_eq(phase->wo, wo) == 1);
  d3(wi, 1.0, 2.0, 3.0);
  if(pdf) *pdf = phase->pdf;
}

static double
phase_eval
  (void* data,
   const double wo[3],
   const double wi[3])
{
  struct phase* phase = data;
  CHK(phase != NULL);
  CHK(wi != NULL);
  CHK(wo != NULL);
  CHK(d3_eq(phase->wo, wo) == 1);
  CHK(d3_eq(phase->wi, wi) == 1);
  return phase->value;
}

static double
phase_pdf
  (void* data,
   const double wo[3],
   const double wi[3])
{
  struct phase* phase = data;
  CHK(phase != NULL);
  CHK(wi != NULL);
  CHK(wo != NULL);
  CHK(d3_eq(phase->wo, wo) == 1);
  CHK(d3_eq(phase->wi, wi) == 1);
  return phase->pdf;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct ssp_rng* rng;
  struct ssf_phase* phase = NULL;
  struct ssf_phase_type type = SSF_PHASE_TYPE_NULL;
  struct ssf_phase_type type2 = phase_dummy;
  struct phase* data = NULL;
  double wi[3];
  double pdf;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);
  CHK(ssp_rng_create(&allocator, SSP_RNG_THREEFRY, &rng) == RES_OK);

  type.init = phase_init;
  type.release = phase_release;
  type.sample = phase_sample;
  type.eval = phase_eval;
  type.pdf = phase_pdf;
  type.sizeof_phase = sizeof(struct phase);
  type.alignof_phase = ALIGNOF(struct phase);

  CHK(ssf_phase_create(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_phase_create(&allocator, NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_phase_create(NULL, &type, NULL) == RES_BAD_ARG);
  CHK(ssf_phase_create(&allocator, &type, NULL) == RES_BAD_ARG);
  CHK(ssf_phase_create(NULL, NULL, &phase) == RES_BAD_ARG);
  CHK(ssf_phase_create(&allocator, NULL, &phase) == RES_BAD_ARG);

  CHK(phase_is_init == 0);
  CHK(ssf_phase_create(NULL, &type, &phase) == RES_OK);
  CHK(phase_is_init == 1);

  CHK(ssf_phase_ref_get(NULL) == RES_BAD_ARG);
  CHK(ssf_phase_ref_get(phase) == RES_OK);
  CHK(ssf_phase_ref_put(NULL) == RES_BAD_ARG);
  CHK(ssf_phase_ref_put(phase) == RES_OK);
  CHK(phase_is_init == 1);
  CHK(ssf_phase_ref_put(phase) == RES_OK);
  CHK(phase_is_init == 0);

  type2.init = NULL;
  CHK(ssf_phase_create(&allocator, &type2, &phase) == RES_OK);
  CHK(ssf_phase_ref_put(phase) == RES_OK);
  type2.init = phase_dummy.init;
  type2.release = NULL;
  CHK(ssf_phase_create(&allocator, &type2, &phase) == RES_OK);
  CHK(ssf_phase_ref_put(phase) == RES_OK);

  type.sample = NULL;
  CHK(ssf_phase_create(&allocator, &type, &phase) == RES_BAD_ARG);
  CHK(phase_is_init == 0);
  type.sample = phase_sample;
  type.alignof_phase = 3;
  CHK(ssf_phase_create(&allocator, &type, &phase) == RES_BAD_ARG);
  CHK(phase_is_init == 0);
  type.alignof_phase = ALIGNOF(struct phase);
  CHK(ssf_phase_create(&allocator, &type, &phase) == RES_OK);
  CHK(phase_is_init == 1);

  CHK(ssf_phase_get_data(NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_phase_get_data(phase, NULL) == RES_BAD_ARG);
  CHK(ssf_phase_get_data(NULL, (void**)&data) == RES_BAD_ARG);
  CHK(ssf_phase_get_data(phase, (void**)&data) == RES_OK);

  CHK(data->id == 0xDECAFBAD);

  d3_normalize(data->wo, d3(data->wo, -1, -1, 0));
  data->rng = rng;
  data->value = 0.1234;
  data->pdf = 4;

  pdf = 0;
  ssf_phase_sample(phase, rng, data->wo, wi, NULL);
  ssf_phase_sample(phase, rng, data->wo, wi, &pdf);
  CHK(pdf == 4);

  d3_normalize(data->wi, wi);
  data->value = 0.4567;
  CHK(ssf_phase_eval(phase, data->wo, data->wi) == data->value);
  data->pdf = 0.890;
  CHK(ssf_phase_pdf(phase, data->wo, data->wi) == data->pdf);

  CHK(ssf_phase_ref_put(phase) == RES_OK);

  CHK(ssp_rng_ref_put(rng) == RES_OK);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

