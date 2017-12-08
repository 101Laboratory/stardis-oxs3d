/* Copyright (C) CNRS 2016-2017
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

#include <rsys/mem_allocator.h>

struct sdis_medium {
  enum sdis_medium_type type;
  union {
    struct sdis_solid_shader solid;
    struct sdis_fluid_shader fluid;
  } shader;

  struct sdis_data* data;

  ref_T ref;
  struct sdis_device* dev;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static int
check_fluid_shader(const struct sdis_fluid_shader* shader)
{
  return shader
      && shader->calorific_capacity
      && shader->volumic_mass
      && shader->initial_temperature;
}

static int
check_solid_shader(const struct sdis_solid_shader* shader)
{
  return shader
      && shader->calorific_capacity
      && shader->thermal_conductivity
      && shader->volumic_mass
      && shader->delta_solid
      && shader->delta_boundary
      && shader->initial_temperature;
}

static res_T
medium_create
  (struct sdis_device* dev,
   struct sdis_medium** out_medium,
   const enum sdis_medium_type type)
{
  struct sdis_medium* medium = NULL;
  res_T res = RES_OK;

  if(!dev || !out_medium || (unsigned)type >= SDIS_MEDIUM_TYPES_COUNT__) {
    res = RES_BAD_ARG;
    goto error;
  }
  medium = MEM_CALLOC(dev->allocator, 1, sizeof(struct sdis_medium));
  if(!medium) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&medium->ref);
  SDIS(device_ref_get(dev));
  medium->dev = dev;
  medium->type = type;

exit:
  if(out_medium) *out_medium = medium;
  return res;
error:
  if(medium) {
    SDIS(medium_ref_put(medium));
    medium = NULL;
  }
  goto exit;
}

static void
medium_release(ref_T* ref)
{
  struct sdis_medium* medium = NULL;
  struct sdis_device* dev = NULL;
  ASSERT(ref);
  medium = CONTAINER_OF(ref, struct sdis_medium, ref);
  dev = medium->dev;
  if(medium->data) SDIS(data_ref_put(medium->data));
  MEM_RM(dev->allocator, medium);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_fluid_create
  (struct sdis_device* dev,
   const struct sdis_fluid_shader* shader,
   struct sdis_data* data, /* May be NULL */
   struct sdis_medium** out_medium)
{
  struct sdis_medium* medium = NULL;
  res_T res = RES_OK;

  if(!out_medium) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(!check_fluid_shader(shader)) {
    log_err(dev, "%s: invalid fluid shader.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  res = medium_create(dev, &medium, SDIS_MEDIUM_FLUID);
  if(res != RES_OK) {
    log_err(dev, "%s: could not create the fluid medium.\n", FUNC_NAME);
    goto error;
  }

  if(data) {
    SDIS(data_ref_get(data));
    medium->data = data;
  }

  medium->shader.fluid = *shader;

exit:
  if(out_medium) *out_medium = medium;
  return res;
error:
  if(medium) {
    SDIS(medium_ref_put(medium));
    medium = NULL;
  }
  goto exit;
}

res_T
sdis_solid_create
  (struct sdis_device* dev,
   const struct sdis_solid_shader* shader,
   struct sdis_data* data, /* May be NULL */
   struct sdis_medium** out_medium)
{
  struct sdis_medium* medium = NULL;
  res_T res = RES_OK;

  if(!out_medium) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(!check_solid_shader(shader)) {
    log_err(dev, "%s: invalid solid shader.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  res = medium_create(dev, &medium, SDIS_MEDIUM_SOLID);
  if(res != RES_OK) {
    log_err(dev, "%s: could not create the solid medium.\n", FUNC_NAME);
    goto error;
  }

  if(data) {
    SDIS(data_ref_get(data));
    medium->data = data;
  }

  medium->shader.solid = *shader;

exit:
  if(out_medium) *out_medium = medium;
  return res;
error:
  if(medium) {
    SDIS(medium_ref_put(medium));
    medium = NULL;
  }
  goto exit;
}

res_T
sdis_medium_ref_get(struct sdis_medium* medium)
{
  if(!medium) return RES_BAD_ARG;
  ref_get(&medium->ref);
  return RES_OK;
}

res_T
sdis_medium_ref_put(struct sdis_medium* medium)
{
  if(!medium) return RES_BAD_ARG;
  ref_put(&medium->ref, medium_release);
  return RES_OK;
}

