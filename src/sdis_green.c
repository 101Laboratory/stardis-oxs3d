/* Copyright (C) 2016-2019 |Meso|Star> (contact@meso-star.com)
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
#include "sdis_green.h"
#include "sdis_medium_c.h"
#include "sdis_interface_c.h"

#include <rsys/mem_allocator.h>

/*******************************************************************************
 * Helper functions
 ******************************************************************************/
static struct green_medium*
fetch_green_medium
  (struct sdis_green_function* green, struct sdis_medium* mdm)
{
  struct green_medium* gmdm = NULL;
  size_t i;
  res_T res = RES_OK;
  ASSERT(green && mdm);

  i = medium_get_id(mdm);

  /* Ensure that the LUT can store the medium */
  if(i >= darray_green_medium_size_get(&green->media)) {
    res = darray_green_medium_resize(&green->media, i+1);
    if(res != RES_OK) goto error;
  }

  gmdm = darray_green_medium_data_get(&green->media) + i;
  if(!gmdm->mdm) {
    /* Register the medium against the green function */
    SDIS(medium_ref_get(mdm));
    gmdm->mdm = mdm;
  }

exit:
  return res != RES_OK ? NULL : gmdm;
error:
  goto exit;
}

static struct green_interface*
fetch_green_interface
  (struct sdis_green_function* green, struct sdis_interface* interf)
{
  struct green_interface* ginterf = NULL;
  size_t i;
  res_T res = RES_OK;
  ASSERT(green && interf);

  i = interface_get_id(interf);

  /* Ensure that the LUT can store the interface */
  if(i >= darray_green_interface_size_get(&green->interfaces)) {
    res = darray_green_interface_resize(&green->interfaces, i+1);
    if(res != RES_OK) goto error;
  }

  ginterf = darray_green_interface_data_get(&green->interfaces) + i;
  if(!ginterf->interf) {
    /* Register the interface against the green function */
    SDIS(interface_ref_get(interf));
    ginterf->interf = interf;
  }

exit:
  return res != RES_OK ? NULL : ginterf;
error:
  goto exit;
}

static void
green_function_clear(struct sdis_green_function* green)
{
  size_t i, n;
  struct green_medium* media;
  struct green_interface* interfaces;
  ASSERT(green);

  n = darray_green_medium_size_get(&green->media);
  media = darray_green_medium_data_get(&green->media);
  FOR_EACH(i, 0, n) {
    if(media[i].mdm) {
      SDIS(medium_ref_put(media[i].mdm));
    }
  }
  darray_green_medium_clear(&green->media);

  n = darray_green_interface_size_get(&green->interfaces);
  interfaces = darray_green_interface_data_get(&green->interfaces);
  FOR_EACH(i, 0, n) {
    if(interfaces[i].interf) {
      SDIS(interface_ref_put(interfaces[i].interf));
    }
  }
  darray_green_interface_clear(&green->interfaces);
}

static void
green_function_release(ref_T* ref)
{
  struct sdis_device* dev;
  struct sdis_green_function* green;
  ASSERT(ref);
  green = CONTAINER_OF(ref, struct sdis_green_function, ref);
  dev = green->dev;
  green_function_clear(green);
  darray_green_medium_release(&green->media);
  darray_green_interface_release(&green->interfaces);
  MEM_RM(dev->allocator, green);
  SDIS(device_ref_put(dev));
}

/*******************************************************************************
 * Local functions
 ******************************************************************************/
res_T
green_function_create
  (struct sdis_device* dev, struct sdis_green_function** out_green)
{
  struct sdis_green_function* green = NULL;
  res_T res = RES_OK;
  ASSERT(dev && out_green);

  green = MEM_CALLOC(dev->allocator, 1, sizeof(*green));
  if(!green) {
    res = RES_MEM_ERR;
    goto error;
  }
  ref_init(&green->ref);
  SDIS(device_ref_get(dev));
  green->dev = dev;
  darray_green_medium_init(dev->allocator, &green->media);
  darray_green_interface_init(dev->allocator, &green->interfaces);

exit:
  *out_green = green;
  return res;
error:
  if(green) {
    green_function_ref_put(green);
    green = NULL;
  }
  goto exit;
}

void
green_function_ref_get(struct sdis_green_function* green)
{
  ASSERT(green);
  ref_get(&green->ref);
}

void
green_function_ref_put(struct sdis_green_function* green)
{
  ASSERT(green);
  ref_put(&green->ref, green_function_release);
}

res_T
green_function_add_medium_limit_vertex
  (struct sdis_green_function* green,
   struct sdis_medium* mdm,
   const struct green_vertex* vertex)
{
  struct green_medium* gmdm = NULL;
  ASSERT(green && mdm && vertex);
  gmdm = fetch_green_medium(green, mdm);
  if(!gmdm) return RES_MEM_ERR;
  return darray_green_vertex_push_back(&gmdm->limit_vertices, vertex);
}

res_T
green_function_add_interface_limit_vertex
  (struct sdis_green_function* green,
   struct sdis_interface* interf,
   const struct green_vertex* vertex)
{
  struct green_interface* ginterf = NULL;
  ASSERT(green && interf && vertex);
  ginterf = fetch_green_interface(green, interf);
  if(!ginterf) return RES_MEM_ERR;
  return darray_green_vertex_push_back(&ginterf->limit_vertices, vertex);
}

res_T
green_function_add_power_term
  (struct sdis_green_function* green,
   struct sdis_medium* mdm,
   const double term)
{
  struct green_medium* gmdm = NULL;
  ASSERT(green && mdm && term >= 0);
  gmdm = fetch_green_medium(green, mdm);
  if(!gmdm) return RES_MEM_ERR;
  gmdm->power_term += term;
  return RES_OK;
}

res_T
green_function_add_flux_term
  (struct sdis_green_function* green,
   struct sdis_interface* interf,
   const double term)
{
  struct green_interface* ginterf = NULL;
  ASSERT(green && interf && term >= 0);
  ginterf = fetch_green_interface(green, interf);
  if(!ginterf) return RES_MEM_ERR;
  ginterf->flux_term += term;
  return RES_OK;
}

