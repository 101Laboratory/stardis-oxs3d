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

#ifndef SMC_TYPE_REAL
  #error "Missing the SMC_TYPE_REAL definition"
#endif

#include <rsys/math.h>

#define SMC_REAL_FUNC__(Func) \
  CONCAT(CONCAT(CONCAT(smc_, SMC_TYPE_REAL), _), Func)

/*******************************************************************************
 * Function definition
 ******************************************************************************/
static void*
SMC_REAL_FUNC__(create)(struct mem_allocator* allocator, void* ctx)
{
  ASSERT(allocator);
  (void)ctx;
  return MEM_ALLOC(allocator, sizeof(SMC_TYPE_REAL));
}

static void
SMC_REAL_FUNC__(destroy)(struct mem_allocator* allocator, void* data)
{
  ASSERT(data);
  MEM_RM(allocator, data);
}

static void
SMC_REAL_FUNC__(set)(void* result, const void* value)
{
  ASSERT(result && value);
  *(SMC_TYPE_REAL*)result = *(const SMC_TYPE_REAL*)value;
}

static void
SMC_REAL_FUNC__(zero)(void* result)
{
  ASSERT(result);
  *(SMC_TYPE_REAL*)result = 0;
}

static void
SMC_REAL_FUNC__(add)(void* result, const void* op0, const void* op1)
{
  ASSERT(result && op0 && op1);
  *(SMC_TYPE_REAL*)result =
    *(const SMC_TYPE_REAL*)op0 + *(const SMC_TYPE_REAL*)op1;
}

static void
SMC_REAL_FUNC__(sub)(void* result, const void* op0, const void* op1)
{
  ASSERT(result && op0 && op1);
  *(SMC_TYPE_REAL*)result =
    *(const SMC_TYPE_REAL*)op0 - *(const SMC_TYPE_REAL*)op1;
}

static void
SMC_REAL_FUNC__(mul)(void* result, const void* op0, const void* op1)
{
  ASSERT(result && op0 && op1);
  *(SMC_TYPE_REAL*)result =
    *(const SMC_TYPE_REAL*)op0 * *(const SMC_TYPE_REAL*)op1;
}

static void
SMC_REAL_FUNC__(divi)(void* result, const void* op0, const size_t op1)
{
  ASSERT(result && op0 && op1);
  *(SMC_TYPE_REAL*)result =
    (SMC_TYPE_REAL)((double)(*(const SMC_TYPE_REAL*)op0) / (double)op1);
}

static void
SMC_REAL_FUNC__(sqrt)(void* result, const void* value)
{
  ASSERT(result && value && *(const SMC_TYPE_REAL*)value >= (SMC_TYPE_REAL)-1.e-6);
  *(SMC_TYPE_REAL*)result = (SMC_TYPE_REAL)sqrt
    (MMAX(*(const SMC_TYPE_REAL*)value, (SMC_TYPE_REAL)0.0));
}

/*******************************************************************************
 * Exported constant
 ******************************************************************************/
const struct smc_type CONCAT(smc_, SMC_TYPE_REAL) = {
  SMC_REAL_FUNC__(create),
  SMC_REAL_FUNC__(destroy),
  SMC_REAL_FUNC__(set),
  SMC_REAL_FUNC__(zero),
  SMC_REAL_FUNC__(add),
  SMC_REAL_FUNC__(sub),
  SMC_REAL_FUNC__(mul),
  SMC_REAL_FUNC__(divi),
  SMC_REAL_FUNC__(sqrt)
};

#undef SMC_REAL_FUNC__
#undef SMC_TYPE_REAL

