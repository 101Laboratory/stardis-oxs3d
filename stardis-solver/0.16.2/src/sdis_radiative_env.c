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

#include "sdis_device_c.h"
#include "sdis_log.h"
#include "sdis_radiative_env_c.h"

#include <rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static void
radiative_env_release(ref_T* ref)
{
  struct sdis_radiative_env* radenv = NULL;
  struct sdis_device* dev = NULL;
  ASSERT(ref);
  radenv = CONTAINER_OF(ref, struct sdis_radiative_env, ref);
  dev = radenv->dev;
  if(radenv->data) SDIS(data_ref_put(radenv->data));
  MEM_RM(dev->allocator, radenv);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_radiative_env_create
  (struct sdis_device* dev,
   const struct sdis_radiative_env_shader* shader,
   struct sdis_data* data, /* Data sent to the shader. May be NULL */
   struct sdis_radiative_env** out_radenv)
{
  struct sdis_radiative_env* radenv = NULL;
  res_T res = RES_OK;

  if(!dev || !shader || !out_radenv) {
    res = RES_BAD_ARG;
    goto error;
  }

  radenv = MEM_CALLOC(dev->allocator, 1, sizeof(*radenv));
  if(!radenv) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&radenv->ref);
  SDIS(device_ref_get(dev));
  radenv->dev = dev;
  radenv->shader = *shader;
  if(data) {
    SDIS(data_ref_get(data));
    radenv->data = data;
  }

exit:
  if(out_radenv) *out_radenv = radenv;
  return res;
error:
  goto exit;
}

res_T
sdis_radiative_env_ref_get(struct sdis_radiative_env* radenv)
{
  if(!radenv) return RES_BAD_ARG;
  ref_get(&radenv->ref);
  return RES_OK;
}

res_T
sdis_radiative_env_ref_put(struct sdis_radiative_env* radenv)
{
  if(!radenv) return RES_BAD_ARG;
  ref_put(&radenv->ref, radiative_env_release);
  return RES_OK;
}

res_T
sdis_radiative_env_get_shader
  (struct sdis_radiative_env* radenv,
   struct sdis_radiative_env_shader* shader)
{
  if(!radenv || !shader) return RES_BAD_ARG;
  *shader = radenv->shader;
  return RES_OK;
}

struct sdis_data*
sdis_radiative_env_get_data(struct sdis_radiative_env* radenv)
{
  ASSERT(radenv);
  return radenv->data;
}
