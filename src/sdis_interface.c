/* Copyright (C) |Meso|Star> 2016-2017 (contact@meso-star.com)
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
#include "sdis_interface_c.h"

#include <rsys/mem_allocator.h>

struct sdis_interface {
  struct sdis_medium* medium_front;
  struct sdis_medium* medium_back;
  struct sdis_interface_shader shader;
  struct sdis_data* data;
  struct fid id; /* Unique identifier of the interface */

  ref_T ref;
  struct sdis_device* dev;
};

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static int
check_interface_shader
  (const struct sdis_interface_shader* shader,
   const struct sdis_medium* front,
   const struct sdis_medium* back)
{
  enum sdis_medium_type type0;
  enum sdis_medium_type type1;
  ASSERT(shader && front && back);

  type0 = sdis_medium_get_type(front);
  type1 = sdis_medium_get_type(back);

  /* Fluid<->solid interface */
  if(type0 != type1 && shader->convection_coef == NULL) {
    return 0;
  }

  /* Solid<->solid interface */
  if(type0 == SDIS_MEDIUM_SOLID
  && type1 == SDIS_MEDIUM_SOLID
  && shader->convection_coef) {
    return 0;
  }

  return 1;
}

static void
interface_release(ref_T* ref)
{
  struct sdis_interface* interface = NULL;
  struct sdis_device* dev = NULL;
  ASSERT(ref);
  interface = CONTAINER_OF(ref, struct sdis_interface, ref);
  dev = interface->dev;
  if(interface->medium_front) SDIS(medium_ref_put(interface->medium_front));
  if(interface->medium_back) SDIS(medium_ref_put(interface->medium_back));
  if(interface->data) SDIS(data_ref_put(interface->data));
  flist_name_del(&dev->names, interface->id);
  MEM_RM(dev->allocator, interface);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/
res_T
sdis_interface_create
  (struct sdis_device* dev,
   struct sdis_medium* front,
   struct sdis_medium* back,
   const struct sdis_interface_shader* shader,
   struct sdis_data* data,
   struct sdis_interface** out_interface)
{
  struct sdis_interface* interface = NULL;
  res_T res = RES_OK;

  if(!dev || !front || !back || !shader || !out_interface) {
    res = RES_BAD_ARG;
    goto error;
  }

  if(sdis_medium_get_type(front) == SDIS_MEDIUM_FLUID
  && sdis_medium_get_type(back) == SDIS_MEDIUM_FLUID) {
    log_err(dev, "%s: invalid fluid<->fluid interface.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  if(!check_interface_shader(shader, front, back)) {
    log_err(dev, "%s: invalid interface shader.\n", FUNC_NAME);
    res = RES_BAD_ARG;
    goto error;
  }

  interface = MEM_CALLOC(dev->allocator, 1, sizeof(struct sdis_interface));
  if(!interface) {
    log_err(dev, "%s: could not create the interface.\n", FUNC_NAME);
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&interface->ref);
  SDIS(medium_ref_get(front));
  SDIS(medium_ref_get(back));
  SDIS(device_ref_get(dev));
  interface->medium_front = front;
  interface->medium_back = back;
  interface->dev = dev;
  interface->shader = *shader;
  interface->id = flist_name_add(&dev->names);

  if(data) {
    SDIS(data_ref_get(data));
    interface->data = data;
  }

exit:
  if(out_interface) *out_interface = interface;
  return res;
error:
  if(interface) {
    SDIS(interface_ref_put(interface));
    interface = NULL;
  }
  goto exit;
}

res_T
sdis_interface_ref_get(struct sdis_interface* interface)
{
  if(!interface) return RES_BAD_ARG;
  ref_get(&interface->ref);
  return RES_OK;
}

res_T
sdis_interface_ref_put(struct sdis_interface* interface)
{
  if(!interface) return RES_BAD_ARG;
  ref_put(&interface->ref, interface_release);
  return RES_OK;
}

/*******************************************************************************
 * Local function
 ******************************************************************************/
unsigned
interface_get_id(const struct sdis_interface* interface)
{
  ASSERT(interface);
  return interface->id.index;
}

