/* Copyright (C) 2016-2025 |Méso|Star> (contact@meso-star.com)
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

#include "sdis.h"
#include "sdis_device_c.h"
#include "sdis_log.h"

#include <rsys_math.h>
#include <rsys/mem_allocator.h>

struct sdis_data {
  void* mem; /* Raw memory where user data are stored */
  void (*release)(void*); /* Function to invoke priorly to `mem' destruction */

  ref_T ref;
  struct sdis_device* dev;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
data_release(ref_T* ref)
{
  struct sdis_data* data = NULL;
  struct sdis_device* dev = NULL;
  ASSERT(ref);
  data = CONTAINER_OF(ref, struct sdis_data, ref);
  dev = data->dev;
  if(data->release) data->release(data->mem);
  if(data->mem) MEM_RM(dev->allocator, data->mem);
  MEM_RM(dev->allocator, data);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_data_create
  (struct sdis_device* dev,
   const size_t size,
   const size_t align,
   void (*release)(void*),
   struct sdis_data** out_data)
{
  struct sdis_data* data = NULL;
  res_T res = RES_OK;

  if(!dev || !size || !IS_POW2(align) || !out_data) {
    res = RES_BAD_ARG;
    goto error;
  }

  data = MEM_CALLOC(dev->allocator, 1, sizeof(struct sdis_data));
  if(!data) {
    log_err(dev, "%s: could not allocate the Stardis data.\n", FUNC_NAME);
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&data->ref);
  SDIS(device_ref_get(dev));
  data->dev = dev;
  data->release = release;

  data->mem = MEM_ALLOC_ALIGNED(dev->allocator, size, align);
  if(!data->mem) {
    log_err(dev, "%s: could not allocate the memory of the Stardis data. "
      "Size: %lu; alignment: %lu\n.", FUNC_NAME, size, align);
    res = RES_MEM_ERR;
    goto error;
  }

exit:
  if(out_data) *out_data = data;
  return res;

error:
  if(data) {
    SDIS(data_ref_put(data));
    data = NULL;
  }
  goto exit;
}

res_T
sdis_data_ref_get(struct sdis_data* data)
{
  if(!data) return RES_BAD_ARG;
  ref_get(&data->ref);
  return RES_OK;
}

res_T
sdis_data_ref_put(struct sdis_data* data)
{
  if(!data) return RES_BAD_ARG;
  ref_put(&data->ref, data_release);
  return RES_OK;
}

void*
sdis_data_get(struct sdis_data* data)
{
  if(!data) FATAL("invalid argument.\n");
  return data->mem;
}

const void*
sdis_data_cget(const struct sdis_data* data)
{
  if(!data) FATAL("invalid argument.\n");
  return data->mem;
}

