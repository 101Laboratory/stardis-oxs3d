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

static int fresnel_is_init = 0;

struct ALIGN(64) fresnel {
  uint32_t id;
  double cos_theta;
  double value;
};

static res_T
fresnel_init(struct mem_allocator* allocator, void* fresnel)
{
  CHK(allocator != NULL);
  CHK(fresnel != NULL);
  CHK(IS_ALIGNED(fresnel, 64) == 1);
  ((struct fresnel*)fresnel)->id = 0xDECAFBAD;
  fresnel_is_init = 1;
  return RES_OK;
}

static void
fresnel_release(void* fresnel)
{
  CHK(fresnel != NULL);
  CHK(((struct fresnel*)fresnel)->id == 0xDECAFBAD);
  fresnel_is_init = 0;
}

static double
fresnel_eval(void* fresnel, const double cos_theta)
{
  struct fresnel* f = fresnel;
  CHK(f != NULL);
  CHK(f->id == 0xDECAFBAD);
  CHK(f->cos_theta == cos_theta);
  return f->value;
}

int
main(int argc, char** argv)
{
  struct mem_allocator allocator;
  struct fresnel* data;
  struct ssf_fresnel* fresnel;
  struct ssf_fresnel_type type = SSF_FRESNEL_TYPE_NULL;
  struct ssf_fresnel_type type2 = fresnel_dummy;
  (void)argc, (void)argv;

  mem_init_proxy_allocator(&allocator, &mem_default_allocator);

  type.init = fresnel_init;
  type.release = fresnel_release;
  type.eval = fresnel_eval;
  type.sizeof_fresnel = sizeof(struct fresnel);
  type.alignof_fresnel = ALIGNOF(struct fresnel);

  CHK(ssf_fresnel_create(NULL, NULL, NULL) == RES_BAD_ARG);

  CHK(ssf_fresnel_create(NULL, NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_fresnel_create(&allocator, NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_fresnel_create(NULL, &type, NULL) == RES_BAD_ARG);
  CHK(ssf_fresnel_create(&allocator, &type, NULL) == RES_BAD_ARG);
  CHK(ssf_fresnel_create(NULL, NULL, &fresnel) == RES_BAD_ARG);
  CHK(ssf_fresnel_create(&allocator, NULL, &fresnel) == RES_BAD_ARG);

  CHK(fresnel_is_init == 0);
  CHK(ssf_fresnel_create(NULL, &type, &fresnel) == RES_OK);
  CHK(fresnel_is_init == 1);

  CHK(ssf_fresnel_ref_get(NULL) == RES_BAD_ARG);
  CHK(ssf_fresnel_ref_get(fresnel) == RES_OK);
  CHK(ssf_fresnel_ref_put(NULL) == RES_BAD_ARG);
  CHK(ssf_fresnel_ref_put(fresnel) == RES_OK);
  CHK(fresnel_is_init == 1);
  CHK(ssf_fresnel_ref_put(fresnel) == RES_OK);
  CHK(fresnel_is_init == 0);

  CHK(ssf_fresnel_create(&allocator, &type, &fresnel) == RES_OK);
  CHK(fresnel_is_init == 1);
  CHK(ssf_fresnel_ref_put(fresnel) == RES_OK);
  CHK(fresnel_is_init == 0);

  type2.init = NULL;
  CHK(ssf_fresnel_create(&allocator, &type2, &fresnel) == RES_OK);
  CHK(ssf_fresnel_ref_put(fresnel) == RES_OK);
  type2.init = fresnel_dummy.init;
  CHK(ssf_fresnel_create(&allocator, &type2, &fresnel) == RES_OK);
  CHK(ssf_fresnel_ref_put(fresnel) == RES_OK);
    
  type.eval = NULL;
  CHK(ssf_fresnel_create(&allocator, &type, &fresnel) == RES_BAD_ARG);
  CHK(fresnel_is_init == 0);
  type.eval = fresnel_eval;
  type.alignof_fresnel = 0;
  CHK(ssf_fresnel_create(&allocator, &type, &fresnel) == RES_BAD_ARG);
  CHK(fresnel_is_init == 0);
  type.alignof_fresnel = ALIGNOF(struct fresnel);
  CHK(ssf_fresnel_create(&allocator, &type, &fresnel) == RES_OK);
  CHK(fresnel_is_init == 1);

  CHK(ssf_fresnel_get_data(NULL, NULL) == RES_BAD_ARG);
  CHK(ssf_fresnel_get_data(fresnel, NULL) == RES_BAD_ARG);
  CHK(ssf_fresnel_get_data(NULL, (void**)&data) == RES_BAD_ARG);
  CHK(ssf_fresnel_get_data(fresnel, (void**)&data) == RES_OK);

  CHK(data->id == 0xDECAFBAD);
  data->cos_theta = 0.1;
  data->value = 1.234;

  CHK(ssf_fresnel_eval(fresnel, 0.1) == data->value);
  data->cos_theta = 1.234;
  data->value = 8.1;
  CHK(ssf_fresnel_eval(fresnel, 1.234) == data->value);

  CHK(fresnel_is_init == 1);
  CHK(ssf_fresnel_ref_put(fresnel) == RES_OK);
  CHK(fresnel_is_init == 0);

  check_memory_allocator(&allocator);
  mem_shutdown_proxy_allocator(&allocator);
  CHK(mem_allocated_size() == 0);
  return 0;
}

