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
#include "sdis_medium_c.h"

#include <rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static res_T
check_fluid_shader(const struct sdis_fluid_shader* shader)
{
  if(!shader
  || !shader->calorific_capacity
  || !shader->volumic_mass
  || !shader->temperature)
    return RES_BAD_ARG;

  return RES_OK;
}

static res_T
check_solid_shader(const struct sdis_solid_shader* shader)
{
  if(!shader
  || !shader->calorific_capacity
  || !shader->thermal_conductivity
  || !shader->volumic_mass
  || !shader->delta
  || !shader->temperature)
    return RES_BAD_ARG;

  return RES_OK;
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
  medium->id = flist_name_add(&dev->media_names);
  flist_name_get(&dev->media_names, medium->id)->mem = medium;

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
  flist_name_del(&dev->media_names, medium->id);
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

  if(!dev || !out_medium) { res = RES_BAD_ARG; goto error; }

  res = check_fluid_shader(shader);
  if(res != RES_OK) {
    log_err(dev, "%s: invalid fluid shader.\n", FUNC_NAME);
    goto error;
  }

  res = medium_create(dev, &medium, SDIS_FLUID);
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
sdis_fluid_get_shader
  (const struct sdis_medium* mdm, struct sdis_fluid_shader* shader)
{
  if(!mdm || mdm->type != SDIS_FLUID || !shader) return RES_BAD_ARG;
  *shader = mdm->shader.fluid;
  return RES_OK;
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

  if(!dev || !out_medium) { res = RES_BAD_ARG; goto error; }

  res = check_solid_shader(shader);
  if(res != RES_OK) {
    log_err(dev, "%s: invalid solid shader.\n", FUNC_NAME);
    goto error;
  }

  res = medium_create(dev, &medium, SDIS_SOLID);
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
sdis_solid_get_shader
  (const struct sdis_medium* mdm, struct sdis_solid_shader* shader)
{
  if(!mdm || mdm->type != SDIS_SOLID || !shader) return RES_BAD_ARG;
  *shader = mdm->shader.solid;
  return RES_OK;
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

enum sdis_medium_type
sdis_medium_get_type(const struct sdis_medium* medium)
{
  ASSERT(medium != NULL);
  return medium->type;
}

struct sdis_data*
sdis_medium_get_data(struct sdis_medium* medium)
{
  ASSERT(medium);
  return medium->data;
}

unsigned
sdis_medium_get_id(const struct sdis_medium* medium)
{
  ASSERT(medium);
  return medium->id.index;
}

