/* Copyright (C) 2015-2018, 2021-2023 |Méso|Star> (contact@meso-star.com)
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

#include "smc.h"
#include <rsys/dynamic_array_double.h>
#include <rsys_math.h>

/*******************************************************************************
 * smc_doubleN functions
 ******************************************************************************/
static void
smc_doubleN_destroy(struct mem_allocator* allocator, void* data)
{
  struct darray_double* dblN = data;
  ASSERT(data);
  darray_double_release(dblN);
  MEM_RM(allocator, dblN);
}

static void*
smc_doubleN_create(struct mem_allocator* allocator, void* context)
{
  struct smc_doubleN_context* ctx = context;
  struct darray_double* dblN = NULL;
  res_T res = RES_OK;
  ASSERT(allocator);

  if(!ctx || !ctx->count) goto error;

  dblN = MEM_ALLOC(allocator, sizeof(struct darray_double));
  if(!dblN) goto error;

  darray_double_init(allocator, dblN);
  res = darray_double_resize(dblN, ctx->count);
  if(res != RES_OK) goto error;

exit:
  return dblN;
error:
  if(dblN) {
    smc_doubleN_destroy(allocator, dblN);
    dblN = NULL;
  }
  goto exit;
}

static void
smc_doubleN_set(void* result, const void* value)
{
  struct darray_double* dst = result;
  const struct darray_double* src = value;
  size_t i, n;
  ASSERT(dst && src);

  n = darray_double_size_get(dst);
  if(n !=  darray_double_size_get(src)) {
    FATAL("The vectors must have the same dimension.\n");
  }
  FOR_EACH(i, 0, n) {
    darray_double_data_get(dst)[i] = darray_double_cdata_get(src)[i];
  }
}

static void
smc_doubleN_zero(void* result)
{
  struct darray_double* dblN = result;
  ASSERT(result);
  memset(darray_double_data_get(dblN), 0,
    darray_double_size_get(dblN)*sizeof(double));
}

static void
smc_doubleN_add(void* result, const void* op0, const void* op1)
{
  struct darray_double* dst = result;
  const struct darray_double* a = op0;
  const struct darray_double* b = op1;
  size_t i, n;
  ASSERT(result && op0 && op1);

  n = darray_double_size_get(dst);
  if(n != darray_double_size_get(a) || n != darray_double_size_get(b)) {
    FATAL("The vectors must have the same dimension.\n");
  }
  FOR_EACH(i, 0, n) {
    darray_double_data_get(dst)[i] =
      darray_double_cdata_get(a)[i] + darray_double_cdata_get(b)[i];
  }
}

static void
smc_doubleN_sub(void* result, const void* op0, const void* op1)
{
  struct darray_double* dst = result;
  const struct darray_double* a = op0;
  const struct darray_double* b = op1;
  size_t i, n;
  ASSERT(result && op0 && op1);

  n = darray_double_size_get(dst);
  if(n != darray_double_size_get(a) || n != darray_double_size_get(b)) {
    FATAL("The vectors must have the same dimension.\n");
  }
  FOR_EACH(i, 0, n) {
    darray_double_data_get(dst)[i] =
      darray_double_cdata_get(a)[i] - darray_double_cdata_get(b)[i];
  }
}

static void
smc_doubleN_mul(void* result, const void* op0, const void* op1)
{
  struct darray_double* dst = result;
  const struct darray_double* a = op0;
  const struct darray_double* b = op1;
  size_t i, n;
  ASSERT(result && op0 && op1);

  n = darray_double_size_get(dst);
  if(n != darray_double_size_get(a) || n != darray_double_size_get(b)) {
    FATAL("The vectors must have the same dimension.\n");
  }
  FOR_EACH(i, 0, n) {
    darray_double_data_get(dst)[i] =
      darray_double_cdata_get(a)[i] * darray_double_cdata_get(b)[i];
  }
}

static void
smc_doubleN_divi(void* result, const void* op0, const size_t op1)
{
  struct darray_double* dst = result;
  const struct darray_double* a = op0;
  size_t i, n;
  ASSERT(result && op0 && op1);

  n = darray_double_size_get(dst);
  if(n != darray_double_size_get(a)) {
    FATAL("The vectors must have the same dimension.\n");
  }
  FOR_EACH(i, 0, n) {
    darray_double_data_get(dst)[i] = darray_double_cdata_get(a)[i]/(double)op1;
  }
}

static void
smc_doubleN_sqrt(void* result, const void* value)
{
  struct darray_double* dst = result;
  const struct darray_double* src = value;
  size_t i, n;
  ASSERT(result && value);

  n = darray_double_size_get(dst);
  if(n !=  darray_double_size_get(src)) {
    FATAL("The vectors must have the same dimension.\n");
  }
  FOR_EACH(i, 0, n) {
    darray_double_data_get(dst)[i] = sqrt(darray_double_cdata_get(src)[i]);
  }
}

/*******************************************************************************
 * Exported helper function
 ******************************************************************************/
double*
SMC_DOUBLEN(void* val)
{
  ASSERT(val);
  return darray_double_data_get(val);
}

/*******************************************************************************
 * Exported type
 ******************************************************************************/
const struct smc_type smc_doubleN = {
  smc_doubleN_create,
  smc_doubleN_destroy,
  smc_doubleN_set,
  smc_doubleN_zero,
  smc_doubleN_add,
  smc_doubleN_sub,
  smc_doubleN_mul,
  smc_doubleN_divi,
  smc_doubleN_sqrt
};

