/* Copyright (C) 2014-2017, 2020-2023 Vincent Forest (vaplv@free.fr)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>. */

#define _POSIX_C_SOURCE 200112L /* strtok_r support */

#include "aw_c.h"
#include <rsys/cstr.h>
#include <rsys/logger.h>
#include <rsys/mem_allocator.h>
#include <string.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
print_info(const char* msg, void* ctx)
{
  fprintf(stderr, "%s%s", ctx ? (const char*)ctx : "info: ", msg);
}

static void
print_err(const char* msg, void* ctx)
{
  fprintf(stderr, "%s%s", ctx ? (const char*)ctx : "error: ", msg);
}

static void
print_warn(const char* msg, void* ctx)
{
  fprintf(stderr, "%s%s", ctx ? (const char*)ctx : "warning: ", msg);
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
parse_doubleX
  (double* dblX,
   const unsigned int count_min,
   const unsigned int count_max,
   const double range_min,
   const double range_max,
   const double default_value,
   char** tk_ctx)
{
  size_t i;
  ASSERT(dblX && count_min <= count_max && range_min <= range_max && tk_ctx);
  ASSERT(default_value >= range_min && default_value <= range_max);

  FOR_EACH(i, 0, count_max) {
    char* real = strtok_r(NULL, " \t", tk_ctx);
    res_T res = RES_OK;

    if(!real) break;

    res = cstr_to_double(real, dblX + i);
    if(res != RES_OK) return res;

    if(dblX[i] < range_min || dblX[i] > range_max)
      return RES_BAD_ARG;
  }
  if(i < count_min)
    return RES_BAD_ARG;

  FOR_EACH(i, i, count_max)
    dblX[i] = default_value;

  return RES_OK;
}

res_T
setup_default_logger
  (struct mem_allocator* allocator,
   struct logger* logger,
   const char* prefix_info, /* May be NULL */
   const char* prefix_error, /* May be NULL */
   const char* prefix_warning) /* May be NULL */
{
  res_T res = RES_OK;
  ASSERT(logger);
  res = logger_init(allocator, logger);
  if(res != RES_OK) return res;
  logger_set_stream(logger, LOG_OUTPUT, print_info, (void*)prefix_info);
  logger_set_stream(logger, LOG_ERROR, print_err, (void*)prefix_error);
  logger_set_stream(logger, LOG_WARNING, print_warn, (void*)prefix_warning);
  return RES_OK;
}


